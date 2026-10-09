// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/user_data_importer/common/importer_visit_row.h"

namespace user_data_importer {

ImporterVisitRow::ImporterVisitRow()
    : visit_count(0),
      typed_count(0),
      hidden(false),
      source_visit_id(0),
      source_from_visit_id(0),
      transition(0) {}

ImporterVisitRow::ImporterVisitRow(const GURL& url)
    : url(url),
      visit_count(0),
      typed_count(0),
      hidden(false),
      source_visit_id(0),
      source_from_visit_id(0),
      transition(0) {}

ImporterVisitRow::ImporterVisitRow(const ImporterVisitRow&) = default;

ImporterVisitRow& ImporterVisitRow::operator=(const ImporterVisitRow&) = default;

ImporterVisitRow::~ImporterVisitRow() = default;

}  // namespace user_data_importer