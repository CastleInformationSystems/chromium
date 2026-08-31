// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <UIKit/UIKit.h>

#include "ios/chrome/browser/jatter/analytics/jatter_analytics_service.h"

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"

@interface JatterIOSSessionObserver : NSObject
- (instancetype)initWithService:(JatterAnalyticsService*)service;
- (void)stop;
@end

@implementation JatterIOSSessionObserver {
  raw_ptr<JatterAnalyticsService> _service;
  base::TimeTicks _sessionStartTime;
  BOOL _isTracking;
  id<NSObject> _didBecomeActiveObserver;
  id<NSObject> _willResignActiveObserver;
}

- (instancetype)initWithService:(JatterAnalyticsService*)service {
  self = [super init];
  if (self) {
    _service = service;
    _isTracking = NO;
    [self startObserving];
  }
  return self;
}

- (void)startObserving {
  NSNotificationCenter* center = [NSNotificationCenter defaultCenter];

  _didBecomeActiveObserver =
      [center addObserverForName:UIApplicationDidBecomeActiveNotification
                          object:nil
                           queue:[NSOperationQueue mainQueue]
                      usingBlock:^(NSNotification* note) {
                        [self onAppDidBecomeActive];
                      }];

  _willResignActiveObserver =
      [center addObserverForName:UIApplicationWillResignActiveNotification
                          object:nil
                           queue:[NSOperationQueue mainQueue]
                      usingBlock:^(NSNotification* note) {
                        [self onAppWillResignActive];
                      }];

  if ([UIApplication sharedApplication].applicationState == UIApplicationStateActive) {
    [self onAppDidBecomeActive];
  }
}

- (void)onAppDidBecomeActive {
  if (!_isTracking) {
    _sessionStartTime = base::TimeTicks::Now();
    _isTracking = YES;
  }
}

- (void)onAppWillResignActive {
  if (_isTracking && _service) {
    base::TimeDelta duration = base::TimeTicks::Now() - _sessionStartTime;
    _service->RecordSessionEnded(duration);
    _isTracking = NO;
  }
}

- (void)stop {
  [self onAppWillResignActive];
  NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
  if (_didBecomeActiveObserver) {
    [center removeObserver:_didBecomeActiveObserver];
    _didBecomeActiveObserver = nil;
  }
  if (_willResignActiveObserver) {
    [center removeObserver:_willResignActiveObserver];
    _willResignActiveObserver = nil;
  }
  _service = nullptr;
}
@end

// Bridge container
class JatterSessionObserverBridge {
 public:
  explicit JatterSessionObserverBridge(JatterAnalyticsService* service) {
    observer_ = [[JatterIOSSessionObserver alloc] initWithService:service];
  }

  ~JatterSessionObserverBridge() {
    [observer_ stop];
    observer_ = nil;
  }

 private:
  JatterIOSSessionObserver* observer_;
};

void JatterAnalyticsService::BridgeDeleter::operator()(
    JatterSessionObserverBridge* ptr) const {
  delete ptr;
}

// --- Platform Hooks ---

void JatterAnalyticsService::StartPlatformSessionTracking() {
  if (!session_observer_bridge_) {
    session_observer_bridge_.reset(new JatterSessionObserverBridge(this));
  }
}

void JatterAnalyticsService::StopPlatformSessionTracking() {
  session_observer_bridge_.reset();
}