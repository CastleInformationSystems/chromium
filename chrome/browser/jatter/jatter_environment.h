#ifndef JATTER_ENVIRONMENT_H_
#define JATTER_ENVIRONMENT_H_

#if defined(JATTER_DEVELOPMENT_MODE)
const char kFirebaseApiKey[] = "AIzaSyDMEcoBQaYnTWMA_hGmkIK3hpr0NB9Zqf8";
const char kLogWebPageVisitUrl[] =
    "https://us-central1-beacon-development-46c50.cloudfunctions.net/"
    "logWebPageVisit";
#elif defined(JATTER_STAGING_MODE)
const char kFirebaseApiKey[] = "AIzaSyCiWZQCdKx8nTM20h4Leq6_2FC2ekbsvoQ";
const char kLogWebPageVisitUrl[] =
    "https://us-central1-beacon-staging-df5f2.cloudfunctions.net/"
    "logWebPageVisit";
#else  // JATTER_PRODUCTION_MODE
#endif

#endif  // JATTER_ENVIRONMENT_H_
