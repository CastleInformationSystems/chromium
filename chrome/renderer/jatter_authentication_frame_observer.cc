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

constexpr std::array<const char*, 7> kAllowedHosts = {
    "chat.jatter.ai",
    "www.jatter.ai",
    "app.jatter.ai",
    "beacon-development-46c50.firebaseapp.com",
    "beacon-development-46c50.web.app",
    "beacon-staging-df5f2.firebaseapp.com",
    "beacon-staging-df5f2.web.app",
};

JatterAuthenticationFrameObserver::JatterAuthenticationFrameObserver(
    content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame) {}

JatterAuthenticationFrameObserver::~JatterAuthenticationFrameObserver() =
    default;

void JatterAuthenticationFrameObserver::DidCreateScriptContext(
    v8::Local<v8::Context> context,
    int world_id) {
  v8::Isolate* isolate = render_frame()->GetWebFrame()->
      GetAgentGroupScheduler()->Isolate();
  v8::HandleScope handle_scope(isolate);

  if (world_id != content::ISOLATED_WORLD_ID_GLOBAL) {
    return;
  }

  const url::Origin& origin = 
      render_frame()->GetWebFrame()->GetSecurityOrigin();

  if (std::find(kAllowedHosts.begin(), kAllowedHosts.end(), origin.host()) == 
      kAllowedHosts.end()) {
    return;
  }

  v8::Local<v8::Object> global = context->Global();
  v8::Local<v8::External> external_data = 
      v8::External::New(isolate, this, v8::kExternalPointerTypeTagDefault);

  // 1. sendAuthToken
  global->Set(context,
              v8::String::NewFromUtf8(isolate, "sendAuthToken").ToLocalChecked(),
              v8::Function::New(
                  context,
                  [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                    if (args.Length() < 1 || !args[0]->IsString()) return;
                    v8::String::Utf8Value arg_val(args.GetIsolate(), args[0]);
                    std::string val = *arg_val;

                    v8::Local<v8::Context> current_context = args.GetIsolate()->GetCurrentContext();
                    if (blink::WebLocalFrame* web_frame = blink::WebLocalFrame::FrameForContext(current_context)) {
                      if (content::RenderFrame* current_frame = content::RenderFrame::FromWebFrame(web_frame)) {
                        v8::Local<v8::External> external = v8::Local<v8::External>::Cast(args.Data());
                        JatterAuthenticationFrameObserver* my_frame =
                            static_cast<JatterAuthenticationFrameObserver*>(
                                external->Value(v8::kExternalPointerTypeTagDefault));

                        mojo::Remote<jatter::mojom::JatterAuthorization>& auth_interface =
                                my_frame->authorization_interface_;

                        if (!auth_interface.is_bound()) {
                          current_frame->GetBrowserInterfaceBroker().GetInterface(
                              auth_interface.BindNewPipeAndPassReceiver());
                        }
                        if (auth_interface.is_connected()) {
                          auth_interface->SendAuthToken(val);
                        }
                      }
                    }
                  },
                  external_data)
                  .ToLocalChecked())
      .Check();

  // 2. sendPrivateKey
  global->Set(context,
              v8::String::NewFromUtf8(isolate, "sendPrivateKey").ToLocalChecked(),
              v8::Function::New(
                  context,
                  [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                    if (args.Length() < 1 || !args[0]->IsString()) return;
                    v8::String::Utf8Value arg_val(args.GetIsolate(), args[0]);
                    std::string val = *arg_val;

                    v8::Local<v8::Context> current_context = args.GetIsolate()->GetCurrentContext();
                    if (blink::WebLocalFrame* web_frame = blink::WebLocalFrame::FrameForContext(current_context)) {
                      if (content::RenderFrame* current_frame = content::RenderFrame::FromWebFrame(web_frame)) {
                        v8::Local<v8::External> external = v8::Local<v8::External>::Cast(args.Data());
                        JatterAuthenticationFrameObserver* my_frame =
                            static_cast<JatterAuthenticationFrameObserver*>(
                                external->Value(v8::kExternalPointerTypeTagDefault));

                        mojo::Remote<jatter::mojom::JatterAuthorization>& auth_interface =
                                my_frame->authorization_interface_;

                        if (!auth_interface.is_bound()) {
                          current_frame->GetBrowserInterfaceBroker().GetInterface(
                              auth_interface.BindNewPipeAndPassReceiver());
                        }
                        if (auth_interface.is_connected()) {
                          auth_interface->SendPrivateKey(val);
                        }
                      }
                    }
                  },
                  external_data)
                  .ToLocalChecked())
      .Check();

  // 3. sendPrivateKeySignature
  global->Set(context,
              v8::String::NewFromUtf8(isolate, "sendPrivateKeySignature").ToLocalChecked(),
              v8::Function::New(
                  context,
                  [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                    if (args.Length() < 1 || !args[0]->IsString()) return;
                    v8::String::Utf8Value arg_val(args.GetIsolate(), args[0]);
                    std::string val = *arg_val;

                    v8::Local<v8::Context> current_context = args.GetIsolate()->GetCurrentContext();
                    if (blink::WebLocalFrame* web_frame = blink::WebLocalFrame::FrameForContext(current_context)) {
                      if (content::RenderFrame* current_frame = content::RenderFrame::FromWebFrame(web_frame)) {
                        v8::Local<v8::External> external = v8::Local<v8::External>::Cast(args.Data());
                        JatterAuthenticationFrameObserver* my_frame =
                            static_cast<JatterAuthenticationFrameObserver*>(
                                external->Value(v8::kExternalPointerTypeTagDefault));

                        mojo::Remote<jatter::mojom::JatterAuthorization>& auth_interface =
                                my_frame->authorization_interface_;

                        if (!auth_interface.is_bound()) {
                          current_frame->GetBrowserInterfaceBroker().GetInterface(
                              auth_interface.BindNewPipeAndPassReceiver());
                        }
                        if (auth_interface.is_connected()) {
                          auth_interface->SendPrivateKeySignature(val);
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
