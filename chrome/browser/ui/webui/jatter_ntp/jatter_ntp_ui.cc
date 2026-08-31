#include "chrome/browser/ui/webui/jatter_ntp/jatter_ntp_ui.h"

#include "chrome/browser/jatter/jatter_environment.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/browser_resources.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"

JatterNtpUI::JatterNtpUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  
  // Minimal data source for the trampoline
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), "jatter-ntp");

  source->SetDefaultResource(IDR_JATTER_NTP_HTML);
  source->AddString("appIframeUrl", jatter::kAppUrl);

  // Simple CSP: Just allow it to exist and redirect
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ScriptSrc, 
      "script-src 'self' 'unsafe-inline' chrome://resources;");
}

JatterNtpUI::~JatterNtpUI() = default;