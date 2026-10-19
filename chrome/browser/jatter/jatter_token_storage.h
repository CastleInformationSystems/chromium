#ifndef JATTER_TOKEN_STORAGE_H_
#define JATTER_TOKEN_STORAGE_H_

#include <string>
#include <optional>
#include "base/supports_user_data.h"
#include "chrome/browser/profiles/profile.h"

class JatterTokenStorage : public base::SupportsUserData::Data {
 public:
  static JatterTokenStorage* GetOrCreate(Profile* profile);

  ~JatterTokenStorage() override;

  void SetTokens(const std::string& id_token, const std::string& refresh_token);
  void SetPrivateKey(const std::string& private_key);
  void SetPrivateKeySignature(const std::string& signature);

  std::string GetIdToken() const;
  std::string GetRefreshToken() const;
  
  // Returns the key/signature ONLY if both are present and valid for the current user.
  std::optional<std::string> GetPrivateKey() const;
  std::optional<std::string> GetPrivateKeySignature() const;

 private:
  explicit JatterTokenStorage(Profile* profile);

  void LoadFromPrefs();
  void SaveToPrefs();
  void SaveEncryptionPairToPrefs();
  std::string ExtractUidFromToken(const std::string& id_token) const;

  raw_ptr<Profile> profile_;
  std::string id_token_;
  std::string refresh_token_;
  std::string current_uid_;
  std::string key_owner_uid_;

  std::optional<std::string> private_key_;
  std::optional<std::string> signature_;
};

#endif  // JATTER_TOKEN_STORAGE_H_
