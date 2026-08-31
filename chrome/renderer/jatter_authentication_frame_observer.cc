#include "chrome/renderer/jatter_authentication_frame_observer.h"

#include "content/public/common/isolated_world_ids.h"
#include "content/public/renderer/render_frame.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/scheduler/web_agent_group_scheduler.h"
#include "third_party/blink/public/platform/url_conversion.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "url/origin.h"
#include "v8/include/v8-context.h"
#include "v8/include/v8-function.h"
#include "v8/include/v8.h"

constexpr std::array<const char*, 8> kAllowedHosts = {
    "chat.jatter.ai",
    "www.jatter.ai",
    "app.jatter.ai",
    "beacon-development-46c50.firebaseapp.com",
    "beacon-development-46c50.web.app",
    "beacon-staging-df5f2.firebaseapp.com",
    "beacon-staging-df5f2.web.app",
    "localhost",
};

JatterAuthenticationFrameObserver::JatterAuthenticationFrameObserver(
    content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame) {}

JatterAuthenticationFrameObserver::~JatterAuthenticationFrameObserver() =
    default;

void JatterAuthenticationFrameObserver::DidCreateScriptContext(
    v8::Local<v8::Context> context,
    int world_id) {
  // early placement with regard to V8 API conventions
  v8::Isolate* isolate = render_frame()->GetWebFrame()->
      GetAgentGroupScheduler()->Isolate();
  v8::HandleScope handle_scope(isolate);

  // Only allow in the main world
  if (world_id != content::ISOLATED_WORLD_ID_GLOBAL) {
    return;
  }

  // Check allowed origin
  const url::Origin& origin = 
      render_frame()->GetWebFrame()->GetSecurityOrigin();

  if (std::find(kAllowedHosts.begin(), kAllowedHosts.end(), origin.host()) == 
      kAllowedHosts.end()) {
    return;
  }

  v8::Local<v8::Object> global = context->Global();

  v8::Local<v8::External> external_data = v8::External::New(isolate, this, v8::kExternalPointerTypeTagDefault);

  global
      ->Set(
          context,
          v8::String::NewFromUtf8(isolate, "sendAuthToken").ToLocalChecked(),
          v8::Function::New(
              context,
              [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                DLOG(INFO) << "sendAuthToken, enter";
                if (args.Length() < 1 || !args[0]->IsString()) {
                  return;
                }

                v8::String::Utf8Value token(args.GetIsolate(), args[0]);
                std::string auth_token = *token;

                // TODO: Send token to browser process via IPC
                DLOG(INFO) << "Received Firebase token: " << auth_token;

                v8::Local<v8::Context> context =
                    args.GetIsolate()->GetCurrentContext();

                blink::WebLocalFrame* web_frame =
                    blink::WebLocalFrame::FrameForContext(context);
                if (web_frame) {
                  DLOG(INFO) << "Got web_frame";
                  content::RenderFrame* current_frame =
                      content::RenderFrame::FromWebFrame(web_frame);

                  if (current_frame) {
                    DLOG(INFO) << "Got current_frame";

                    v8::Local<v8::External> external =
                        v8::Local<v8::External>::Cast(args.Data());
                    JatterAuthenticationFrameObserver* my_frame =
                        static_cast<JatterAuthenticationFrameObserver*>(
                            external->Value(v8::kExternalPointerTypeTagDefault));

                    mojo::Remote<jatter::mojom::JatterAuthorization>&
                        authorizationInterface =
                            my_frame->authorization_interface_;

                    if (!authorizationInterface.is_bound()) {
                      current_frame->GetBrowserInterfaceBroker().GetInterface(
                          authorizationInterface.BindNewPipeAndPassReceiver());
                    }

                    DLOG(INFO) << "authorization_interface_.is_connected() = "
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
      
  global
      ->Set(
          context,
          v8::String::NewFromUtf8(isolate, "sendPrivateKey").ToLocalChecked(),
          v8::Function::New(
              context,
              [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                DLOG(INFO) << "sendPrivateKey, enter";
                if (args.Length() < 1 || !args[0]->IsString()) {
                  return;
                }

                v8::String::Utf8Value token(args.GetIsolate(), args[0]);
                std::string private_key = *token;

                DLOG(INFO) << "Received Jatter Private Key.";

                v8::Local<v8::Context> context =
                    args.GetIsolate()->GetCurrentContext();

                blink::WebLocalFrame* web_frame =
                    blink::WebLocalFrame::FrameForContext(context);
                if (web_frame) {
                  content::RenderFrame* current_frame =
                      content::RenderFrame::FromWebFrame(web_frame);

                  if (current_frame) {
                    v8::Local<v8::External> external =
                        v8::Local<v8::External>::Cast(args.Data());
                    JatterAuthenticationFrameObserver* my_frame =
                        static_cast<JatterAuthenticationFrameObserver*>(
                            external->Value(v8::kExternalPointerTypeTagDefault));

                    mojo::Remote<jatter::mojom::JatterAuthorization>&
                        authorizationInterface =
                            my_frame->authorization_interface_;

                    if (!authorizationInterface.is_bound()) {
                      current_frame->GetBrowserInterfaceBroker().GetInterface(
                          authorizationInterface.BindNewPipeAndPassReceiver());
                    }

                    if (authorizationInterface.is_connected()) {
                      // Call the new Mojom method
                      authorizationInterface->SendPrivateKey(private_key);
                    }
                  }
                }
              },
              external_data)
              .ToLocalChecked())
      .Check();
}

void JatterAuthenticationFrameObserver::OnDestruct() {
  authorization_interface_.reset();
}
