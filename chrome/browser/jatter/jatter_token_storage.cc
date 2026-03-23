#include "chrome/browser/jatter/jatter_token_storage.h"

#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"

namespace {

const char kUserDataKey[] = "JatterTokenStorage";

}  // namespace

JatterTokenStorage* JatterTokenStorage::GetOrCreate(Profile* profile) {
  JatterTokenStorage* storage =
      static_cast<JatterTokenStorage*>(profile->GetUserData(kUserDataKey));
  if (!storage) {
    storage = new JatterTokenStorage(profile);
    profile->SetUserData(kUserDataKey, base::WrapUnique(storage));
  }
  return storage;
}

JatterTokenStorage::JatterTokenStorage(Profile* profile) : profile_(profile) {
  LoadFromPrefs();
}

void JatterTokenStorage::SetTokens(const std::string& id_token,
                                   const std::string& refresh_token) {
  id_token_ = id_token;
  refresh_token_ = refresh_token;
  SaveToPrefs();
}

std::string JatterTokenStorage::GetIdToken() const {
  return id_token_;
}

std::string JatterTokenStorage::GetRefreshToken() const {
  return refresh_token_;
}

void JatterTokenStorage::LoadFromPrefs() {
  const base::Value::Dict& dict =
      profile_->GetPrefs()->GetDict(prefs::kJatterAuthenticationToken);
  const std::string* id_token = dict.FindString("id_token");
  const std::string* refresh_token = dict.FindString("refresh_token");
  if (id_token) {
    id_token_ = *id_token;
  }
  if (refresh_token) {
    refresh_token_ = *refresh_token;
  }
}

void JatterTokenStorage::SaveToPrefs() {
  base::Value::Dict dict;
  dict.Set("id_token", id_token_);
  dict.Set("refresh_token", refresh_token_);
  profile_->GetPrefs()->SetDict(prefs::kJatterAuthenticationToken,
                                std::move(dict));
}
