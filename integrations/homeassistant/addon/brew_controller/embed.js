/* Makes the Brew Controller page behave like a Home Assistant app panel.
 *
 * Served by the add-on next to the controller's own page (same origin as Home
 * Assistant through ingress), so it may talk to the Home Assistant page around
 * it: it asks Home Assistant to fold its sidebar into a slide-over drawer
 * (the `hass-kiosk-mode` event, as Music Assistant does) and adds one small
 * button that opens that drawer. Outside Home Assistant (opened directly) it
 * does nothing. If Home Assistant ever changes these events, the page simply
 * stays inside the normal panel. */
(function () {
  'use strict';

  // Net for any absolute path the proxy's rewrite missed: keep it under ingress.
  try {
    var base = new URL('./', document.currentScript.src).pathname; // ends with "/"
    var abs = function (u) {
      return typeof u === 'string' && u.charAt(0) === '/' && u.charAt(1) !== '/' && u.indexOf(base) !== 0
        ? base + u.slice(1) : u;
    };
    var f = window.fetch;
    if (f) window.fetch = function (u, o) { return f.call(this, abs(u), o); };
    var xo = XMLHttpRequest.prototype.open;
    XMLHttpRequest.prototype.open = function (m, u) {
      arguments[1] = abs(u);
      return xo.apply(this, arguments);
    };
  } catch (e) { /* ignore */ }

  var P = null;
  try {
    if (window.parent && window.parent !== window && window.parent.document) P = window.parent;
  } catch (e) { P = null; }
  if (!P) return;

  function kiosk(on) {
    try { P.dispatchEvent(new P.CustomEvent('hass-kiosk-mode', { detail: { enable: on } })); } catch (e) { /* ignore */ }
  }
  function drawer() {
    try {
      var ha = P.document.querySelector('home-assistant');
      var main = ha.shadowRoot.querySelector('home-assistant-main');
      main.dispatchEvent(new P.CustomEvent('hass-toggle-menu', { bubbles: true, composed: true }));
    } catch (e) { /* ignore */ }
  }

  kiosk(true);
  window.addEventListener('pagehide', function () { kiosk(false); });
  window.addEventListener('beforeunload', function () { kiosk(false); });

  function addButton() {
    if (document.getElementById('ha-menu-btn')) return;
    var b = document.createElement('button');
    b.id = 'ha-menu-btn';
    b.type = 'button';
    b.title = 'Home Assistant menu';
    b.setAttribute('aria-label', 'Open the Home Assistant menu');
    b.textContent = '☰';
    b.style.cssText = 'position:fixed;left:10px;bottom:10px;z-index:2147483000;width:40px;height:40px;' +
      'border-radius:20px;border:1px solid rgba(255,255,255,.25);background:rgba(20,22,26,.85);' +
      'color:#e8e8e8;font:20px/1 system-ui,sans-serif;cursor:pointer;padding:0;opacity:.8;' +
      'box-shadow:0 2px 8px rgba(0,0,0,.4)';
    b.onmouseenter = function () { b.style.opacity = '1'; };
    b.onmouseleave = function () { b.style.opacity = '.8'; };
    b.onclick = drawer;
    document.body.appendChild(b);
  }
  if (document.body) addButton();
  else document.addEventListener('DOMContentLoaded', addButton);
})();
