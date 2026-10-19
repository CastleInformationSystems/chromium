#include "chrome/browser/jatter/jatter_token_storage.h"

#include "base/base64.h"
#include "base/base64url.h"
#include "base/json/json_reader.h"
#include "base/strings/string_split.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/os_crypt/sync/os_crypt.h"
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

JatterTokenStorage::~JatterTokenStorage() = default;

std::string JatterTokenStorage::ExtractUidFromToken(const std::string& id_token) const {
  std::vector<std::string> parts =
      base::SplitString(id_token, ".", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (parts.size() < 2) {
    return "";
  }

  std::string payload;
  if (base::Base64UrlDecode(parts[1], base::Base64UrlDecodePolicy::IGNORE_PADDING, &payload)) {
    std::optional<base::Value> json =
        base::JSONReader::Read(payload, base::JSON_PARSE_RFC);
    if (json && json->is_dict()) {
      if (const std::string* sub = json->GetDict().FindString("user_id")) {
        return *sub;
      }
    }
  }
  return "";
}

void JatterTokenStorage::SetTokens(const std::string& id_token,
                                   const std::string& refresh_token) {
  id_token_ = id_token;
  refresh_token_ = refresh_token;

  std::string new_uid = ExtractUidFromToken(id_token_);

  if (!new_uid.empty()) {
    // If we had keys tied to an actual prior user, and the new token is for someone else:
    if (!key_owner_uid_.empty() && key_owner_uid_ != new_uid) {
      private_key_ = std::nullopt;
      signature_ = std::nullopt;
      key_owner_uid_.clear();
      SaveEncryptionPairToPrefs();
    } else {
      // Bind the incoming/current keys to this newly authenticated UID
      key_owner_uid_ = new_uid;
      SaveEncryptionPairToPrefs();
    }
    current_uid_ = new_uid;
  }

  SaveToPrefs();
}

void JatterTokenStorage::SetPrivateKey(const std::string& private_key) {
  private_key_ = private_key;
  signature_ = std::nullopt;  // Incomplete until signature arrives (R2.2)
  // Bind to current_uid_ if known; otherwise SetTokens will bind it upon token arrival
  key_owner_uid_ = current_uid_;
  SaveEncryptionPairToPrefs();
}

void JatterTokenStorage::SetPrivateKeySignature(const std::string& signature) {
  signature_ = signature;
  SaveEncryptionPairToPrefs();
}

std::string JatterTokenStorage::GetIdToken() const { return id_token_; }
std::string JatterTokenStorage::GetRefreshToken() const { return refresh_token_; }

std::optional<std::string> JatterTokenStorage::GetPrivateKey() const {
  if (private_key_ && signature_) return private_key_;
  return std::nullopt; // Incomplete
}

std::optional<std::string> JatterTokenStorage::GetPrivateKeySignature() const {
  if (private_key_ && signature_) return signature_;
  return std::nullopt; // Incomplete
}

void JatterTokenStorage::LoadFromPrefs() {
  const base::DictValue& dict = profile_->GetPrefs()->GetDict(prefs::kJatterAuthenticationToken);
  
  if (const std::string* id_token = dict.FindString("id_token")) {
    id_token_ = *id_token;
    current_uid_ = ExtractUidFromToken(id_token_);
  }
  if (const std::string* refresh_token = dict.FindString("refresh_token")) {
    refresh_token_ = *refresh_token;
  }

  // Load OSCrypt Encrypted Key Pair
  if (const std::string* b64_enc_key = dict.FindString("enc_private_key")) {
    std::string enc_key, dec_key;
    if (base::Base64Decode(*b64_enc_key, &enc_key) && OSCrypt::DecryptString(enc_key, &dec_key)) {
      private_key_ = dec_key;
    }
  }
  if (const std::string* b64_enc_sig = dict.FindString("enc_signature")) {
    std::string enc_sig, dec_sig;
    if (base::Base64Decode(*b64_enc_sig, &enc_sig) && OSCrypt::DecryptString(enc_sig, &dec_sig)) {
      signature_ = dec_sig;
    }
  }

  if (const std::string* owner = dict.FindString("key_owner_uid")) {
    key_owner_uid_ = *owner;
  }
}

void JatterTokenStorage::SaveToPrefs() {
  base::DictValue dict = profile_->GetPrefs()->GetDict(prefs::kJatterAuthenticationToken).Clone();
  dict.Set("id_token", id_token_);
  dict.Set("refresh_token", refresh_token_);
  profile_->GetPrefs()->SetDict(prefs::kJatterAuthenticationToken, std::move(dict));
}

void JatterTokenStorage::SaveEncryptionPairToPrefs() {
  base::DictValue dict = profile_->GetPrefs()->GetDict(prefs::kJatterAuthenticationToken).Clone();
  
  if (private_key_) {
    std::string enc_key;
    if (OSCrypt::EncryptString(*private_key_, &enc_key)) {
      dict.Set("enc_private_key", base::Base64Encode(enc_key));
    }
  } else {
    dict.Remove("enc_private_key");
  }

  if (signature_) {
    std::string enc_sig;
    if (OSCrypt::EncryptString(*signature_, &enc_sig)) {
      dict.Set("enc_signature", base::Base64Encode(enc_sig));
    }
  } else {
    dict.Remove("enc_signature");
  }

  if (!key_owner_uid_.empty() && private_key_) {
    dict.Set("key_owner_uid", key_owner_uid_);
  } else {
    dict.Remove("key_owner_uid");
  }

  profile_->GetPrefs()->SetDict(prefs::kJatterAuthenticationToken, std::move(dict));
}
