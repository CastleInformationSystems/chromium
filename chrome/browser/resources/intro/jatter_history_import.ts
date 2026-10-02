import {addWebUiListener} from 'chrome://resources/js/cr.js';

// Define the shape of the data coming from C++
interface BrowserProfile {
  id: number;
  name: string;
}

document.addEventListener('DOMContentLoaded', () => {
  // 1. Ask the C++ backend to scan for browsers
  chrome.send('initializeHistoryImport');

  // 2. Listen for the response
  addWebUiListener('available-browsers-loaded', (browsers: BrowserProfile[]) => {
    const select = document.getElementById('browserSelect') as HTMLSelectElement;
    
    // Safely clear the hardcoded options without violating Trusted Types
    select.replaceChildren(); 

    if (browsers.length === 0) {
      // Fallback if the user has no other browsers installed
      const option = document.createElement('option');
      option.textContent = 'No browsers found';
      option.disabled = true;
      select.appendChild(option);
      
      (document.getElementById('importBtn') as HTMLButtonElement).disabled = true;
      return;
    }

    browsers.forEach((browser: BrowserProfile) => {
      const option = document.createElement('option');
      option.value = browser.id.toString(); 
      option.textContent = browser.name;
      select.appendChild(option);
    });
  });
});

document.getElementById('importBtn')!.addEventListener('click', () => {
  const select = document.getElementById('browserSelect') as HTMLSelectElement;
  
  // Parse the value back to an integer since the C++ ImporterList expects an index
  const selectedIndex = parseInt(select.value, 10);
  chrome.send('handleHistoryImport', [true, selectedIndex]);
});

document.getElementById('skipBtn')!.addEventListener('click', () => {
  // Pass -1 or a dummy value for the index when skipping
  chrome.send('handleHistoryImport', [false, -1]);
});
