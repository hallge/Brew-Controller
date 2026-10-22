// Brew Controller helper, on the controller's own web page: says the extension is here, and passes
// messages between the page and the extension's worker. Registered only for the controller's address
// the owner paired in the popup.
(function () {
  if (window.__bbwRelay) return;   // put in once, whether registered or added to an open page
  window.__bbwRelay = true;
  const version = chrome.runtime.getManifest().version;
  document.documentElement.dataset.bbwExtension = version;
  // The page asks: {bbw:'update', requestId, subscriptions:[{id, name, url, manage_url}]}
  window.addEventListener('message', event => {
    if (event.source !== window || event.origin !== location.origin) return;
    const m = event.data;
    if (!m || m.bbw !== 'update' || !Array.isArray(m.subscriptions)) return;
    chrome.runtime.sendMessage({type: 'update', requestId: String(m.requestId || ''), subscriptions: m.subscriptions.slice(0, 12)});
  });
  // The worker answers: progress, a result or an error, for one subscription.
  chrome.runtime.onMessage.addListener(m => {
    if (!m || m.type !== 'result') return;
    window.postMessage({bbw: 'result', requestId: m.requestId, subscription: m.subscription, state: m.state,
                        message: m.message || '', data: m.data || null}, location.origin);
  });
  window.postMessage({bbw: 'hello', version}, location.origin);
})();
