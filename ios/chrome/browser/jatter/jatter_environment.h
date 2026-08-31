#ifndef JATTER_ENVIRONMENT_H_
#define JATTER_ENVIRONMENT_H_

namespace jatter {

#if defined(JATTER_DEVELOPMENT_MODE)
  constexpr char kFirebaseApiKey[] = "";
  constexpr char kLogWebPageVisitUrl[] = "https://us-central1-beacon-development-46c50.cloudfunctions.net/logWebPageVisitV2";
  constexpr char kBaseApiUrl[] = "https://us-central1-beacon-development-46c50.cloudfunctions.net/";
  
  constexpr char kAppUrl[] = "https://beacon-development-46c50.firebaseapp.com/";
  constexpr char kAppDomain[] = "firebaseapp.com"; 
  constexpr char kAppHost[] = "beacon-development-46c50.firebaseapp.com";
  constexpr char kAppUpgradeUrl[] = "https://beacon-development-46c50.firebaseapp.com/upgrade";
  constexpr char kAppPromptUrl[] = "https://beacon-development-46c50.firebaseapp.com/prompt?q=";

#elif defined(JATTER_STAGING_MODE)
  constexpr char kFirebaseApiKey[] = "";
  constexpr char kLogWebPageVisitUrl[] = "https://us-central1-beacon-staging-df5f2.cloudfunctions.net/logWebPageVisitV2";
  constexpr char kBaseApiUrl[] = "https://us-central1-beacon-staging-df5f2.cloudfunctions.net/";
  
  constexpr char kAppUrl[] = "https://beacon-staging-df5f2.firebaseapp.com/";
  constexpr char kAppDomain[] = "firebaseapp.com"; 
  constexpr char kAppHost[] = "beacon-staging-df5f2.firebaseapp.com";
  constexpr char kAppUpgradeUrl[] = "https://beacon-staging-df5f2.firebaseapp.com/upgrade";
  constexpr char kAppPromptUrl[] = "https://beacon-staging-df5f2.firebaseapp.com/prompt?q=";

#else  // JATTER_PRODUCTION_MODE
  constexpr char kFirebaseApiKey[] = "";
  constexpr char kLogWebPageVisitUrl[] = "https://us-central1-beacon-dd0a1.cloudfunctions.net/logWebPageVisitV2";
  constexpr char kBaseApiUrl[] = "https://us-central1-beacon-dd0a1.cloudfunctions.net/";
  
  constexpr char kAppUrl[] = "https://chat.jatter.ai/";
  constexpr char kAppDomain[] = "jatter.ai"; 
  constexpr char kAppHost[] = "chat.jatter.ai";
  constexpr char kAppUpgradeUrl[] = "https://chat.jatter.ai/upgrade";
  constexpr char kAppPromptUrl[] = "https://chat.jatter.ai/prompt?q=";
#endif

}  // namespace jatter

#endif  // JATTER_ENVIRONMENT_H_
