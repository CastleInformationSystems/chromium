#ifndef JATTER_TOKEN_STORAGE_H_
#define JATTER_TOKEN_STORAGE_H_

#include "base/supports_user_data.h"
#include "chrome/browser/profiles/profile.h"

class JatterTokenStorage : public base::SupportsUserData::Data {
 public:
  static JatterTokenStorage* GetOrCreate(Profile* profile);

  void SetTokens(const std::string& id_token, const std::string& refresh_token);
  std::string GetIdToken() const;
  std::string GetRefreshToken() const;

 private:
  explicit JatterTokenStorage(Profile* profile);

  void LoadFromPrefs();
  void SaveToPrefs();

  raw_ptr<Profile> profile_;
  std::string id_token_;
  std::string refresh_token_;
};

#endif  // JATTER_TOKEN_STORAGE_H_
