// ios/chrome/browser/jatter/rag_ingestion.ts

console.log("[RAG-TS] !!! RAG INGESTION SCRIPT INJECTED AND RUNNING !!!");

interface ExtractedContent {
  text: string;
  links: string[];
  error?: string;
}

function extractContent(): ExtractedContent {
  try {
    const bodyText = document.body?.innerText || "";
    
    // Safely traverse links without assuming Array.from won't fail on weird DOMs
    const rawLinks = document.links || [];
    const links: string[] = [];
    
    for (let i = 0; i < rawLinks.length; i++) {
      const href = rawLinks[i]?.href;
      if (href && typeof href === "string" && href.length > 0) {
        links.push(href);
      }
    }

    console.log(`[RAG-TS] Extraction success! Chars: ${bodyText.length}, Links: ${links.length}`);
    return {
      text: bodyText,
      links: links
    };
  } catch (err: any) {
    console.error("[RAG-TS] FATAL ERROR during DOM scraping:", err);
    return {
      text: "",
      links: [],
      error: err?.toString() || "Unknown JS extraction error"
    };
  }
}

// ==========================================
// THE CHROMIUM gCrWeb API REGISTRATION
// ==========================================
const globalObj = globalThis as any;
const gCrWeb = globalObj.__gCrWeb || {};

// 1. Map our exposed functions
const functionsMap: { [key: string]: Function } = {
  extractContent: extractContent
};

// 2. Build an API object that exactly matches Chromium's internal class interface
const ragIngestionApi = {
  getApiName: function(): string {
    return "ragIngestion";
  },
  hasFunction: function(name: string): boolean {
    return !!functionsMap[name];
  },
  getFunction: function(name: string): Function | undefined {
    return functionsMap[name];
  },
  // [FIXED] Prefix with underscore to satisfy TS strict unused-parameter checks
  getProperty: function(_name: string): any {
    return undefined;
  },
  addFunction: function(name: string, fn: Function): void {
    functionsMap[name] = fn;
  }
};

// 3. Register with the message router
if (typeof gCrWeb.registerApi === "function") {
  gCrWeb.registerApi(ragIngestionApi);
} else {
  gCrWeb.ragIngestion = ragIngestionApi;
}

globalObj.__gCrWeb = gCrWeb;