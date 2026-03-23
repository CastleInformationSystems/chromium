#ifndef CHROME_BROWSER_UI_WEBUI_JATTER_NTP_JATTER_NTP_UI_H_
#define CHROME_BROWSER_UI_WEBUI_JATTER_NTP_JATTER_NTP_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"

class JatterNtpUI : public content::WebUIController {
 public:
  explicit JatterNtpUI(content::WebUI* web_ui);
  JatterNtpUI(const JatterNtpUI&) = delete;
  JatterNtpUI& operator=(const JatterNtpUI&) = delete;
  ~JatterNtpUI() override;
};

// Modern Chromium uses WebUIConfig to register new endpoints
class JatterNtpUIConfig : public content::DefaultWebUIConfig<JatterNtpUI> {
 public:
  JatterNtpUIConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme, "jatter-ntp") {}
};

#endif  // CHROME_BROWSER_UI_WEBUI_JATTER_NTP_JATTER_NTP_UI_H_