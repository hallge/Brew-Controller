// Connects the helper to controllers: the addresses whose pages it relays for (relay.js checks this
// list). Connecting is only ever done here, by the owner: a page cannot connect itself.
const addr = document.getElementById('addr'), state = document.getElementById('state'), list = document.getElementById('list');

const DEFAULT_CONTROLLER = 'http://brewbyweight.local';   // every controller's mDNS name, on from the start
async function controllers() {
  const {controllers = [], controller, forgotDefault} = await chrome.storage.local.get(['controllers', 'controller', 'forgotDefault']);
  const all = controller && !controllers.includes(controller) ? [...controllers, controller] : controllers;
  return forgotDefault || all.includes(DEFAULT_CONTROLLER) ? all : [DEFAULT_CONTROLLER, ...all];
}

async function draw() {
  const all = await controllers();
  const rows = all.map(o => [o, true]);
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
      await chrome.storage.local.set({controllers: (await controllers()).filter(x => x !== o && x !== DEFAULT_CONTROLLER),
                                      controller: '', ...(o === DEFAULT_CONTROLLER ? {forgotDefault: true} : {})});
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
  if (!all.includes(origin) || origin === DEFAULT_CONTROLLER) {
    await chrome.storage.local.set({controllers: [...all.filter(x => x !== DEFAULT_CONTROLLER), origin], controller: '',
                                    ...(origin === DEFAULT_CONTROLLER ? {forgotDefault: false} : {})});
  }
  state.textContent = 'Connected to ' + origin + '. Its page shows Update now (reload it if it does not).';
  draw();
});
