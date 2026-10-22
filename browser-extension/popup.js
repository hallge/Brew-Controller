// Pairs the helper with controllers: their addresses, and Chrome's permission to run on their pages.
//
// The address is saved BEFORE Chrome is asked: Chrome's prompt can close this popup before the request
// answers, and the worker registers the relay itself once the permission is there
// (chrome.permissions.onAdded), so pairing completes either way.
const addr = document.getElementById('addr'), state = document.getElementById('state'), list = document.getElementById('list');

async function controllers() {
  const {controllers = [], controller} = await chrome.storage.local.get(['controllers', 'controller']);
  return controller && !controllers.includes(controller) ? [...controllers, controller] : controllers;
}

async function draw() {
  const all = await controllers();
  const rows = await Promise.all(all.map(async o => [o, await chrome.permissions.contains({origins: [o + '/*']})]));
  list.innerHTML = '';
  for (const [o, ok] of rows) {
    const row = document.createElement('div');
    row.className = 'row';
    const name = document.createElement('span');
    name.textContent = o + (ok ? '' : ' (not allowed yet)');
    const forget = document.createElement('button');
    forget.className = 'small';
    forget.textContent = 'Forget';
    forget.onclick = async () => {
      await chrome.storage.local.set({controllers: (await controllers()).filter(x => x !== o), controller: ''});
      try {
        await chrome.permissions.remove({origins: [o + '/*']});
      } catch (e) {}
      chrome.runtime.sendMessage({type: 'paired'});
      draw();
    };
    row.append(name, forget);
    list.append(row);
  }
  list.style.display = rows.length ? '' : 'none';
}

(async () => {
  // Opened on a controller's page: offer its address.
  const [tab] = await chrome.tabs.query({active: true, currentWindow: true});
  try {
    const u = new URL(tab.url);
    if (u.protocol === 'http:' || u.protocol === 'https:') addr.value = u.origin;
  } catch (e) {}
  const all = await controllers();
  if (all.includes(addr.value)) state.textContent = 'This page is connected. Use Update on its Subscriptions page.';
  draw();
})();

document.getElementById('pair').addEventListener('click', async () => {
  let origin;
  try {
    const raw = addr.value.trim();
    origin = new URL(/^https?:\/\//.test(raw) ? raw : 'http://' + raw).origin;
  } catch (e) {
    state.textContent = 'That is not an address.';
    return;
  }
  const all = await controllers();
  if (!all.includes(origin)) await chrome.storage.local.set({controllers: [...all, origin], controller: ''});
  state.textContent = 'Asking Chrome…';
  const ok = await chrome.permissions.request({origins: [origin + '/*']});
  chrome.runtime.sendMessage({type: 'paired'});
  state.textContent = ok ? 'Connected to ' + origin + '. Reload its page.' : 'Chrome was not allowed to open that page: try again.';
  draw();
});
