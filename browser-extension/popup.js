// Pairs the helper with a controller: its address, and Chrome's permission to run on its page.
const addr = document.getElementById('addr'), state = document.getElementById('state');

(async () => {
  const {controller} = await chrome.storage.local.get('controller');
  if (controller) {
    addr.value = controller;
    state.textContent = 'Connected to ' + controller;
    return;
  }
  // Opened on the controller's own page: offer its address.
  const [tab] = await chrome.tabs.query({active: true, currentWindow: true});
  try {
    const u = new URL(tab.url);
    if (u.protocol === 'http:' || u.protocol === 'https:') addr.value = u.origin;
  } catch (e) {}
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
  const ok = await chrome.permissions.request({origins: [origin + '/*']});
  if (!ok) {
    state.textContent = 'Not connected: Chrome was not allowed to open that page.';
    return;
  }
  await chrome.storage.local.set({controller: origin});
  chrome.runtime.sendMessage({type: 'paired'});
  state.textContent = 'Connected to ' + origin + '. Reload its page.';
});
