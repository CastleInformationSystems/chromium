// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/services/rag_ingestion_pdf/rag_ingestion_pdf_extractor_impl.h"

#include "base/strings/utf_string_conversions.h"
#include "third_party/pdfium/public/fpdf_text.h"
#include "third_party/pdfium/public/fpdfview.h"

namespace rag_ingestion {

PdfTextExtractorImpl::PdfTextExtractorImpl(
    mojo::PendingReceiver<mojom::PdfTextExtractor> receiver)
    : receiver_(this, std::move(receiver)) {}

PdfTextExtractorImpl::~PdfTextExtractorImpl() = default;

void PdfTextExtractorImpl::ExtractText(const std::vector<uint8_t>& pdf_bytes,
                                       ExtractTextCallback callback) {
  if (pdf_bytes.empty()) {
    std::move(callback).Run("");
    return;
  }

  // 1. Initialize PDFium
  FPDF_LIBRARY_CONFIG config;
  config.version = 2;
  config.m_pUserFontPaths = nullptr;
  config.m_pIsolate = nullptr;
  config.m_v8EmbedderSlot = 0;
  FPDF_InitLibraryWithConfig(&config);

  // 2. Load the document from memory
  FPDF_DOCUMENT doc = FPDF_LoadMemDocument(pdf_bytes.data(), pdf_bytes.size(), nullptr);
  if (!doc) {
    FPDF_DestroyLibrary();
    std::move(callback).Run("");
    return;
  }

  std::u16string full_text;
  int page_count = FPDF_GetPageCount(doc);

  // 3. Extract text page by page
  for (int i = 0; i < page_count; i++) {
    FPDF_PAGE page = FPDF_LoadPage(doc, i);
    if (!page) continue;

    FPDF_TEXTPAGE text_page = FPDFText_LoadPage(page);
    if (text_page) {
      int chars_count = FPDFText_CountChars(text_page);
      if (chars_count > 0) {
        std::vector<unsigned short> buffer(chars_count + 1);
        FPDFText_GetText(text_page, 0, chars_count, buffer.data());
        full_text += std::u16string(buffer.begin(), buffer.end() - 1);
        full_text += u"\n\n"; 
      }
      FPDFText_ClosePage(text_page);
    }
    FPDF_ClosePage(page);
  }

  FPDF_CloseDocument(doc);
  FPDF_DestroyLibrary();

  // 4. Image-Only PDF check
  if (full_text.length() < 50) {
     std::move(callback).Run(""); 
     return;
  }

  std::move(callback).Run(base::UTF16ToUTF8(full_text));
}

}  // namespace rag_ingestion