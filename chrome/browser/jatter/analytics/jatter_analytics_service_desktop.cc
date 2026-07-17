// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/jatter/analytics/jatter_analytics_service.h"
#include "chrome/browser/metrics/desktop_session_duration/desktop_session_duration_tracker.h"

// The Bridge: Connects the Desktop Tracker to our Cross-Platform Service
class JatterSessionObserverBridge : public metrics::DesktopSessionDurationTracker::Observer {
 public:
  explicit JatterSessionObserverBridge(JatterAnalyticsService* service) : service_(service) {
    if (metrics::DesktopSessionDurationTracker::Get()) {
      metrics::DesktopSessionDurationTracker::Get()->AddObserver(this);
    }
  }

  ~JatterSessionObserverBridge() override {
    if (metrics::DesktopSessionDurationTracker::Get()) {
      metrics::DesktopSessionDurationTracker::Get()->RemoveObserver(this);
    }
  }

  // metrics::DesktopSessionDurationTracker::Observer implementation
  void OnSessionEnded(base::TimeDelta session_length, base::TimeTicks session_end) override {
    service_->RecordSessionEnded(session_length);
  }

 private:
  raw_ptr<JatterAnalyticsService> service_;
};

void JatterAnalyticsService::BridgeDeleter::operator()(JatterSessionObserverBridge* ptr) const {
  // Safe! The compiler knows the full size of the bridge here.
  delete ptr; 
}

// --- Platform Hooks ---

void JatterAnalyticsService::StartPlatformSessionTracking() {
  if (!session_observer_bridge_) {
    session_observer_bridge_.reset(new JatterSessionObserverBridge(this));
  }
}
void JatterAnalyticsService::StopPlatformSessionTracking() {
  // Destroying the unique_ptr automatically calls the bridge's destructor, 
  // which safely unregisters the observer from the tracker.
  session_observer_bridge_.reset();
}