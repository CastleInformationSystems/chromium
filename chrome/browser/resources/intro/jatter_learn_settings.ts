document.getElementById('continueBtn')!.addEventListener('click', () => {
  const isAlwaysAllow = (document.getElementById('alwaysAllow') as HTMLInputElement).checked;
  chrome.send('continueFromLearn', [isAlwaysAllow]);
});