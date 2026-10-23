// Brew Controller helper, on a controller's own web page: says the helper is here (and whether this
// controller is connected), and passes messages between the page and the extension's worker.
//
// Declared in the manifest for every http page, so it is always there; it does nothing unless the page
// is a Brew Controller page (<meta name="bbw-controller">). It only relays for a controller the owner
// connected in the popup: a page cannot connect itself.
(async function () {
  if (window.__bbwRelay) return;
  window.__bbwRelay = true;
  const version = chrome.runtime.getManifest().version;
  const origin = location.origin;

  // The name every controller answers to on a home network (mDNS), connected from the start. It may
  // stand for any device that claims the name, so it is taken only on a page that says it is a Brew
  // Controller page, or on firmware from before that marker once its page shows the controller's own
  // Subscriptions section. Forgetting it in the popup turns this off.
  const DEFAULT = 'http://brewbyweight.local';
  const looksLikeOne = () => !!document.querySelector('meta[name="bbw-controller"]') ||
                             (!!document.getElementById('p-subscriptions') && !!document.getElementById('sxbody'));
  // After the helper is reloaded or updated, Chrome cuts this copy (left in a page that was already
  // open) off from the helper: it can no longer read or send. The page is told to reload instead.
  const alive = () => { try { return !!(chrome.runtime && chrome.runtime.id && chrome.storage); } catch (e) { return false; } };
  async function paired() {
    if (!alive()) return false;
    const {controllers = [], controller, forgotDefault} = await chrome.storage.local.get(['controllers', 'controller', 'forgotDefault']);
    if (controllers.includes(origin) || controller === origin) return true;
    return origin === DEFAULT && !forgotDefault && looksLikeOne();
  }
  // A Brew Controller page says so; a connected one is taken as it is (firmware from before the marker).
  if (!looksLikeOne() && !(await paired())) return;

  async function hello() {
    if (!alive()) return;
    const ok = await paired();
    document.documentElement.dataset.bbwExtension = ok ? version : 'unpaired';
    window.postMessage({bbw: 'hello', version, paired: ok}, origin);
  }

  // The page asks: {bbw:'update', requestId, subscriptions:[{id, name, url, manage_url}]}
  window.addEventListener('message', async event => {
    if (event.source !== window || event.origin !== origin) return;
    const m = event.data;
    if (!m || m.bbw !== 'update' || !Array.isArray(m.subscriptions)) return;
    if (!alive()) return window.postMessage({bbw: 'stale'}, origin);
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
    if (changes.controllers || changes.controller || changes.forgotDefault) hello();
  });
  hello();
})();
