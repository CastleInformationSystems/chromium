#include "chrome/renderer/jatter_authentication_frame_observer.h"

#include "content/public/common/isolated_world_ids.h"
#include "content/public/renderer/render_frame.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/url_conversion.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "url/origin.h"
#include "v8/include/v8-context.h"
#include "v8/include/v8-function.h"
#include "v8/include/v8.h"

JatterAuthenticationFrameObserver::JatterAuthenticationFrameObserver(
    content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame) {}

JatterAuthenticationFrameObserver::~JatterAuthenticationFrameObserver() =
    default;

void JatterAuthenticationFrameObserver::DidCreateScriptContext(
    v8::Local<v8::Context> context,
    int world_id) {
  // Only allow in the main world
  if (world_id != content::ISOLATED_WORLD_ID_GLOBAL) {
    return;
  }

  content::RenderFrame* frame = render_frame();
  const url::Origin& origin = frame->GetWebFrame()->GetSecurityOrigin();

  // Check allowed origin
  if (origin.host() != "www.jatter.ai" &&
      origin.host() != "beacon-development-46c50.firebaseapp.com" &&
      origin.host() != "beacon-staging-df5f2.firebaseapp.com" &&
      origin.host() != "localhost") {
    return;
  }

  v8::Isolate* isolate = context->GetIsolate();
  v8::HandleScope handle_scope(isolate);

  v8::Local<v8::Object> global = context->Global();

  v8::Local<v8::External> external_data = v8::External::New(isolate, this);

  global
      ->Set(
          context,
          v8::String::NewFromUtf8(isolate, "sendAuthToken").ToLocalChecked(),
          v8::Function::New(
              context,
              [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                LOG(INFO) << "sendAuthToken, enter";
                if (args.Length() < 1 || !args[0]->IsString()) {
                  return;
                }

                v8::String::Utf8Value token(args.GetIsolate(), args[0]);
                std::string auth_token = *token;

                // TODO: Send token to browser process via IPC
                LOG(INFO) << "Received Firebase token: " << auth_token;

                v8::Local<v8::Context> context =
                    args.GetIsolate()->GetCurrentContext();

                blink::WebLocalFrame* web_frame =
                    blink::WebLocalFrame::FrameForContext(context);
                if (web_frame) {
                  LOG(INFO) << "Got web_frame";
                  content::RenderFrame* current_frame =
                      content::RenderFrame::FromWebFrame(web_frame);

                  if (current_frame) {
                    LOG(INFO) << "Got current_frame";

                    v8::Local<v8::External> external =
                        v8::Local<v8::External>::Cast(args.Data());
                    JatterAuthenticationFrameObserver* my_frame =
                        static_cast<JatterAuthenticationFrameObserver*>(
                            external->Value());

                    mojo::Remote<jatter::mojom::JatterAuthorization>&
                        authorizationInterface =
                            my_frame->authorization_interface_;

                    if (!authorizationInterface.is_bound()) {
                      current_frame->GetBrowserInterfaceBroker().GetInterface(
                          authorizationInterface.BindNewPipeAndPassReceiver());
                    }

                    LOG(INFO) << "authorization_interface_.is_connected() = "
                              << authorizationInterface.is_connected();
                    if (authorizationInterface.is_connected()) {
                      authorizationInterface->SendAuthToken(auth_token);
                    }
                  }
                }
              },
              external_data)
              .ToLocalChecked())
      .Check();
}

void JatterAuthenticationFrameObserver::OnDestruct() {}
