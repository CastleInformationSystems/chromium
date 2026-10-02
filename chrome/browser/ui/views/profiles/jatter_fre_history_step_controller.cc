#include "chrome/browser/ui/views/profiles/jatter_fre_history_step_controller.h"

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/importer/external_process_importer_host.h"
#include "chrome/browser/importer/importer_progress_observer.h"
#include "chrome/browser/importer/profile_writer.h"
#include "chrome/browser/jatter/jatter_firebase_client_factory.h"
#include "chrome/browser/jatter/jatter_firebase_client.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/profiles/profile_picker_web_contents_host.h"
#include "chrome/browser/ui/webui/intro/intro_ui.h"
#include "chrome/browser/ui/webui/intro/jatter_history_handler.h"
#include "components/history/core/browser/history_db_task.h"
#include "components/history/core/browser/history_backend.h"
#include "components/history/core/browser/history_database.h"
#include "components/history/core/browser/history_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "url/gurl.h"

namespace {

// Forward declaration so the task can notify the helper when it finishes
class JatterHistoryIngestionHelper;

class JatterBulkIngestionTask : public history::HistoryDBTask {
 public:
  JatterBulkIngestionTask(Profile* profile, JatterHistoryIngestionHelper* helper) 
      : profile_(profile), helper_(helper) {}

  // This runs on the background History DB thread. Do NOT access Profile here.
  bool RunOnDBThread(history::HistoryBackend* backend,
                     history::HistoryDatabase* db) override {
    if (!db) return false;

    // Use Chromium's public abstractions instead of raw SQL
    history::QueryOptions options;
    options.max_count = 5000;
    history::VisitVector visits;
    
    // Grabs the most recent visits, natively ignoring hidden redirects/subframes
    db->GetVisibleVisitsInRange(options, &visits);

    for (const history::VisitRow& visit : visits) {
      history::URLRow url_row;
      // Fetch the accompanying URL info for this visit
      if (!db->GetURLRow(visit.url_id, &url_row)) {
        continue;
      }

      // Convert times safely without using the missing base::Time method
      double visit_time_micros = static_cast<double>(
          (visit.visit_time - base::Time::UnixEpoch()).InMicroseconds());
      double last_visit_time_micros = static_cast<double>(
          (url_row.last_visit() - base::Time::UnixEpoch()).InMicroseconds());
      double visit_duration_micros = static_cast<double>(
          visit.visit_duration.InMicroseconds());

      // Construct JSON payload using the modern value-based API supported by your M147 build
      base::DictValue url_dict;
      url_dict.Set("url", url_row.url().spec());
      url_dict.Set("title", base::UTF16ToUTF8(url_row.title()));
      url_dict.Set("visitCount", url_row.visit_count());
      url_dict.Set("typedCount", url_row.typed_count());
      url_dict.Set("lastVisitTime", last_visit_time_micros);
      url_dict.Set("metadata", base::DictValue());

      base::DictValue visit_dict;
      visit_dict.Set("id", base::NumberToString(visit.visit_id));
      visit_dict.Set("url", std::move(url_dict));
      visit_dict.Set("visitTime", visit_time_micros);

      std::string from_visit_str = (visit.referring_visit != 0) 
          ? base::NumberToString(visit.referring_visit) : "";
      visit_dict.Set("fromVisit", from_visit_str);
      visit_dict.Set("external_referrer_url", visit.external_referrer_url.spec());
      visit_dict.Set("visitDuration", visit_duration_micros);

      visits_list_.Append(std::move(visit_dict));
    }
    return true; // Indicates success
  }

  // This runs automatically on the UI thread when the DB thread finishes
  void DoneRunOnMainThread() override;

 private:
  raw_ptr<Profile> profile_;
  raw_ptr<JatterHistoryIngestionHelper> helper_;
  base::ListValue visits_list_;
};

class JatterHistoryIngestionHelper : public importer::ImporterProgressObserver {
 public:
  JatterHistoryIngestionHelper(Profile* profile) : profile_(profile) {}

  void ImportStarted() override {}
  // Updated to the correct user_data_importer namespace
  void ImportItemStarted(user_data_importer::ImportItem item) override {}
  void ImportItemEnded(user_data_importer::ImportItem item) override {}
  
  void ImportEnded() override {
    history::HistoryService* history_service = HistoryServiceFactory::GetForProfile(
        profile_, ServiceAccessType::EXPLICIT_ACCESS);
    if (!history_service) {
      delete this;
      return;
    }

    // Schedule the custom extraction. 
    // We pass `this` so the task can tell us when it is safe to self-destruct.
    history_service->ScheduleDBTask(
        FROM_HERE,
        std::make_unique<JatterBulkIngestionTask>(profile_, this),
        &task_tracker_);
  }

  // Called by JatterBulkIngestionTask::DoneRunOnMainThread
  void OnTaskComplete() {
    delete this;
  }

 private:
  raw_ptr<Profile> profile_;
  base::CancelableTaskTracker task_tracker_;
};

// Define DoneRunOnMainThread here so it can access JatterHistoryIngestionHelper methods
void JatterBulkIngestionTask::DoneRunOnMainThread() {
  JatterFirebaseClientFactory::GetForProfile(profile_)
    ->LogBulkWebPageVisits(profile_, std::move(visits_list_));
  
  if (helper_) {
    helper_->OnTaskComplete();
  }
}

} // namespace

JatterFreHistoryStepController::JatterFreHistoryStepController(
    ProfilePickerWebContentsHost* host,
    base::OnceClosure step_completed_callback)
    : ProfileManagementStepController(host),
      step_completed_callback_(std::move(step_completed_callback)) {}

JatterFreHistoryStepController::~JatterFreHistoryStepController() = default;

void JatterFreHistoryStepController::Show(StepSwitchFinishedCallback step_shown_callback, bool reset_state) {
  host()->ShowScreenInPickerContents(
      GURL("chrome://intro/history"),
      base::BindOnce(&JatterFreHistoryStepController::OnHistoryPageLoaded,
                     weak_ptr_factory_.GetWeakPtr(),
                     std::move(step_shown_callback)));
}

void JatterFreHistoryStepController::OnHistoryPageLoaded(StepSwitchFinishedCallback step_shown_callback) {
  std::move(step_shown_callback.value()).Run(true);
  content::WebUI* web_ui = host()->GetPickerContents()->GetWebUI();
  if (web_ui) {
    web_ui->AddMessageHandler(std::make_unique<JatterHistoryHandler>(
        base::BindOnce(&JatterFreHistoryStepController::OnHistoryChoice,
                       weak_ptr_factory_.GetWeakPtr())));
  }
}

void JatterFreHistoryStepController::OnHistoryChoice(
    bool should_import, 
    const user_data_importer::SourceProfile& source_profile) {
    
  if (should_import) {
    LOG(INFO) << "[JATTER FRE] Triggering history import for: " 
              << base::UTF16ToUTF8(source_profile.importer_name);

    Profile* target_profile = Profile::FromBrowserContext(
        host()->GetPickerContents()->GetBrowserContext());

    uint16_t items_to_import = user_data_importer::HISTORY;

    if ((source_profile.services_supported & items_to_import) != 0) {
      auto* importer_host = new ExternalProcessImporterHost();
      importer_host->set_observer(new JatterHistoryIngestionHelper(target_profile));
      
      importer_host->StartImportSettings(
          source_profile,
          target_profile,
          items_to_import,
          new ProfileWriter(target_profile));
    } else {
      LOG(WARNING) << "[JATTER FRE] Selected browser does not support history import.";
    }
  }

  if (step_completed_callback_) {
    std::move(step_completed_callback_).Run();
  }
}

void JatterFreHistoryStepController::OnNavigateBackRequested() {
  NavigateBackInternal(host()->GetPickerContents());
}
