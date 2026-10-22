// Brew Controller helper, on a controller's own web page: says the helper is here (and whether this
// controller is connected), and passes messages between the page and the extension's worker.
//
// Declared in the manifest for every http page, so it is always there; it does nothing unless the page
// is a Brew Controller page (<meta name="bbw-controller">). It only relays for a controller the owner
// connected in the popup: a page cannot connect itself.
(function () {
  if (window.__bbwRelay) return;
  window.__bbwRelay = true;
  if (!document.querySelector('meta[name="bbw-controller"]')) return;
  const version = chrome.runtime.getManifest().version;
  const origin = location.origin;

  async function paired() {
    const {controllers = [], controller} = await chrome.storage.local.get(['controllers', 'controller']);
    return controllers.includes(origin) || controller === origin;
  }

  async function hello() {
    const ok = await paired();
    document.documentElement.dataset.bbwExtension = ok ? version : 'unpaired';
    window.postMessage({bbw: 'hello', version, paired: ok}, origin);
  }

  // The page asks: {bbw:'update', requestId, subscriptions:[{id, name, url, manage_url}]}
  window.addEventListener('message', async event => {
    if (event.source !== window || event.origin !== origin) return;
    const m = event.data;
    if (!m || m.bbw !== 'update' || !Array.isArray(m.subscriptions)) return;
    if (!(await paired())) return hello();
    chrome.runtime.sendMessage({type: 'update', requestId: String(m.requestId || ''), subscriptions: m.subscriptions.slice(0, 12)});
  });
  // The worker answers: progress, a result or an error, for one subscription.
  chrome.runtime.onMessage.addListener(m => {
    if (!m || m.type !== 'result') return;
    window.postMessage({bbw: 'result', requestId: m.requestId, subscription: m.subscription, state: m.state,
                        message: m.message || '', data: m.data || null}, origin);
  });
  // Connected or forgotten in the popup: say so at once.
  chrome.storage.onChanged.addListener(changes => {
    if (changes.controllers || changes.controller) hello();
  });
  hello();
})();
