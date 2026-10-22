// Asks Chrome for the sites one subscription lives on (a permission request needs a click in a page).
const q = new URLSearchParams(location.search);
const origins = (q.get('origins') || '').split(' ').filter(o => /^https:\/\/[a-z0-9.-]+\/\*$/i.test(o));
document.getElementById('name').textContent = q.get('name') || 'this shop';
document.getElementById('sites').textContent = [...new Set(origins.map(o => o.slice(8, -2).replace(/^www\./, '')))].join(', ');
document.getElementById('yes').addEventListener('click', async () => {
  await chrome.permissions.request({origins});
  chrome.runtime.sendMessage({type: 'granted'});
});
document.getElementById('no').addEventListener('click', () => window.close());
