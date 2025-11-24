#ifndef JATTER_HISTORY_OBSERVER_H_
#define JATTER_HISTORY_OBSERVER_H_

#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_service_observer.h"
#include "components/keyed_service/core/keyed_service.h"

class Profile;

class JatterHistoryObserver : public KeyedService,
                              public history::HistoryServiceObserver {
 public:
  JatterHistoryObserver(Profile* profile);
  ~JatterHistoryObserver() override;

  void Shutdown() override;

  void OnURLVisited(history::HistoryService* history_service,
                    const history::URLRow& url_row,
                    const history::VisitRow& new_visit) override;

 private:
  raw_ptr<Profile> profile_;
  raw_ptr<history::HistoryService> history_service_;
};

#endif
