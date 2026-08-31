// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/jatter/analytics/jatter_analytics_service.h"

#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/system/sys_info.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "ios/chrome/browser/jatter/jatter_environment.h"
#include "ios/chrome/browser/jatter/jatter_firebase_client.h"
#include "ios/chrome/browser/shared/model/profile/profile_ios.h"
#include "net/http/http_status_code.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace {

const char kJatterClientIdPref[] = "beacon.rag_ingestion.profile_guid";

constexpr base::TimeDelta kFlushInterval = base::Seconds(60);
constexpr size_t kMaxQueueSize = 50;

const net::NetworkTrafficAnnotationTag kAnalyticsTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("jatter_analytics_service_ios", R"(
      semantics {
        sender: "Jatter Analytics Service iOS"
        description: "Sends anonymized product usage telemetry to Jatter's backend."
        trigger: "Periodic flush of queued events (every 60s), or on app suspension."
        data: "Anonymous client UUID, event timestamps, and basic interaction metrics."
        destination: GOOGLE_OWNED_SERVICE
      }
      policy {
        cookies_allowed: NO
        setting: "This feature can be disabled in Settings under Privacy and Security."
      })");

}  // namespace

// static
void JatterAnalyticsService::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(kJatterClientIdPref, std::string());
}

JatterAnalyticsService::JatterAnalyticsService(ProfileIOS* profile)
    : profile_(profile) {
  client_id_ = GetOrCreateClientId();

  StartPlatformSessionTracking();

  base::DictValue open_params;
  open_params.Set("timestamp_micros",
                  std::to_string(base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds()));
  QueueEvent("app_open", std::move(open_params));

  flush_timer_.Start(FROM_HERE, kFlushInterval, this,
                     &JatterAnalyticsService::FlushEvents);
}

JatterAnalyticsService::~JatterAnalyticsService() = default;

void JatterAnalyticsService::Shutdown() {
  flush_timer_.Stop();
  StopPlatformSessionTracking();
  FlushEvents();
}

std::string JatterAnalyticsService::GetOrCreateClientId() {
  PrefService* prefs = profile_->GetPrefs();
  std::string client_id = prefs->GetString(kJatterClientIdPref);

  if (client_id.empty()) {
    client_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
    prefs->SetString(kJatterClientIdPref, client_id);
  }
  return client_id;
}

void JatterAnalyticsService::RecordNavigationEvent() {
  base::DictValue params;
  params.Set("timestamp_micros",
             std::to_string(base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds()));
  QueueEvent("navigation", std::move(params));
}

void JatterAnalyticsService::RecordDefaultBrowserSet() {
  base::DictValue params;
  params.Set("timestamp_micros",
             std::to_string(base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds()));
  QueueEvent("default_browser_set", std::move(params));
  FlushEvents();
}

void JatterAnalyticsService::RecordCustomEvent(const std::string& event_name,
                                               base::DictValue params) {
  if (!params.FindString("timestamp_micros")) {
    params.Set("timestamp_micros",
               std::to_string(base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds()));
  }
  QueueEvent(event_name, std::move(params));
}

void JatterAnalyticsService::RecordSessionEnded(base::TimeDelta session_length) {
  base::DictValue params;
  params.Set("engagement_time_msec", static_cast<double>(session_length.InMilliseconds()));
  params.Set("timestamp_micros",
             std::to_string(base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds()));

  QueueEvent("session_duration", std::move(params));
}

void JatterAnalyticsService::QueueEvent(const std::string& event_name,
                                        base::DictValue params) {
  // Centrally inject the operating system name into every event's parameters
  params.Set("os", base::SysInfo::OperatingSystemName());

  base::DictValue event_node;
  event_node.Set("name", event_name);
  event_node.Set("params", std::move(params));

  event_queue_.Append(std::move(event_node));
  queued_event_count_++;

  if (queued_event_count_ >= kMaxQueueSize) {
    FlushEvents();
  }
}

void JatterAnalyticsService::FlushEvents() {
  if (queued_event_count_ == 0 || !profile_) {
    return;
  }

  base::DictValue payload;
  payload.Set("client_id", client_id_);
  payload.Set("events", std::move(event_queue_));

  std::string json_string;
  base::JSONWriter::Write(payload, &json_string);

  event_queue_ = base::ListValue();
  queued_event_count_ = 0;

  JatterFirebaseClient::GetInstance()->Invoke(
      profile_,
      [json_string, profile = profile_]() {
        const std::string endpoint_url = std::string(jatter::kBaseApiUrl) + "/logAnalyticEvents";

        auto resource_request = std::make_unique<network::ResourceRequest>();
        resource_request->url = GURL(endpoint_url);
        resource_request->method = "POST";
        resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

        JatterFirebaseClient::SetAuthorizationHeader(profile, resource_request.get());

        auto loader = network::SimpleURLLoader::Create(
            std::move(resource_request), kAnalyticsTrafficAnnotation);

        loader->SetAllowHttpErrorResults(true);
        loader->AttachStringForUpload(json_string, "application/json");

        return loader;
      },
      base::BindOnce(&JatterAnalyticsService::OnFlushCompleted,
                     weak_factory_.GetWeakPtr()));
}

void JatterAnalyticsService::OnFlushCompleted(std::optional<std::string> response_body) {
  if (!response_body) {
#ifndef JATTER_PRODUCTION_MODE
    LOG(WARNING) << "[Jatter Analytics iOS] Failed to flush events to Firebase.";
#endif
  }
}