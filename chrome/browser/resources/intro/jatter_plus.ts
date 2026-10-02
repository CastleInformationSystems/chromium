document.getElementById('getPlusBtn')!.addEventListener('click', () => {
  chrome.send('handleJatterPlusChoice', [true]);
});

document.getElementById('continueFreeBtn')!.addEventListener('click', () => {
  chrome.send('handleJatterPlusChoice', [false]);
});