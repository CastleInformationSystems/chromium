#ifndef IOS_CHROME_BROWSER_JATTER_JATTER_TOKEN_STORAGE_H_
#define IOS_CHROME_BROWSER_JATTER_JATTER_TOKEN_STORAGE_H_

#include <string>
#include "base/memory/raw_ptr.h"
#include "base/supports_user_data.h"

class ProfileIOS;

class JatterTokenStorage : public base::SupportsUserData::Data {
 public:
  static JatterTokenStorage* GetOrCreate(ProfileIOS* profile);

  void SetTokens(const std::string& id_token, const std::string& refresh_token);
  std::string GetIdToken() const;
  std::string GetRefreshToken() const;

 private:
  explicit JatterTokenStorage(ProfileIOS* profile);

  void LoadFromPrefs();
  void SaveToPrefs();

  raw_ptr<ProfileIOS> profile_;
  std::string id_token_;
  std::string refresh_token_;
};

#endif  // IOS_CHROME_BROWSER_JATTER_JATTER_TOKEN_STORAGE_H_