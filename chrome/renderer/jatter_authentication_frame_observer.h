#ifndef JATTER_AUTHENTICATION_FRAME_OBSERVER_H_
#define JATTER_AUTHENTICATION_FRAME_OBSERVER_H_

#include "content/public/renderer/render_frame_observer.h"

class JatterAuthenticationFrameObserver : public content::RenderFrameObserver {
 public:
  explicit JatterAuthenticationFrameObserver(
      content::RenderFrame* render_frame);

  void DidCreateScriptContext(v8::Local<v8::Context> context,
                              int32_t world_id) override;

  void OnDestruct() override;
};

#endif
