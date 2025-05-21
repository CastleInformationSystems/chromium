#include "chrome/browser/jatter/jatter_history_observer.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/jatter/jatter_firebase_client.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/core/service_access_type.h"

JatterHistoryObserver::JatterHistoryObserver(Profile* profile) {
  profile_ = profile;

  history_service_ = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);

  if (history_service_) {
    history_service_->AddObserver(this);
  }
}

JatterHistoryObserver::~JatterHistoryObserver() {
  Shutdown();
}

void JatterHistoryObserver::Shutdown() {
  if (history_service_) {
    history_service_->RemoveObserver(this);
    history_service_ = nullptr;
  }
}

void JatterHistoryObserver::OnURLVisited(
    history::HistoryService* history_service,
    const history::URLRow& url_row,
    const history::VisitRow& new_visit) {
  LOG(INFO) << "Visited URL: " << url_row.url();

  JatterFirebaseClient::GetInstance()->ObservePageVisit(
      profile_, url_row.url().spec(), base::UTF16ToUTF8(url_row.title()));
}
