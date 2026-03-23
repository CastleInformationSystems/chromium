// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains constants for known URLs and portions thereof.
// Except for WebUI UI/Host/SubPage constants. Those go in
// chrome/common/webui_url_constants.h.
//
// - The constants are divided into sections: Cross platform, platform-specific,
//   and feature-specific.
// - When adding platform/feature specific constants, if there already exists an
//   appropriate #if block, use that.
// - Keep the constants sorted by name within its section.

#ifndef CHROME_COMMON_URL_CONSTANTS_H_
#define CHROME_COMMON_URL_CONSTANTS_H_

#include <stddef.h>

#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "chrome/common/buildflags.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/common/buildflags.h"
#include "content/public/common/url_constants.h"
#include "net/net_buildflags.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "ash/constants/chrome_url_constants.h"
#endif  // BUILDFLAG(IS_CHROMEOS)

namespace chrome {

// "Learn more" URL linked in the dialog to cast using a code.
inline constexpr char kAccessCodeCastLearnMoreURL[] =
    "";

// "Learn more" URL for accessibility image labels, linked from the permissions
// dialog shown when a user enables the feature.
inline constexpr char kAccessibilityLabelsLearnMoreURL[] =
    "";

// "Learn more" URL for Ad Privacy.
inline constexpr char kAdPrivacyLearnMoreURL[] =
    "";

// "Learn more" URL for when profile settings are automatically reset.
inline constexpr char kAutomaticSettingsResetLearnMoreURL[] =
    "";

// "Learn more" URL for Advanced Protection download warnings.
inline constexpr char kAdvancedProtectionDownloadLearnMoreURL[] =
    "";

// "Chrome Settings" URL for the appearance page.
inline constexpr char kBrowserSettingsSearchEngineURL[] =
    "chrome://settings/search";

// "Learn more" URL for Battery Saver Mode.
inline constexpr const char16_t kBatterySaverModeLearnMoreUrl[] =
    u"";

// The URL for providing help when the Bluetooth adapter is off.
inline constexpr char kBluetoothAdapterOffHelpURL[] =
    "";

// "Learn more" URL shown in the dialog to enable cloud services for Cast.
inline constexpr char kCastCloudServicesHelpURL[] =
    "";

// The URL for the help center article to show when no Cast destination has been
// found.
inline constexpr char kCastNoDestinationFoundURL[] =
    "";

// The URL for the WebHID API help center article.
inline constexpr char kChooserHidOverviewUrl[] =
    "";

// The URL for the Web Serial API help center article.
inline constexpr char kChooserSerialOverviewUrl[] =
    "";

// The URL for the WebUsb help center article.
inline constexpr char kChooserUsbOverviewURL[] =
    "";

// Link to the forum for Chrome Beta.
inline constexpr char kChromeBetaForumURL[] =
    "";

// The URL for the help center article to fix Chrome update problems.
inline constexpr char16_t kChromeFixUpdateProblems[] =
    u"";

// General help links for Chrome, opened using various actions.
inline constexpr char kChromeHelpViaKeyboardURL[] =
#if BUILDFLAG(IS_CHROMEOS)
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
    "chrome-extension://honijodknafkokifofgiaalefdiedpko/main.html";
#else
    "";
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING)
#else
    "";
#endif  // BUILDFLAG(IS_CHROMEOS)

inline constexpr char kChromeHelpViaMenuURL[] =
#if BUILDFLAG(IS_CHROMEOS)
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
    "chrome-extension://honijodknafkokifofgiaalefdiedpko/main.html";
#else
    "";
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING)
#else
    "";
#endif  // BUILDFLAG(IS_CHROMEOS)

inline constexpr char kChromeHelpViaWebUIURL[] =
    "https://www.jatter.ai/help/";
#if BUILDFLAG(IS_CHROMEOS)
inline constexpr char kChromeOsHelpViaWebUIURL[] =
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
    "chrome-extension://honijodknafkokifofgiaalefdiedpko/main.html";
#else
    "";
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING)
#endif  // BUILDFLAG(IS_CHROMEOS)

// The chrome-native: scheme is used show pages rendered with platform specific
// widgets instead of using HTML.
inline constexpr char kChromeNativeScheme[] = "chrome-native";

// The URL of safe section in Chrome page (https://www.google.com/chrome).
inline constexpr char16_t kChromeSafePageURL[] =
    u"https://www.google.com/chrome/#safe";

// Host and URL for most visited iframes used on the Instant Extended NTP.
inline constexpr char kChromeSearchMostVisitedHost[] = "most-visited";
inline constexpr char kChromeSearchMostVisitedUrl[] =
    "chrome-search://most-visited/";

// URL for NTP custom background image selected from the user's machine and
// filename for the version of the file in the Profile directory
inline constexpr char kChromeUIUntrustedNewTabPageBackgroundUrl[] =
    "chrome-untrusted://new-tab-page/background.jpg";
inline constexpr char kChromeUIUntrustedNewTabPageBackgroundFilename[] =
    "background.jpg";

// Page under chrome-search.
inline constexpr char kChromeSearchRemoteNtpHost[] = "remote-ntp";

// The chrome-search: scheme is served by the same backend as chrome:.  However,
// only specific URLDataSources are enabled to serve requests via the
// chrome-search: scheme.  See |InstantIOContext::ShouldServiceRequest| and its
// callers for details.  Note that WebUIBindings should never be granted to
// chrome-search: pages.  chrome-search: pages are displayable but not readable
// by external search providers (that are rendered by Instant renderer
// processes), and neither displayable nor readable by normal (non-Instant) web
// pages.  To summarize, a non-Instant process, when trying to access
// 'chrome-search://something', will bump up against the following:
//
//  1. Renderer: The display-isolated check in WebKit will deny the request,
//  2. Browser: Assuming they got by #1, the scheme checks in
//     URLDataSource::ShouldServiceRequest will deny the request,
//  3. Browser: for specific sub-classes of URLDataSource, like ThemeSource
//     there are additional Instant-PID checks that make sure the request is
//     coming from a blessed Instant process, and deny the request.
inline constexpr char kChromeSearchScheme[] = "chrome-search";

// This is the base URL of content that can be embedded in chrome://new-tab-page
// using an <iframe>. The embedded untrusted content can make web requests and
// can include content that is from an external source.
inline constexpr char kChromeUIUntrustedNewTabPageUrl[] =
    "chrome-untrusted://new-tab-page/";

// The URL for the Chromium project used in the About dialog.
inline constexpr char16_t kChromiumProjectURL[] = u"https://www.chromium.org/";
#if BUILDFLAG(IS_CHROMEOS)
static_assert(std::u16string_view(kChromiumProjectURL) ==
              ash::chrome_external_urls::kChromiumProjectURL);
#endif

#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
// The URL for the "Clear browsing data in Chrome" help center article.
inline constexpr char16_t kClearBrowsingDataHelpCenterURL[] =
    u"";
#endif

inline constexpr char16_t kContentSettingsExceptionsLearnMoreURL[] =
    u"";

// "Learn more" URL for cookies.
inline constexpr char kCookiesSettingsHelpCenterURL[] =
    "";

// "Learn more" URL for "Aw snap" page when showing "Reload" button.
inline constexpr char kCrashReasonURL[] =
#if BUILDFLAG(IS_CHROMEOS)
    "";
#else
    "";
#endif

// "Learn more" URL for "Aw snap" page when showing "Send feedback" button.
inline constexpr char kCrashReasonFeedbackDisplayedURL[] =
#if BUILDFLAG(IS_CHROMEOS)
    "";
#else
    "";
#endif

// "Learn more" URL for the inactive tabs appearance setting.
inline constexpr const char16_t kDiscardRingTreatmentLearnMoreUrl[] =
    u"";

// "Learn more" URL for the "Do not track" setting in the privacy section.
inline constexpr char16_t kDoNotTrackLearnMoreURL[] =
#if BUILDFLAG(IS_CHROMEOS)
    u"";
#else
    u"";
#endif

// The URL for the "Learn more" page for interrupted downloads.
inline constexpr char kDownloadInterruptedLearnMoreURL[] =
    "";

// The URL for the "Learn more" page for download scanning.
inline constexpr char kDownloadScanningLearnMoreURL[] =
    "";

// The URL for the "Learn more" page for blocked downloads.
// Note: This is the same as the above URL. This is done to decouple the URLs,
// in case the support page is split apart into separate pages in the future.
inline constexpr char kDownloadBlockedLearnMoreURL[] =
    "";

// "Learn more" URL for the Settings API, NTP bubble and other settings bubbles
// showing which extension is controlling them.
inline constexpr char kExtensionControlledSettingLearnMoreURL[] =
    "";

// Link for creating family group with Google Families.
inline constexpr char16_t kFamilyGroupCreateURL[] =
    u"https://myaccount.google.com/family/create?utm_source=cpwd";

// Link for viewing family group with Google Families.
inline constexpr char16_t kFamilyGroupViewURL[] =
    u"https://myaccount.google.com/family/details?utm_source=cpwd";

// "Learn more" URL for related website sets.
inline constexpr char kRelatedWebsiteSetsLearnMoreURL[] =
    ""
    "chrome?p=cpn_cookies&rd=1#allow_block_cookies&zippy=%2Callow-or-block-"
    "third-party-cookies%2Callow-related-sites-to-access-your-activity%2Cabout-"
    "embedded-content";

// Url to a blogpost about Flash deprecation.
inline constexpr char kFlashDeprecationLearnMoreURL[] =
    "https://blog.chromium.org/2017/07/so-long-and-thanks-for-all-flash.html";

// URL of the 'Activity controls' section of the privacy settings page.
inline constexpr char kGoogleAccountActivityControlsURL[] =
    "https://myaccount.google.com/activitycontrols/search";

// URL of the 'Activity controls' section of the privacy settings page, with
// privacy guide parameters and a link for users to manage data.
inline constexpr char kGoogleAccountActivityControlsURLInPrivacyGuide[] =
    "https://myaccount.google.com/activitycontrols/"
    "search&utm_source=chrome&utm_medium=privacy-guide";

// URL of the 'Linked services' section of the privacy settings page.
inline constexpr char kGoogleAccountLinkedServicesURL[] =
    "https://myaccount.google.com/linked-services?utm_source=chrome_s";

// URL of the Google Account.
inline constexpr char kGoogleAccountURL[] = "https://myaccount.google.com";

// URL of the Google Account chooser.
inline constexpr char kGoogleAccountChooserURL[] =
    "https://accounts.google.com/AccountChooser";

// URL of the Google Account page showing the known user devices.
inline constexpr char kGoogleAccountDeviceActivityURL[] =
    "https://myaccount.google.com/device-activity?utm_source=chrome";

// URL of the Google Account home address page.
inline constexpr char kGoogleAccountHomeAddressURL[] =
    "https://myaccount.google.com/address/"
    "home?utm_source=chrome&utm_campaign=manage_addresses";

// URL of the Google Account work address page.
inline constexpr char kGoogleAccountWorkAddressURL[] =
    "https://myaccount.google.com/address/"
    "work?utm_source=chrome&utm_campaign=manage_addresses";

// URL of the change Google Account name page.
inline constexpr char kGoogleAccountNameEmailAddressEditURL[] =
    "https://myaccount.google.com/"
    "personal-info?utm_source=chrome-settings&utm_medium=autofill";

// URL of the two factor authentication setup required intersitial.
inline constexpr char kGoogleTwoFactorIntersitialURL[] =
    "https://myaccount.google.com/interstitials/twosvrequired";

// URL of the Google Password Manager.
inline constexpr char kGooglePasswordManagerURL[] =
    "https://passwords.google.com";

// URL of the Google Photos.
inline constexpr char kGooglePhotosURL[] = "https://photos.google.com";

// The URL for the "Learn more" link for the Memory Saver Mode.
inline constexpr const char16_t kMemorySaverModeLearnMoreUrl[] =
    u"";

// The URL in the help text for the Memory Saver Mode tab discarding
// exceptions add dialog.
inline constexpr char16_t kMemorySaverModeTabDiscardingHelpUrl[] =
    u"";

// The URL to the help center article of Incognito mode.
inline constexpr char16_t kIncognitoHelpCenterURL[] =
    u"";

// The URL for the "Learn more" page for the usage/crash reporting option in the
// first run dialog.
inline constexpr char kLearnMoreReportingURL[] =
    "";

// The URL for the tab group sync help center page.
inline constexpr char kTabGroupsLearnMoreURL[] =
    "";

// The URL for the Learn More page about policies and enterprise enrollment.
inline constexpr char16_t kManagedUiLearnMoreUrl[] =
#if BUILDFLAG(IS_CHROMEOS)
    u"";
#else
    u"";
#endif

// The URL for the "Learn more" page for insecure download blocking.
inline constexpr char kInsecureDownloadBlockingLearnMoreUrl[] =
    "";

// "myactivity.google.com" URLs with their respective UTM sources.
// - In the Clear Browsing Data footer.
// - In the Clear Browsing Data "notice about other forms of history".
// - On the history page.
inline constexpr char16_t kMyActivityUrlInClearBrowsingData[] =
    u"https://myactivity.google.com/myactivity?utm_source=chrome_cbd";
inline constexpr char16_t kMyActivityUrlInClearBrowsingDataNotice[] =
    u"https://myactivity.google.com/myactivity/?utm_source=chrome_n";
inline constexpr char16_t kMyActivityUrlInHistory[] =
    u"https://myactivity.google.com/myactivity/?utm_source=chrome_h";

// The URL for the Gemini Personal Context page.
inline constexpr char16_t kGeminiPersonalContextUrl[] =
    u"https://gemini.google.com/saved-info";

// The URL for "Your Gemini Apps Activity" page.
inline constexpr char16_t kMyActivityGeminiAppsUrl[] =
    u"https://myactivity.google.com/product/gemini";

// The URL for the AI Mode activity page.
inline constexpr char16_t kMyActivityAiModeUrl[] =
    u"https://myactivity.google.com/myactivity?product=83";

#if !BUILDFLAG(IS_CHROMEOS) && BUILDFLAG(GOOGLE_CHROME_BRANDING)
// "Learn more" URL for On-Device AI.
inline constexpr char16_t kOnDeviceAiLearnMoreUrl[] =
    u"https://support.google.com/chrome?p=on_device_genAI";
#endif

// Help URL for the Omnibox setting.
inline constexpr char16_t kOmniboxLearnMoreURL[] =
#if BUILDFLAG(IS_CHROMEOS)
    u"";
#else
    u"";
#endif

// "What do these mean?" URL for the Page Info bubble.
inline constexpr char kPageInfoHelpCenterURL[] =
#if BUILDFLAG(IS_CHROMEOS)
    "";
#else
    "";
#endif

// Help center article URL for automated password change.
inline constexpr char16_t kPasswordChangeLearnMoreURL[] =
    u"https://jatter.ai/help";

// Help URL for the bulk password check.
inline constexpr char kPasswordCheckLearnMoreURL[] =
#if BUILDFLAG(IS_CHROMEOS)
    ""
    "?p=settings_password#leak_detection_privacy";
#else
    ""
    "?p=settings_password#leak_detection_privacy";
#endif

// Help URL for password generation.
inline constexpr char kPasswordGenerationLearnMoreURL[] =
    "https://jatter.ai/help";

inline constexpr char16_t kPasswordManagerLearnMoreURL[] =
#if BUILDFLAG(IS_CHROMEOS)
    u"";
#else
    u"https://jatter.ai/help";
#endif

// Help URL for passwords import.
inline constexpr char kPasswordManagerImportLearnMoreURL[] =
    "https://jatter.ai/help";

// Help URL for password sharing.
inline constexpr char kPasswordSharingLearnMoreURL[] =
    "https://jatter.ai/help";

// Help URL for troubleshooting password sharing.
inline constexpr char kPasswordSharingTroubleshootURL[] =
    "https://jatter.ai/help";

// The URL for the "Fill out forms automatically" support page.
inline constexpr char kAddressesAndPaymentMethodsLearnMoreURL[] =
#if BUILDFLAG(IS_CHROMEOS)
    "";
#else
    "https://jatter.ai/help";
#endif

// The URL for the "Pay over time" support page.
// TODO(crbug.com/395027230): Change URL once official support page is
// finalized.
inline constexpr char16_t kPayOverTimeLearnMoreUrl[] =
    u"";

// The URL for the Wallet website.
inline constexpr char16_t kWalletUrl[] = u"https://wallet.google.com";

// Help URL for Autofill AI.
inline constexpr char16_t kAutofillAiLearnMoreURL[] =
    u"";

// "Learn more" URL for the autofill show card benefits setting.
inline constexpr char16_t kCardBenefitsLearnMoreURL[] =
    u"";

// "Learn more" URL for the performance intervention notification setting.
inline constexpr const char16_t kPerformanceInterventionLearnMoreUrl[] =
    u"";

// "Learn more" URL for the preloading section in Performance settings.
inline constexpr const char16_t kPreloadingLearnMoreUrl[] =
    u"";

// "Learn more" URL for the Privacy section under Options.
inline constexpr char kPrivacyLearnMoreURL[] =
#if BUILDFLAG(IS_CHROMEOS)
    "";
#else
    "";
#endif

// "Chrome Settings" URL for Ad Topics page
inline constexpr char kPrivacySandboxAdTopicsURL[] =
    "chrome://settings/adPrivacy/interests";

// "Chrome Settings" URL for Managing Topics page
inline constexpr char kPrivacySandboxManageTopicsURL[] =
    "chrome://settings/adPrivacy/interests/manage";

#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
// The Privacy Sandbox homepage.
inline constexpr char16_t kPrivacySandboxURL[] =
    u"https://www.privacysandbox.com/";
#endif

// The URL for the Learn More link of the non-CWS bubble.
inline constexpr char kRemoveNonCWSExtensionURL[] =
    "";

// "Learn more" URL for resetting profile preferences.
inline constexpr char kResetProfileSettingsLearnMoreURL[] =
    "";

// "Learn more" URL for Safebrowsing
inline constexpr char kSafeBrowsingHelpCenterURL[] =
    "";

// Updated "Info icon" URL for Safebrowsing
inline constexpr char kSafeBrowsingHelpCenterUpdatedURL[] =
    "";

// "Learn more" URL for Enhanced Protection
inline constexpr char16_t kSafeBrowsingInChromeHelpCenterURL[] =
    u"";

// The URL for Safe Browsing link in Safety Check page.
inline constexpr char16_t kSafeBrowsingUseInChromeURL[] =
    u"";

// "Learn more" URL for Safety Check page.
inline constexpr char16_t kSafetyHubHelpCenterURL[] =
    u"";

// "Learn more" URL for safety tip bubble.
inline constexpr char kSafetyTipHelpCenterURL[] =
    "";

// Google search history URL that leads users of the CBD dialog to their search
// history in their Google account.
inline constexpr char16_t kSearchHistoryUrlInClearBrowsingData[] =
    u"https://myactivity.google.com/product/search?utm_source=chrome_cbd";

// The URL for the "See more security tips" with advices how to create a strong
// password.
inline constexpr char kSeeMoreSecurityTipsURL[] =
    "https://jatter.ai/help";

// Help URL for the settings page's search feature.
inline constexpr char16_t kSettingsSearchHelpURL[] =
    u"";

// The URL for the Learn More page about Sync and Google services.
inline constexpr char kSyncAndGoogleServicesLearnMoreURL[] =
    "";
#if BUILDFLAG(IS_CHROMEOS)
static_assert(std::string_view(kSyncAndGoogleServicesLearnMoreURL) ==
              ash::chrome_external_urls::kSyncAndGoogleServicesLearnMoreURL);
#endif

// The URL for the "Learn more" page on sync encryption.
inline constexpr char16_t kSyncEncryptionHelpURL[] =
#if BUILDFLAG(IS_CHROMEOS)
    u"";
#else
    u"";
#endif

// The URL for the "Learn more" link when there is a sync error.
inline constexpr char kSyncErrorsHelpURL[] =
    "";

// Legacy URL to the sync google dashboard.
inline constexpr char kLegacySyncGoogleDashboardURL[] =
    "https://www.google.com/settings/chrome/sync";
#if BUILDFLAG(IS_CHROMEOS)
static_assert(std::string_view(kLegacySyncGoogleDashboardURL) ==
              ash::chrome_external_urls::kLegacySyncGoogleDashboardURL);
#endif

// New URL to the sync google dashboard.
inline constexpr char kNewSyncGoogleDashboardURL[] =
    "https://chrome.google.com/data";
#if BUILDFLAG(IS_CHROMEOS)
static_assert(std::string_view(kNewSyncGoogleDashboardURL) ==
              ash::chrome_external_urls::kNewSyncGoogleDashboardURL);
#endif

// The URL for the "Learn more" page for sync setup on the personal stuff page.
inline constexpr char16_t kSyncLearnMoreURL[] =
    u"";

// The URL for the "Learn more" page for signing in to chrome with expanded
// section on "Sign in and turn on sync" in the Computer/Desktop tab.
inline constexpr char kSigninOnDesktopLearnMoreURL[] =
    ""
    "chrome?p=settings_sign_in#zippy=sign-in-turn-on-sync";

// The URL for the "Learn more" page for adding a new profile to Chrome.
inline constexpr char kAddNewProfileOnDesktopLearnMoreURL[] =
    "";

// The URL for the "Learn more" page for AI settings for managed users.
inline constexpr char16_t kAiSettingsLearnMorePageManagedUrl[] =
    u"";

// The URL for the "Learn more" page for Help me Write.
inline constexpr char kComposeLearnMorePageURL[] =
    "";

// The URL for the "Learn more" page for Help me Write for managed users.
inline constexpr char kComposeLearnMorePageManagedURL[] =
    "";

// The URL for the "Learn more" links for pages related to History search.
inline constexpr char kHistorySearchLearnMorePageURL[] =
    "";

// The URL for the "Learn more" links for pages related to History search for
// managed users.
inline constexpr char kHistorySearchLearnMorePageManagedURL[] =
    "";

// The URL for the Settings page to enable history search.
inline constexpr char16_t kHistorySearchSettingURL[] =
    u"chrome://settings/ai/historySearch";

// The URL for the "Learn more" page for Wallpaper Search.
inline constexpr char kWallpaperSearchLearnMorePageURL[] =
    "";

// The URL for the passed in Google Wallet.
inline constexpr char kWalletPassesPageURL[] =
    "https://wallet.google.com/wallet/passes";

// The URL for the "Learn more" page for Tab Organization.
inline constexpr char kTabOrganizationLearnMorePageURL[] =
    "";

// The URL for the "Learn more" page for Tab Organization for managed users.
inline constexpr char kTabOrganizationLearnMorePageManagedURL[] =
    "";

// The URL for the "Learn more" link in the enterprise disclaimer for managed
// profile in the Signin Intercept bubble.
inline constexpr char kSigninInterceptManagedDisclaimerLearnMoreURL[] =
    "";

#if !BUILDFLAG(IS_ANDROID)
// The URL for the trusted vault sync passphrase opt in.
inline constexpr char kSyncTrustedVaultOptInURL[] =
    "https://passwords.google.com/encryption/enroll?"
    "utm_source=chrome&utm_medium=desktop&utm_campaign=encryption_enroll";
#endif

// The URL for the "Learn more" link for the trusted vault sync passphrase.
inline constexpr char kSyncTrustedVaultLearnMoreURL[] =
    "";

// The URL for the Help Center page about User Bypass.
inline constexpr char16_t kUserBypassHelpCenterURL[] =
    u"";

inline constexpr char kUpgradeHelpCenterBaseURL[] =
    ""
    "{8A69D345-D564-463c-AFF1-A69D9E530F96}&error=";

// The URL for the "Learn more" link for nearby share.
inline constexpr char16_t kNearbyShareLearnMoreURL[] =
    u"";

// Help center URL for who the account administrator is.
inline constexpr char16_t kWhoIsMyAdministratorHelpURL[] =
    u"";

// The URL for the "Learn more" link about CWS Enhanced Safe Browsing.
inline constexpr char16_t kCwsEnhancedSafeBrowsingLearnMoreURL[] =
    u"";

// The URL path to Google's Privacy Policy page.
inline constexpr char kPrivacyPolicyURL[] =
    "";

// The URL path to Google's Privacy Policy page for users in China.
inline constexpr char kPrivacyPolicyURLChina[] =
    "https://policies.google.cn/privacy";

// The URL path to Google's Embedded Privacy Policy page.
inline constexpr char kPrivacyPolicyOnlineURLPath[] =
    "";

// The URL path to Google's Embedded Privacy Policy page for users in China.
inline constexpr char kPrivacyPolicyEmbeddedURLPathChina[] =
    "https://policies.google.cn/privacy/embedded";

#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_CHROMEOS)
// "Learn more" URL for the enhanced playback notification dialog.
inline constexpr char kEnhancedPlaybackNotificationLearnMoreURL[] =
#if BUILDFLAG(IS_CHROMEOS)
    "";
#else
    // Keep in sync with
    // chrome/browser/ui/android/strings/android_chrome_strings.grd
    "";
#endif
#endif

#if BUILDFLAG(IS_CHROMEOS)
// Chrome OS default pre-defined custom handlers
inline constexpr char kChromeOSDefaultMailtoHandler[] =
    "https://mail.google.com/mail/?extsrc=mailto&amp;url=%s";
inline constexpr char kChromeOSDefaultWebcalHandler[] =
    "https://www.google.com/calendar/render?cid=%s";

// The URL for the "Account recovery" page.
inline constexpr char kAccountRecoveryURL[] =
    "https://accounts.google.com/signin/recovery";

// The URL for the "How to add a new user account on a Chromebook" page.
inline constexpr char16_t kAddNewUserURL[] =
    u"https://www.google.com/chromebook/howto/add-another-account";

// Help center URL for ARC ADB sideloading.
inline constexpr char16_t kArcAdbSideloadingLearnMoreURL[] =
    u"";

// The path format to the localized offline ARC++ Privacy Policy.
// Relative to |kChromeOSAssetPath|.
inline constexpr char kArcPrivacyPolicyPathFormat[] =
    "arc_tos/%s/privacy_policy.pdf";

// The path format to the localized offline ARC++ Terms of Service.
// Relative to |kChromeOSAssetPath|.
inline constexpr char kArcTermsPathFormat[] = "arc_tos/%s/terms.html";

// Source for chrome://os-credits. On some devices, this will be compressed.
// Check both.
inline constexpr char kChromeOSCreditsPath[] =
    "/opt/google/chrome/resources/about_os_credits.html";

inline constexpr char kChromeOSCreditsCompressedPath[] =
    "/opt/google/chrome/resources/about_os_credits.html.gz";

// The URL for the help center article about redeeming Chromebook offers.
inline constexpr char kEchoLearnMoreURL[] =
    "chrome://help-app/help/sub/3399709/id/2703646";

// The URL for the Learn More page about enterprise enrolled devices.
inline constexpr char kLearnMoreEnterpriseURL[] =
    "";

// The URL path to offline OEM EULA.
inline constexpr char kOemEulaURLPath[] = "oem";

inline constexpr char kOrcaSuggestionLearnMoreURL[] =
    "";

// Help URL for the OS settings page's search feature.
inline constexpr char kOsSettingsSearchHelpURL[] =
    "";

// The URL path to offline ARC++ Terms of Service.
inline constexpr char kArcTermsURLPath[] = "arc/terms";

// The URL path to offline ARC++ Privacy Policy.
inline constexpr char kArcPrivacyPolicyURLPath[] = "arc/privacy_policy";

// The URL for contacts management in Nearby Share feature.
inline constexpr char16_t kNearbyShareManageContactsURL[] =
    u"https://contacts.google.com";

#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_MAC)
// "Learn more" URL for the enterprise sign-in confirmation dialog.
inline constexpr char kChromeEnterpriseSignInLearnMoreURL[] =
    "";

// The URL for the "learn more" link on the macOS version obsolescence infobar.
inline constexpr char kMacOsObsoleteURL[] =
    "";
#endif

#if BUILDFLAG(IS_WIN)
// The URL for the Windows XP/Vista deprecation help center article.
inline constexpr char kWindowsXPVistaDeprecationURL[] =
    "https://chrome.blogspot.com/2015/11/"
    "updates-to-chrome-platform-support.html";

// The URL for the Windows 7/8.1 deprecation help center article.
inline constexpr char kWindows78DeprecationURL[] =
    "";
#endif

#if BUILDFLAG(ENABLE_PLUGINS)
// The URL for the "Learn more" page for the outdated plugin infobar.
inline constexpr char kOutdatedPluginLearnMoreURL[] =
    "";
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
// "Learn more" URL for the chrome apps deprecation dialog.
inline constexpr char kChromeAppsDeprecationLearnMoreURL[] =
    "";
#endif

#if BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)
inline constexpr char kChromeRootStoreSettingsHelpCenterURL[] =
    "";
#endif

#if BUILDFLAG(ENABLE_PDF_SAVE_TO_DRIVE)
// "Learn more" URL for how to save PDF to Google Drive from the PDF viewer.
inline constexpr char kPdfViewerSaveToDriveHelpCenterURL[] =
    "";
#endif

// Please do not append entries here. See the comments at the top of the file.

}  // namespace chrome

#endif  // CHROME_COMMON_URL_CONSTANTS_H_
