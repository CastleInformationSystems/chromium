// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_SERVICE_H_
#define IOS_CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_SERVICE_H_

#include <memory>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "components/keyed_service/core/keyed_service.h"
#include "services/network/public/cpp/simple_url_loader.h"

class ProfileIOS;

namespace user_prefs {
class PrefRegistrySyncable;
}

// Forward declare the iOS Objective-C++ observer bridge
class JatterSessionObserverBridge;

class JatterAnalyticsService : public KeyedService {
 public:
  explicit JatterAnalyticsService(ProfileIOS* profile);
  JatterAnalyticsService(const JatterAnalyticsService&) = delete;
  JatterAnalyticsService& operator=(const JatterAnalyticsService&) = delete;
  ~JatterAnalyticsService() override;

  struct BridgeDeleter {
    void operator()(JatterSessionObserverBridge* ptr) const;
  };

  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  // KeyedService implementation:
  void Shutdown() override;

  // Called by platform hooks/bridges
  void RecordSessionEnded(base::TimeDelta session_length);

  void RecordNavigationEvent();
  void RecordDefaultBrowserSet();
  void RecordCustomEvent(const std::string& event_name, base::DictValue params);

 private:
  void QueueEvent(const std::string& event_name, base::DictValue params);
  void FlushEvents();
  void OnFlushCompleted(std::optional<std::string> response_body);
  std::string GetOrCreateClientId();

  // Platform-specific hooks
  void StartPlatformSessionTracking();
  void StopPlatformSessionTracking();

  raw_ptr<ProfileIOS> profile_;
  std::string client_id_;

  base::ListValue event_queue_;
  size_t queued_event_count_ = 0;
  base::RepeatingTimer flush_timer_;

  std::unique_ptr<JatterSessionObserverBridge, BridgeDeleter> session_observer_bridge_;

  base::WeakPtrFactory<JatterAnalyticsService> weak_factory_{this};
};

#endif  // IOS_CHROME_BROWSER_JATTER_ANALYTICS_JATTER_ANALYTICS_SERVICE_H_