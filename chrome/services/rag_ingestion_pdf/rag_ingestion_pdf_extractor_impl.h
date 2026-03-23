// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_SERVICES_RAG_INGESTION_PDF_RAG_INGESTION_PDF_EXTRACTOR_IMPL_H_
#define CHROME_SERVICES_RAG_INGESTION_PDF_RAG_INGESTION_PDF_EXTRACTOR_IMPL_H_

#include "chrome/services/rag_ingestion_pdf/public/mojom/rag_ingestion_pdf.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"

namespace rag_ingestion {

class PdfTextExtractorImpl : public mojom::PdfTextExtractor {
 public:
  explicit PdfTextExtractorImpl(mojo::PendingReceiver<mojom::PdfTextExtractor> receiver);
  ~PdfTextExtractorImpl() override;
  PdfTextExtractorImpl(const PdfTextExtractorImpl&) = delete;
  PdfTextExtractorImpl& operator=(const PdfTextExtractorImpl&) = delete;

  // mojom::PdfTextExtractor:
  void ExtractText(const std::vector<uint8_t>& pdf_bytes,
                   ExtractTextCallback callback) override;

 private:
  mojo::Receiver<mojom::PdfTextExtractor> receiver_;
};

}  // namespace rag_ingestion

#endif  // CHROME_SERVICES_RAG_INGESTION_PDF_RAG_INGESTION_PDF_EXTRACTOR_IMPL_H_