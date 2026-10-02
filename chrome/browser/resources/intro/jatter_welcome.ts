document.getElementById('continueBtn')!.addEventListener('click', () => {
  // Cast to HTMLInputElement to expose the 'checked' property
  const checkbox = document.getElementById('defaultBrowserCheckbox') as HTMLInputElement;
  const isDefaultChecked = checkbox.checked;
  
  chrome.send('continueFromWelcome', [isDefaultChecked]);
});