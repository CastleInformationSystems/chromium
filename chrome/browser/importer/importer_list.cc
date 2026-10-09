// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/importer/importer_list.h"

#include <stdint.h>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/common/importer/firefox_importer_utils.h"
#include "chrome/common/importer/importer_bridge.h"
#include "chrome/grit/generated_resources.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "ui/base/l10n/l10n_util.h"

#if BUILDFLAG(IS_MAC)
#include "base/apple/foundation_util.h"
#include "chrome/common/importer/safari_importer_utils.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "chrome/common/importer/edge_importer_utils_win.h"
#endif

namespace {

enum class DataLocation {
  kLocalAppdata,
  kRoamingAppdata,
};

struct DerivativeConfig {
  const char16_t* brand_name;
  const char* relative_path;
  DataLocation location;
  bool is_direct_profile;  // True if 'History' is directly in root (e.g. Opera)
};

const DerivativeConfig kDerivatives[] = {
#if BUILDFLAG(IS_WIN)
    {u"Google Chrome", "Google\\Chrome\\User Data", DataLocation::kLocalAppdata, false},
    {u"Google Chrome Beta", "Google\\Chrome Beta\\User Data", DataLocation::kLocalAppdata, false},
    {u"Microsoft Edge", "Microsoft\\Edge\\User Data", DataLocation::kLocalAppdata, false},
    {u"Brave", "BraveSoftware\\Brave-Browser\\User Data", DataLocation::kLocalAppdata, false},
    {u"Vivaldi", "Vivaldi\\User Data", DataLocation::kLocalAppdata, false},
    {u"Arc", "The Browser Company\\Arc\\User Data", DataLocation::kLocalAppdata, false},
    {u"Yandex Browser", "Yandex\\YandexBrowser\\User Data", DataLocation::kLocalAppdata, false},
    {u"Opera", "Opera Software\\Opera Stable", DataLocation::kRoamingAppdata, true},
    {u"Opera GX", "Opera Software\\Opera GX Stable", DataLocation::kRoamingAppdata, true},
#elif BUILDFLAG(IS_MAC)
    {u"Google Chrome", "Google/Chrome", DataLocation::kLocalAppdata, false},
    {u"Google Chrome Beta", "Google/Chrome Beta", DataLocation::kLocalAppdata, false},
    {u"Microsoft Edge", "Microsoft Edge", DataLocation::kLocalAppdata, false},
    {u"Brave", "BraveSoftware/Brave-Browser", DataLocation::kLocalAppdata, false},
    {u"Vivaldi", "Vivaldi", DataLocation::kLocalAppdata, false},
    {u"Arc", "Arc/User Data", DataLocation::kLocalAppdata, false},
    {u"Yandex Browser", "Yandex/YandexBrowser", DataLocation::kLocalAppdata, false},
    {u"Opera", "com.operasoftware.Opera", DataLocation::kLocalAppdata, true},
    {u"Opera GX", "com.operasoftware.OperaGX", DataLocation::kLocalAppdata, true},
#endif
};

base::FilePath ResolveBaseDataDir(DataLocation location) {
  base::FilePath base_path;
#if BUILDFLAG(IS_WIN)
  if (location == DataLocation::kRoamingAppdata) {
    base::PathService::Get(base::DIR_ROAMING_APP_DATA, &base_path);
  } else {
    base::PathService::Get(base::DIR_LOCAL_APP_DATA, &base_path);
  }
#elif BUILDFLAG(IS_MAC)
  base_path = base::apple::GetUserLibraryPath().AppendASCII("Application Support");
#endif
  return base_path;
}

void EnumerateChromiumProfiles(
    const DerivativeConfig& config,
    const base::FilePath& root_path,
    std::vector<user_data_importer::SourceProfile>* profiles) {
  // 1. Direct profile directory (Opera / Opera GX)
  if (config.is_direct_profile) {
    if (base::PathExists(root_path.AppendASCII("History"))) {
      user_data_importer::SourceProfile profile;
      profile.importer_name = config.brand_name;
      profile.importer_type = user_data_importer::TYPE_CHROME;
      profile.source_path = root_path;
      profile.app_path = root_path;
      profile.services_supported = user_data_importer::HISTORY |
                                   user_data_importer::FAVORITES;
      profiles->push_back(profile);
    }
    return;
  }

  // 2. Multi-profile resolution via Local State
  base::FilePath local_state_path = root_path.AppendASCII("Local State");
  std::string file_content;
  if (!base::ReadFileToString(local_state_path, &file_content)) {
    // Fallback if Local State is missing: check for default directory
    base::FilePath default_path = root_path.AppendASCII("Default");
    if (base::PathExists(default_path.AppendASCII("History"))) {
      user_data_importer::SourceProfile profile;
      profile.importer_name = config.brand_name;
      profile.importer_type = user_data_importer::TYPE_CHROME;
      profile.source_path = default_path;
      profile.app_path = default_path;
      profile.services_supported = user_data_importer::HISTORY |
                                   user_data_importer::FAVORITES;
      profiles->push_back(profile);
    }
    return;
  }

  // Pass base::JSON_PARSE_RFC (or 0) for the required 'int options' parameter
  std::optional<base::Value> json =
      base::JSONReader::Read(file_content, base::JSON_PARSE_RFC);
  if (!json || !json->is_dict()) {
    return;
  }

  // Use base::DictValue* (or const auto*) instead of base::Value::Dict*
  const base::DictValue* info_cache =
      json->GetDict().FindDictByDottedPath("profile.info_cache");
  if (!info_cache || info_cache->empty()) {
    base::FilePath default_path = root_path.AppendASCII("Default");
    if (base::PathExists(default_path.AppendASCII("History"))) {
      user_data_importer::SourceProfile profile;
      profile.importer_name = config.brand_name;
      profile.importer_type = user_data_importer::TYPE_CHROME;
      profile.source_path = default_path;
      profile.app_path = default_path;
      profile.services_supported = user_data_importer::HISTORY |
                                   user_data_importer::FAVORITES;
      profiles->push_back(profile);
    }
    return;
  }

  // Capture by reference (&) to avoid copying move-only base::Value objects
  for (auto [profile_dir_name, profile_value] : *info_cache) {
    base::FilePath profile_path = root_path.AppendASCII(profile_dir_name);
    if (!base::PathExists(profile_path.AppendASCII("History"))) {
      continue;
    }

    std::u16string display_name = config.brand_name;
    if (profile_value.is_dict()) {
      const std::string* custom_name =
          profile_value.GetDict().FindString("name");
      if (custom_name && !custom_name->empty()) {
        display_name += u" (" + base::UTF8ToUTF16(*custom_name) + u")";
      } else if (info_cache->size() > 1) {
        display_name += u" (" + base::UTF8ToUTF16(profile_dir_name) + u")";
      }
    }

    user_data_importer::SourceProfile profile;
    profile.importer_name = display_name;
    profile.importer_type = user_data_importer::TYPE_CHROME;
    profile.source_path = profile_path;
    profile.app_path = profile_path;
    profile.services_supported = user_data_importer::HISTORY |
                                 user_data_importer::FAVORITES;
    profiles->push_back(profile);
  }
}

void DetectChromiumProfiles(
    std::vector<user_data_importer::SourceProfile>* profiles) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);

  for (const auto& derivative : kDerivatives) {
    base::FilePath base_dir = ResolveBaseDataDir(derivative.location);
    if (base_dir.empty()) {
      continue;
    }

    base::FilePath candidate_path = base_dir.AppendASCII(derivative.relative_path);
    if (base::DirectoryExists(candidate_path)) {
      EnumerateChromiumProfiles(derivative, candidate_path, profiles);
    }
  }
}

#if BUILDFLAG(IS_WIN)
void DetectIEProfiles(
    std::vector<user_data_importer::SourceProfile>* profiles) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);

  // IE always exists and doesn't have multiple profiles.
  user_data_importer::SourceProfile ie;
  ie.importer_name = l10n_util::GetStringUTF16(IDS_IMPORT_FROM_IE);
  ie.importer_type = user_data_importer::TYPE_IE;
  ie.services_supported = user_data_importer::HISTORY |
                          user_data_importer::FAVORITES |
                          user_data_importer::SEARCH_ENGINES;
  profiles->push_back(ie);
}

// void DetectEdgeProfiles(
//     std::vector<user_data_importer::SourceProfile>* profiles) {
//   if (!importer::EdgeImporterCanImport()) {
//     return;
//   }
//   user_data_importer::SourceProfile edge;
//   edge.importer_name = l10n_util::GetStringUTF16(IDS_IMPORT_FROM_EDGE);
//   edge.importer_type = user_data_importer::TYPE_EDGE;
//   edge.services_supported = user_data_importer::FAVORITES;
//   edge.source_path = importer::GetEdgeDataFilePath();
//   profiles->push_back(edge);
// }

void DetectBuiltinWindowsProfiles(
    std::vector<user_data_importer::SourceProfile>* profiles) {
  // if (shell_integration::IsIEDefaultBrowser()) {
  //   DetectIEProfiles(profiles);
  //   DetectEdgeProfiles(profiles);
  // } else {
  //   DetectEdgeProfiles(profiles);
    DetectIEProfiles(profiles);
  // }
}

#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_MAC)
void DetectSafariProfiles(
    std::vector<user_data_importer::SourceProfile>* profiles) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);

  uint16_t items = user_data_importer::NONE;
  if (!SafariImporterCanImport(base::apple::GetUserLibraryPath(), &items)) {
    return;
  }

  user_data_importer::SourceProfile safari;
  safari.importer_name = l10n_util::GetStringUTF16(IDS_IMPORT_FROM_SAFARI);
  safari.importer_type = user_data_importer::TYPE_SAFARI;
  safari.services_supported = items;
  profiles->push_back(safari);
}
#endif  // BUILDFLAG(IS_MAC)

// |locale|: The application locale used for lookups in Firefox's
// locale-specific search engines feature (see firefox_importer.cc for
// details).
void DetectFirefoxProfiles(
    const std::string locale,
    std::vector<user_data_importer::SourceProfile>* profiles) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
#if BUILDFLAG(IS_WIN)
  const std::string firefox_install_id =
      shell_integration::GetFirefoxProgIdSuffix();
#else
  const std::string firefox_install_id;
#endif  // BUILDFLAG(IS_WIN)
  std::vector<FirefoxDetail> details = GetFirefoxDetails(firefox_install_id);
  if (details.empty())
    return;

  for (const auto& detail : details) {
    base::FilePath app_path;
    if (detail.path.empty())
      continue;

    int version = 0;
#if BUILDFLAG(IS_WIN)
    version = GetCurrentFirefoxMajorVersionFromRegistry();
#endif

    if (version < 2) {
      GetFirefoxVersionAndPathFromProfile(detail.path, &version, &app_path);
      // Note that |version| is re-assigned above.
      if (version < 48) {
        // While this cutoff (and the removal of outdated code) should depend on
        // the usage percent of different versions of Firefox (see
        // https://en.wikipedia.org/wiki/Firefox_version_history), the reality
        // is that the current cutoff of at least Firefox 48 is mostly due to
        // the fact that there's a Firefox 48 profile for testing in
        // firefox_importer_unittest.cc. TODO(crbug.com/40169760): Add
        // more modern Firefox test profiles, and roll the cutoff version.
        continue;
      }
    }

    user_data_importer::SourceProfile firefox;
    firefox.importer_name = GetFirefoxImporterName(app_path);
    firefox.profile = detail.name;
    firefox.importer_type = user_data_importer::TYPE_FIREFOX;
    firefox.source_path = detail.path;
#if BUILDFLAG(IS_WIN)
    firefox.app_path = GetFirefoxInstallPathFromRegistry();
#endif
    if (firefox.app_path.empty())
      firefox.app_path = app_path;
    firefox.services_supported = user_data_importer::HISTORY |
                                 user_data_importer::FAVORITES |
                                 user_data_importer::AUTOFILL_FORM_DATA;
#if !BUILDFLAG(IS_MAC)
    // Passwords are imported by loading the NSS DLLs into the Chromium process.
    // Restrictive code signing prevents that from ever working again in modern
    // macOSes, so don't promise an import service that can't be delivered.
    firefox.services_supported |= user_data_importer::PASSWORDS;
#endif
    firefox.locale = locale;
    profiles->push_back(firefox);
  }
}

// void DetectChromeProfiles(std::vector<user_data_importer::SourceProfile>* profiles) {
//   base::FilePath chrome_user_data_dir;

// #if BUILDFLAG(IS_WIN)
//   if (base::PathService::Get(base::DIR_LOCAL_APP_DATA, &chrome_user_data_dir)) {
//     chrome_user_data_dir = chrome_user_data_dir.AppendASCII("Google")
//                                                .AppendASCII("Chrome")
//                                                .AppendASCII("User Data");
//   }
// #elif BUILDFLAG(IS_MAC)
//   // DIR_APP_DATA on macOS resolves to ~/Library/Application Support
//   if (base::PathService::Get(base::DIR_APP_DATA, &chrome_user_data_dir)) {
//     chrome_user_data_dir = chrome_user_data_dir.AppendASCII("Google")
//                                                .AppendASCII("Chrome");
//   }
// #elif BUILDFLAG(IS_LINUX)
//   // Linux installations typically default to ~/.config/google-chrome
//   base::FilePath home_dir;
//   if (base::PathService::Get(base::DIR_HOME, &home_dir)) {
//     chrome_user_data_dir = home_dir.AppendASCII(".config")
//                                    .AppendASCII("google-chrome");
//   }
// #endif

//   // If the directory exists, Chrome is installed and has user data
//   if (!chrome_user_data_dir.empty() && base::PathExists(chrome_user_data_dir)) {
//     user_data_importer::SourceProfile chrome_profile;
//     chrome_profile.importer_name = u"Google Chrome";
//     chrome_profile.importer_type = user_data_importer::TYPE_CHROME;
//     chrome_profile.source_path = chrome_user_data_dir;
//     chrome_profile.app_path = chrome_user_data_dir;
//     // Specify what Jatter can extract from Chrome
//     chrome_profile.services_supported = user_data_importer::HISTORY | 
//                                         user_data_importer::FAVORITES;
    
//     profiles->push_back(chrome_profile);
//   }
// }

std::vector<user_data_importer::SourceProfile> DetectSourceProfilesWorker(
    const std::string& locale,
    bool include_interactive_profiles) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);

  std::vector<user_data_importer::SourceProfile> profiles;

  // The first run import will automatically take settings from the first
  // profile detected, which should be the user's current default.
#if BUILDFLAG(IS_WIN)
  if (shell_integration::IsFirefoxDefaultBrowser()) {
    DetectFirefoxProfiles(locale, &profiles);
    DetectBuiltinWindowsProfiles(&profiles);
  } else {
    DetectBuiltinWindowsProfiles(&profiles);
    DetectFirefoxProfiles(locale, &profiles);
  }
#elif BUILDFLAG(IS_MAC)
  if (shell_integration::IsFirefoxDefaultBrowser()) {
    DetectFirefoxProfiles(locale, &profiles);
    DetectSafariProfiles(&profiles);
  } else {
    DetectSafariProfiles(&profiles);
    DetectFirefoxProfiles(locale, &profiles);
  }
#else
  DetectFirefoxProfiles(locale, &profiles);
#endif

  DetectChromiumProfiles(&profiles);

  if (include_interactive_profiles) {
    user_data_importer::SourceProfile bookmarks_profile;
    bookmarks_profile.importer_name =
        l10n_util::GetStringUTF16(IDS_IMPORT_FROM_BOOKMARKS_HTML_FILE);
    bookmarks_profile.importer_type = user_data_importer::TYPE_BOOKMARKS_FILE;
    bookmarks_profile.services_supported = user_data_importer::FAVORITES;
    profiles.push_back(bookmarks_profile);
  }

  return profiles;
}

}  // namespace

ImporterList::ImporterList() = default;

ImporterList::~ImporterList() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void ImporterList::DetectSourceProfiles(
    const std::string& locale,
    bool include_interactive_profiles,
    base::OnceClosure profiles_loaded_callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&DetectSourceProfilesWorker, locale,
                     include_interactive_profiles),
      base::BindOnce(&ImporterList::SourceProfilesLoaded,
                     weak_ptr_factory_.GetWeakPtr(),
                     std::move(profiles_loaded_callback)));
}

const user_data_importer::SourceProfile& ImporterList::GetSourceProfileAt(
    size_t index) const {
  DCHECK_LT(index, count());
  return source_profiles_[index];
}

void ImporterList::SourceProfilesLoaded(
    base::OnceClosure profiles_loaded_callback,
    const std::vector<user_data_importer::SourceProfile>& profiles) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  source_profiles_.assign(profiles.begin(), profiles.end());
  std::move(profiles_loaded_callback).Run();
}
