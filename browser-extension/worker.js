// Brew Controller helper, the worker: when the controller's page asks for an update, it opens each
// subscription's shop in a tab, waits while the owner signs in if they must, reads the details with
// reader.js, and sends them back to the controller tab that asked.
//
// Chrome stops an idle worker after about 30 seconds, so the work in hand is kept in
// chrome.storage.session and every listener is registered here at the top: a page load in a shop tab
// wakes the worker, which picks the job up again.
//
// Rules (docs/SUBSCRIPTIONS.md):
// - Only messages from the paired controller's own page are taken.
// - Only https pages on a subscription's own websites are opened (bbwReader.allowedUrl).
// - Nothing read is stored beyond the job in hand; only the controller's address is kept.
importScripts('reader.js');

const WAIT_MS = 10 * 60 * 1000;   // time to sign in
const MAX_HOPS = 4;

const hostOf = u => {
  try {
    return new URL(u).hostname.replace(/^www\./, '');
  } catch (e) {
    return '';
  }
};
const originsFor = hosts => hosts.flatMap(h => [`https://${h}/*`, `https://www.${h}/*`]);

// The controllers paired in the popup (a home may have more than one, and a test emulator).
async function controllers() {
  const {controllers = [], controller} = await chrome.storage.local.get(['controllers', 'controller']);
  return controller && !controllers.includes(controller) ? [...controllers, controller] : controllers;
}

// The relay runs on the paired controllers' pages only: registered for every one Chrome has allowed,
// and put straight into such pages already open, so no reload is needed.
async function registerRelay() {
  const allowed = [];
  for (const o of await controllers()) {
    if (await chrome.permissions.contains({origins: [o + '/*']})) allowed.push(o + '/*');
  }
  try {
    await chrome.scripting.unregisterContentScripts({ids: ['relay']});
  } catch (e) {}
  if (!allowed.length) return;
  await chrome.scripting.registerContentScripts([{id: 'relay', matches: allowed, js: ['relay.js'], runAt: 'document_end'}]);
  for (const tab of await chrome.tabs.query({url: allowed})) {
    chrome.scripting.executeScript({target: {tabId: tab.id}, files: ['relay.js']}).catch(() => {});
  }
}
// Chrome's prompt can close the popup before it hears the answer: the grant itself completes pairing.
chrome.permissions.onAdded.addListener(registerRelay);
chrome.permissions.onRemoved.addListener(registerRelay);

// ---- the work in hand ---------------------------------------------------------------------------
// queue: [{controllerTab, requestId, sub}]; job: the one being read now, with its shop tab.
async function load() {
  const {queue = [], job = null} = await chrome.storage.session.get(['queue', 'job']);
  return {queue, job};
}
const save = (queue, job) => chrome.storage.session.set({queue, job});

function tell(tabId, requestId, subscription, state, message, data) {
  chrome.tabs.sendMessage(tabId, {type: 'result', requestId, subscription, state, message, data}).catch(() => {});
}

async function next() {
  let {queue, job} = await load();
  if (job || !queue.length) return;
  const item = queue.shift();
  const sub = item.sub;
  const say = (state, message) => tell(item.controllerTab, item.requestId, sub.id, state, message);
  const hosts = [hostOf(sub.url), hostOf(sub.manage_url)].filter((h, i, a) => h && a.indexOf(h) === i);
  const start = [sub.url, sub.manage_url].find(u => bbwReader.allowedUrl(u || '', hosts));
  await save(queue, null);
  if (!start) {
    say('error', 'no https link to read');
    return next();
  }
  if (!(await chrome.permissions.contains({origins: originsFor(hosts)}))) {
    // Chrome asks only from a page the owner clicks in: a small window, and the job waits for it.
    await save(queue, {...item, hosts, start, waitingGrant: true, deadline: Date.now() + WAIT_MS});
    const url = chrome.runtime.getURL('grant.html') + '?' + new URLSearchParams({name: sub.name || hosts[0], origins: originsFor(hosts).join(' ')});
    await chrome.windows.create({url, type: 'popup', width: 460, height: 340});
    return say('working', 'allow the helper to read ' + hosts.join(', '));
  }
  return open({...item, hosts, start}, queue);
}

async function open(item, queue) {
  const tab = await chrome.tabs.create({url: item.start, active: true});
  await save(queue, {...item, waitingGrant: false, shopTab: tab.id, startHost: hostOf(item.start), hops: 0,
                     backHome: false, deadline: Date.now() + WAIT_MS});
  tell(item.controllerTab, item.requestId, item.sub.id, 'working', 'opening ' + hostOf(item.start));
  chrome.alarms.create('deadline', {periodInMinutes: 1});
}

async function finish(state, message, data) {
  const {queue, job} = await load();
  if (!job) return;
  tell(job.controllerTab, job.requestId, job.sub.id, state, message, data);
  if (job.shopTab) chrome.tabs.remove(job.shopTab).catch(() => {});
  await save(queue, null);
  if (!queue.length) chrome.tabs.update(job.controllerTab, {active: true}).catch(() => {});
  next();
}

// A page in the shop tab finished loading: read it.
chrome.tabs.onUpdated.addListener(async (id, info, t) => {
  if (info.status !== 'complete') return;
  const {queue, job} = await load();
  if (!job || job.shopTab !== id) return;
  const say = (state, message) => tell(job.controllerTab, job.requestId, job.sub.id, state, message);
  const url = t.url || '';
  // Signing in may pass through other sites (a login service): wait there without reading.
  if (!bbwReader.allowedUrl(url, job.hosts)) return say('working', 'waiting while you sign in');
  let result;
  try {
    await chrome.scripting.executeScript({target: {tabId: id}, files: ['reader.js']});
    const [r] = await chrome.scripting.executeScript({target: {tabId: id}, func: () => globalThis.bbwReader.readHere()});
    result = r && r.result;
  } catch (e) {
    return say('working', 'waiting for the page');
  }
  if (!result) return;
  if (result.state === 'done') return finish('done', '', result.data);
  if (result.state === 'wait') return say('working', 'sign in to ' + hostOf(url) + ' in the tab that opened');
  if (result.state === 'go' && job.hops < MAX_HOPS && bbwReader.allowedUrl(result.url, job.hosts)) {
    await save(queue, {...job, hops: job.hops + 1});
    return chrome.tabs.update(id, {url: result.url});
  }
  // Nothing here: a page of the shop's account service; try the shop itself once.
  if (hostOf(url) !== job.startHost && !job.backHome) {
    await save(queue, {...job, backHome: true});
    return chrome.tabs.update(id, {url: job.start});
  }
  finish('error', result.why || 'nothing found');
});

chrome.tabs.onRemoved.addListener(async id => {
  const {job} = await load();
  if (job && job.shopTab === id) finish('error', 'the shop tab was closed');
});

chrome.alarms.onAlarm.addListener(async alarm => {
  if (alarm.name !== 'deadline') return;
  const {queue, job} = await load();
  if (!job && !queue.length) return chrome.alarms.clear('deadline');
  if (job && Date.now() > job.deadline) finish('error', job.waitingGrant ? 'not allowed to read the shop' : 'timed out waiting for the page (10 minutes)');
});

chrome.runtime.onMessage.addListener((m, sender) => {
  if (!m) return;
  if (m.type === 'paired') return void registerRelay();
  if (m.type === 'granted') {
    (async () => {
      const {queue, job} = await load();
      if (sender.tab) chrome.tabs.remove(sender.tab.id).catch(() => {});
      if (!job || !job.waitingGrant) return;
      if (await chrome.permissions.contains({origins: originsFor(job.hosts)})) return open(job, queue);
      finish('error', 'not allowed to read ' + job.hosts.join(', '));
    })();
    return;
  }
  if (m.type !== 'update' || !sender.tab) return;
  (async () => {
    let from = '';
    try {
      from = new URL(sender.tab.url).origin;
    } catch (e) {}
    if (!from || !(await controllers()).includes(from)) return;   // only a paired controller
    const {queue} = await load();
    for (const sub of m.subscriptions) {
      if (!sub || typeof sub !== 'object') continue;
      queue.push({controllerTab: sender.tab.id, requestId: m.requestId,
                  sub: {id: +sub.id || 0, name: String(sub.name || '').slice(0, 60), url: String(sub.url || ''),
                        manage_url: String(sub.manage_url || '')}});
    }
    await chrome.storage.session.set({queue});
    chrome.alarms.create('deadline', {periodInMinutes: 1});
    next();
  })();
});

chrome.runtime.onInstalled.addListener(registerRelay);
chrome.runtime.onStartup.addListener(registerRelay);
