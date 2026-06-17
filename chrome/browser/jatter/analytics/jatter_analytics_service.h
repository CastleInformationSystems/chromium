// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_SERVICE_H_
#define CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_SERVICE_H_

#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/metrics/desktop_session_duration/desktop_session_duration_tracker.h"
#include "components/keyed_service/core/keyed_service.h"
#include "services/network/public/cpp/simple_url_loader.h"

class Profile;

namespace user_prefs {
class PrefRegistrySyncable;
}

class JatterAnalyticsService 
    : public KeyedService,
      public metrics::DesktopSessionDurationTracker::Observer {
 public:
  explicit JatterAnalyticsService(Profile* profile);
  JatterAnalyticsService(const JatterAnalyticsService&) = delete;
  JatterAnalyticsService& operator=(const JatterAnalyticsService&) = delete;
  ~JatterAnalyticsService() override;

  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  // KeyedService implementation:
  void Shutdown() override;

  // DesktopSessionDurationTracker::Observer implementation:
  void OnSessionEnded(base::TimeDelta session_length,
                      base::TimeTicks session_end) override;

  // Public API for other Chromium components to queue events
  void RecordNavigationEvent();
  void RecordDefaultBrowserSet();
  void RecordCustomEvent(const std::string& event_name, base::DictValue params);

 private:
  // Core queuing and networking
  void QueueEvent(const std::string& event_name, base::DictValue params);
  void FlushEvents();
  void OnFlushCompleted(std::optional<std::string> response_body);

  // Generates or retrieves the persistent UUID for this installation
  std::string GetOrCreateClientId();

  raw_ptr<Profile> profile_;
  std::string client_id_;

  // In-memory queue
  base::ListValue event_queue_;
  size_t queued_event_count_ = 0; // Tracks size safely across API versions
  
  // Timer to flush the queue to the backend
  base::RepeatingTimer flush_timer_;

  base::WeakPtrFactory<JatterAnalyticsService> weak_factory_{this};
};

#endif  // CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_SERVICE_H_