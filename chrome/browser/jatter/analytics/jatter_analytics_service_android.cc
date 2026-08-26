// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/jatter/analytics/jatter_analytics_service.h"

// A dummy definition to satisfy the std::unique_ptr in the header
class JatterSessionObserverBridge {};

void JatterAnalyticsService::BridgeDeleter::operator()(JatterSessionObserverBridge* ptr) const {
  delete ptr; // Safely deletes the dummy object
}

// --- Platform Hooks ---

void JatterAnalyticsService::StartPlatformSessionTracking() {
  // On Android, session duration is tracked via Java Activity states 
  // (ApplicationStatus listeners). No C++ polling/trackers are needed.
}

void JatterAnalyticsService::StopPlatformSessionTracking() {
  // Safely do nothing.
  session_observer_bridge_.reset();
}