// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_USER_DATA_IMPORTER_COMMON_IMPORTER_VISIT_ROW_H_
#define COMPONENTS_USER_DATA_IMPORTER_COMMON_IMPORTER_VISIT_ROW_H_

#include <string>

#include "base/time/time.h"
#include "url/gurl.h"

namespace user_data_importer {

// Used as the target for importing granular history visits (for RAG pipelines)
// in the utility process. Converted to history::VisitRow and history::URLRow
// after being passed via IPC to the browser.
struct ImporterVisitRow {
 public:
  ImporterVisitRow();
  explicit ImporterVisitRow(const GURL& url);
  ImporterVisitRow(const ImporterVisitRow&);
  ImporterVisitRow& operator=(const ImporterVisitRow&);
  ~ImporterVisitRow();

  // URL data
  GURL url;
  std::u16string title;
  int visit_count = 0;
  int typed_count = 0;
  bool hidden = false;

  // Granular visit data
  int64_t source_visit_id = 0;
  int64_t source_from_visit_id = 0;
  base::Time visit_time;
  base::TimeDelta visit_duration;
  int transition = 0;
  std::string external_referrer_url;
};

}  // namespace user_data_importer

#endif  // COMPONENTS_USER_DATA_IMPORTER_COMMON_IMPORTER_VISIT_ROW_H_