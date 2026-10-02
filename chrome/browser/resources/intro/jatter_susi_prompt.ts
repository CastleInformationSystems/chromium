document.getElementById('signInBtn')!.addEventListener('click', () => {
  chrome.send('handleSusiChoice', [true]); // true = load live SUSI URL
});

document.getElementById('staySignedOutBtn')!.addEventListener('click', () => {
  chrome.send('handleSusiChoice', [false]); // false = skip to Step 3
});