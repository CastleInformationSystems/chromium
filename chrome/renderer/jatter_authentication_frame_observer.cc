#include "chrome/renderer/jatter_authentication_frame_observer.h"

#include "content/public/common/isolated_world_ids.h"
#include "content/public/renderer/render_frame.h"
#include "third_party/blink/public/platform/url_conversion.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "url/origin.h"
#include "v8/include/v8-context.h"
#include "v8/include/v8-function.h"

JatterAuthenticationFrameObserver::JatterAuthenticationFrameObserver(
    content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame) {}

void JatterAuthenticationFrameObserver::DidCreateScriptContext(
    v8::Local<v8::Context> context,
    int world_id) {
  LOG(INFO) << "JatterAuthenticationFrameObserver::DidCreateScriptContext";

  // Only allow in the main world
  if (world_id != content::ISOLATED_WORLD_ID_GLOBAL) {
    return;
  }

  content::RenderFrame* frame = render_frame();
  const url::Origin& origin = frame->GetWebFrame()->GetSecurityOrigin();

  // Check allowed origin
  if (origin.host() != "www.jatter.ai" &&
      origin.host() != "beacon-development-46c50.firebaseapp.com" &&
      origin.host() != "beacon-staging-df5f2.firebaseapp.com") {
    LOG(INFO) << "JatterAuthenticationFrameObserver::DidCreateScriptContext, "
                 "origin NOT allowed";
    return;
  }

  LOG(INFO) << "JatterAuthenticationFrameObserver::DidCreateScriptContext, "
               "origin allowed";

  v8::Isolate* isolate = context->GetIsolate();
  v8::HandleScope handle_scope(isolate);

  v8::Local<v8::Object> global = context->Global();

  global
      ->Set(context,
            v8::String::NewFromUtf8(isolate, "sendAuthToken").ToLocalChecked(),
            v8::Function::New(
                context,
                [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                  if (args.Length() < 1 || !args[0]->IsString()) {
                    return;
                  }

                  v8::String::Utf8Value token(args.GetIsolate(), args[0]);
                  std::string auth_token = *token;

                  // TODO: Send token to browser process via IPC
                  LOG(INFO) << "Received Firebase token: " << auth_token;
                })
                .ToLocalChecked())
      .Check();
}

void JatterAuthenticationFrameObserver::OnDestruct() {}
