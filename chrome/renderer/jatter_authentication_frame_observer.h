#ifndef JATTER_AUTHENTICATION_FRAME_OBSERVER_H_
#define JATTER_AUTHENTICATION_FRAME_OBSERVER_H_

#include "chrome/browser/jatter/jatter.mojom.h"
#include "content/public/renderer/render_frame_observer.h"
#include "mojo/public/cpp/bindings/remote.h"

class JatterAuthenticationFrameObserver : public content::RenderFrameObserver {
 public:
  explicit JatterAuthenticationFrameObserver(
      content::RenderFrame* render_frame);
  JatterAuthenticationFrameObserver(const JatterAuthenticationFrameObserver&) = delete;
  JatterAuthenticationFrameObserver& operator=(const JatterAuthenticationFrameObserver&) = delete;
  ~JatterAuthenticationFrameObserver() override;
 
 private:
  void DidCreateScriptContext(v8::Local<v8::Context> context,
                              int32_t world_id) override;

  void OnDestruct() override;

  mojo::Remote<jatter::mojom::JatterAuthorization> authorization_interface_;
};

#endif
