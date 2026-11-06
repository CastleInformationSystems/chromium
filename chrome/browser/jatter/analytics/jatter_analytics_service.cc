// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/jatter/analytics/jatter_analytics_service.h"

#include "base/i18n/rtl.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/system/sys_info.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "build/build_config.h"
#include "chrome/browser/jatter/jatter_environment.h"
#include "chrome/browser/jatter/jatter_firebase_client.h"
#include "chrome/browser/jatter/jatter_firebase_client_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/storage_partition.h"
#include "net/http/http_status_code.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/mojom/url_response_head.mojom.h" // Required for ResponseInfo

namespace {

// Fix C4: New dedicated pref for analytics
const char kJatterClientIdPref[] = "jatter.analytics.client_id";
const char kOldRagGuidPref[] = "beacon.rag_ingestion.profile_guid";
// New dedicated flag for first-run
const char kFirstRunLoggedPref[] = "jatter.analytics.first_run_logged";
const char kSessionNumberPref[] = "jatter.analytics.session_number";

const char kPendingEventsPref[] = "jatter.analytics.pending_events";
const size_t kMaxQueueSize = 500;

constexpr base::TimeDelta kFlushInterval = base::Seconds(60);

const net::NetworkTrafficAnnotationTag kAnalyticsTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("jatter_analytics_service", R"(
      semantics {
        sender: "Jatter Analytics Service"
        description: "Sends product usage events (app opens, browser sessions, navigations, default-browser changes) to Jatter's backend, which forwards them to Google Analytics 4."
        trigger: "Every 60 seconds while events are queued, at the end of a browser session, and at startup for events saved from the previous run."
        data: "A random per-profile analytics ID; the Firebase ID token when signed in, from which the backend derives the user ID; session ID and number; event names and times; engagement time; OS, OS version, browser version, locale and device model. The backend forwards the request's IP address to Google Analytics for coarse location. No URLs or page content."
        destination: OTHER
        destination_other: "Jatter's backend (Firebase Cloud Functions), which forwards events to Google Analytics 4."
      }
      policy {
        cookies_allowed: NO
        setting: "This feature cannot be disabled in settings. It does not run in Incognito or Guest profiles."
        policy_exception_justification: "Not implemented."
      })");

}  // namespace

// static
void JatterAnalyticsService::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(kJatterClientIdPref, std::string());
  // Fix C4: Keep registering the RAG GUID because the RAG service reads it 
  // but doesn't register it itself.
  registry->RegisterStringPref(kOldRagGuidPref, std::string());
  registry->RegisterBooleanPref(kFirstRunLoggedPref, false);
  registry->RegisterIntegerPref(kSessionNumberPref, 0);
  registry->RegisterListPref(kPendingEventsPref); 
}

JatterAnalyticsService::JatterAnalyticsService(Profile* profile)
    : profile_(profile) {
  client_id_ = GetOrCreateClientId();

  // Fix C6.A: Load pending events from disk from a previous interrupted session
  const base::ListValue& saved_events = profile_->GetPrefs()->GetList(kPendingEventsPref);
  if (!saved_events.empty()) {
    event_queue_ = saved_events.Clone();
    queued_event_count_ = event_queue_.size();
    
    // Wipe the disk cache now that it is loaded into memory
    profile_->GetPrefs()->SetList(kPendingEventsPref, base::ListValue());
  }

  StartPlatformSessionTracking();

  QueueEvent("app_open", base::DictValue());

  flush_timer_.Start(FROM_HERE, kFlushInterval, this,
                     &JatterAnalyticsService::FlushEvents);
}

JatterAnalyticsService::~JatterAnalyticsService() = default;

void JatterAnalyticsService::Shutdown() {
  flush_timer_.Stop();

  // Call the platform-specific teardown hook
  StopPlatformSessionTracking();

  // 1. Recover any events that were in the middle of a network request, 
  // because the request will be instantly killed by the browser closing.
  base::ListValue merged_queue;
  for (const auto& val : in_flight_events_) {
    merged_queue.Append(val.Clone());
  }
  for (const auto& val : event_queue_) {
    merged_queue.Append(val.Clone());
  }
  event_queue_ = std::move(merged_queue);
  in_flight_events_ = base::ListValue(); // Clear it out

  // 2. Save the fully merged queue to disk (capped at 500 events)
  if (!event_queue_.empty()) {
    base::ListValue events_to_save;
    size_t start_idx = event_queue_.size() > kMaxQueueSize ? 
                       event_queue_.size() - kMaxQueueSize : 0;
    
    for (size_t i = start_idx; i < event_queue_.size(); ++i) {
      events_to_save.Append(event_queue_[i].Clone());
    }
    
    profile_->GetPrefs()->SetList(kPendingEventsPref, std::move(events_to_save));
  }

  // NOTE: The old FlushEvents() call is deliberately removed here. 
  // The events are safely on disk and will be flushed on the next browser launch!
}

std::string JatterAnalyticsService::GetOrCreateClientId() {
  PrefService* prefs = profile_->GetPrefs();
  std::string client_id = prefs->GetString(kJatterClientIdPref);

  if (client_id.empty()) {
    client_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
    prefs->SetString(kJatterClientIdPref, client_id);
  }

  // Safely check and log the first run independently of the GUID generation
  if (!prefs->GetBoolean(kFirstRunLoggedPref)) {
    QueueEvent("browser_first_run", base::DictValue());
    prefs->SetBoolean(kFirstRunLoggedPref, true);
  }

  return client_id;
}

void JatterAnalyticsService::StartSession() {
  PrefService* prefs = profile_->GetPrefs();
  
  // Increment the lifetime session counter
  session_number_ = prefs->GetInteger(kSessionNumberPref) + 1;
  prefs->SetInteger(kSessionNumberPref, session_number_);

  // Generate the timestamp-based session ID
  session_id_ = (base::Time::Now() - base::Time::UnixEpoch()).InSeconds();
  
  // Reset the time trackers
  engagement_mark_ = base::TimeTicks::Now();
  reported_engagement_ = base::TimeDelta(); 

  // Fire the start event with 1ms minimum engagement so GA4 registers it as an active session
  base::DictValue params;
  params.Set("engagement_time_msec", 1.0); 
  QueueEvent("browser_session_start", std::move(params));
}

// ===========================================================================
// Public API Hooks
// ===========================================================================

void JatterAnalyticsService::RecordNavigationEvent() {
  QueueEvent("navigation", base::DictValue());
}

void JatterAnalyticsService::RecordDefaultBrowserSet() {
  QueueEvent("default_browser_set", base::DictValue());
  FlushEvents(); 
}

void JatterAnalyticsService::RecordCustomEvent(const std::string& event_name, 
                                               base::DictValue params) {
  // Drop caller-supplied timestamps
  params.Remove("timestamp_micros"); 
  QueueEvent(event_name, std::move(params));
}
void JatterAnalyticsService::RecordSessionEnded(base::TimeDelta session_length) {
  // QueueEvent will automatically calculate the final time delta since the last event
  QueueEvent("session_duration", base::DictValue());

  // Clear the active session ID so background tasks don't get grouped into it
  session_id_ = 0; 
  
  FlushEvents();
}

// ===========================================================================
// Core Queuing & Networking
// ===========================================================================

void JatterAnalyticsService::QueueEvent(const std::string& event_name, 
                                        base::DictValue params) {
  params.Set("os", base::SysInfo::OperatingSystemName());

  // Fix C1.A: Attach session variables and calculate incremental time
  if (session_id_ > 0) {
    params.Set("session_id", static_cast<double>(session_id_));
    params.Set("session_number", session_number_);

    // Calculate time elapsed since the last event (or since session start)
    base::TimeTicks now = base::TimeTicks::Now();
    base::TimeDelta delta = now - engagement_mark_;
    
    // Only attach engagement time if it's not the start event we just forced to 1ms
    if (event_name != "browser_session_start") {
      params.Set("engagement_time_msec", static_cast<double>(delta.InMilliseconds()));
    }
    
    reported_engagement_ += delta;
    engagement_mark_ = now; // Move the marker forward for the next event
  }

  base::DictValue event_node;
  event_node.Set("name", event_name);
  event_node.Set("params", std::move(params));

  // Fix C3: Top-level numeric timestamp in microseconds since Unix Epoch
  double timestamp_micros = static_cast<double>(
      (base::Time::Now() - base::Time::UnixEpoch()).InMicroseconds());
  event_node.Set("timestamp_micros", timestamp_micros);

  event_queue_.Append(std::move(event_node));
  queued_event_count_++;
#ifndef JATTER_PRODUCTION_MODE
  LOG(INFO) << "[JatterAnalytics] Queued event: '" << event_name 
            << "' | Queue size is now: " << queued_event_count_ 
            << "/" << kMaxQueueSize;
#endif
  if (queued_event_count_ >= kMaxQueueSize) {
#ifndef JATTER_PRODUCTION_MODE
    LOG(INFO) << "[JatterAnalytics] Max queue size reached. Flushing events to backend.";
#endif
    FlushEvents();
  }
}

void JatterAnalyticsService::FlushEvents() {
  if (queued_event_count_ == 0 || !profile_) {
    return;
  }

  base::DictValue payload;
  payload.Set("client_id", client_id_);
  
  // Fix C8: Inject the hardware and OS device block
  base::DictValue device_block;
  device_block.Set("operating_system", base::SysInfo::OperatingSystemName());
  device_block.Set("operating_system_version", base::SysInfo::OperatingSystemVersion());
  device_block.Set("model", base::SysInfo::HardwareModelName());
  device_block.Set("browser_version", version_info::GetVersionNumber());
  device_block.Set("language", base::i18n::GetConfiguredLocale());
#if BUILDFLAG(IS_ANDROID)
  device_block.Set("category", "mobile");
#else
  device_block.Set("category", "desktop");
#endif

  payload.Set("device", std::move(device_block));
  payload.Set("events", event_queue_.Clone());

  std::string json_string;
  base::JSONWriter::Write(payload, &json_string);

  // Reset the queue safely
  // Fix C7: Move events to in-flight instead of wiping them
  in_flight_events_ = std::move(event_queue_);
  event_queue_ = base::ListValue();
  queued_event_count_ = 0;

  // Fix C9: Safely check for the client before invoking
  auto* firebase_client = JatterFirebaseClientFactory::GetForProfile(profile_);
  if (!firebase_client) {
    return;
  }

  JatterFirebaseClientFactory::GetForProfile(profile_)->Invoke(
      profile_,
      [json_string, profile = profile_]() {
        const std::string endpoint_url = std::string(jatter::kBaseApiUrl) + "/logAnalyticEvents";
        
        auto resource_request = std::make_unique<network::ResourceRequest>();
        resource_request->url = GURL(endpoint_url);
        resource_request->method = "POST";
        resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

        // INJECT THE FIREBASE BEARER TOKEN
        JatterFirebaseClient::SetAuthorizationnHeader(profile, resource_request.get());

        auto loader = network::SimpleURLLoader::Create(
            std::move(resource_request), kAnalyticsTrafficAnnotation);
            
        loader->SetAllowHttpErrorResults(true);
        loader->AttachStringForUpload(json_string, "application/json");
        
        return loader;
      },
      base::BindOnce(&JatterAnalyticsService::OnFlushCompleted,
                     weak_factory_.GetWeakPtr()),
      /*allow_anonymous=*/true);
}

void JatterAnalyticsService::OnFlushCompleted(std::optional<std::string> response_body) {
  bool should_requeue = false;

  if (!response_body) {
    // Hard network failure (timeout, no connection, firewall)
    should_requeue = true;
  } else {
    // Parse backend JSON response
    auto parsed_json = base::JSONReader::Read(*response_body, base::JSON_PARSE_RFC);
    if (parsed_json && parsed_json->is_dict()) {
      // Find the status key
      const std::string* status = parsed_json->GetDict().FindString("status");
      
      // If backend explicitly says "error" (e.g. Firebase 500 internal error), requeue.
      // If "ok" or "invalid", we DO NOT requeue. They are either successful or malformed.
      if (status && *status == "error") {
        should_requeue = true;
      }
    } else {
      // Malformed response from backend, assume failure
      should_requeue = true;
    }
  }

  if (should_requeue) {
    // Prepend the failed in-flight events back to the main queue
    base::ListValue merged_queue;
    for (const auto& val : in_flight_events_) {
      merged_queue.Append(val.Clone());
    }
    for (const auto& val : event_queue_) {
      merged_queue.Append(val.Clone());
    }
    
    event_queue_ = std::move(merged_queue);
    queued_event_count_ = event_queue_.size();
  }

  // Wipe the in-flight queue regardless of outcome
  in_flight_events_ = base::ListValue();
}
