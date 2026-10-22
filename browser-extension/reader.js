// Brew Controller helper: reads a coffee subscription's next delivery from a shop's page, in the
// owner's own browser where they are signed in (docs/SUBSCRIPTIONS.md).
//
// Run in the shop's tab by the extension's worker (worker.js), and in node by the page check
// (test/host/page_import_check.mjs) with a stand-in for the page. Only what the controller needs comes
// out: plan, dates, item and coffee names, prices. Never an address, phone, email, payment detail or token.
//
// bbwReadSubscription(env) -> one of
//   {state:'done', data:{v, site, platform, plan, next, later, shipments}}
//   {state:'go', url}       the details are on another page of this shop: open it
//   {state:'wait', why}     sign in first (the worker reads again after the next page load)
//   {state:'none', why}     nothing found here
// env: {href, text: () => visible text, html: () => page HTML, links: () => [{text, href}],
//       fetchJson: async path => object or null (same site, with the owner's cookies),
//       exists: async path => whether that page of this site is there (not a 404)}

(function (root) {
  const MONTHS = {jan: 1, feb: 2, mar: 3, apr: 4, may: 5, jun: 6, jul: 7, aug: 8, sep: 9, sept: 9, oct: 10, nov: 11, dec: 12};

  // A date as YYYYMMDD, from "MON, DECEMBER 7, 2026", "Dec 7, 2026", "December 27, 2026",
  // "2026-10-16T18:34:28.000Z" or "2026-10-16". 0 if none.
  function ymd(value) {
    if (!value) return 0;
    const s = String(value);
    let m = s.match(/(\d{4})-(\d{2})-(\d{2})/);
    if (m) return +m[1] * 10000 + +m[2] * 100 + +m[3];
    m = s.match(/\b(jan|feb|mar|apr|may|jun|jul|aug|sept?|oct|nov|dec)[a-z]*\.?\s+(\d{1,2})(?:st|nd|rd|th)?,?\s+(\d{4})/i);
    if (m) return +m[3] * 10000 + MONTHS[m[1].toLowerCase()] * 100 + +m[2];
    m = s.match(/\b(\d{1,2})\s+(jan|feb|mar|apr|may|jun|jul|aug|sept?|oct|nov|dec)[a-z]*\.?,?\s+(\d{4})/i);
    if (m) return +m[3] * 10000 + MONTHS[m[2].toLowerCase()] * 100 + +m[1];
    return 0;
  }

  // "Every 14 weeks" -> 98 (days). 0 if none.
  function everyDays(s) {
    const m = String(s || '').match(/every\s+(\d+)?\s*(day|week|month)s?/i);
    if (!m) return 0;
    const n = +(m[1] || 1), unit = m[2].toLowerCase();
    return unit == 'day' ? n : unit == 'week' ? n * 7 : Math.round(n * 30.44);
  }

  function cents(s) {
    const m = String(s || '').match(/\$\s?(\d{1,4}(?:\.\d{2})?)/);
    return m ? Math.round(parseFloat(m[1]) * 100) : 0;
  }

  // Nothing personal leaves: text that looks like an email, a phone number, a street address, a card or
  // a token is dropped, and everything is cut to a sensible length.
  const PERSONAL = [
    /[^\s@]+@[^\s@]+\.[a-z]{2,}/i,
    /\+?\d[\d\s().-]{8,}\d/,
    /\b\d{2,6}\s+[A-Za-z][A-Za-z .'-]*\b(st|street|rd|road|ave|avenue|dr|drive|ln|lane|ct|court|blvd|way|pl|place|cir|circle|pkwy|hwy|trl|ter)\b/i,
    /\*{2,}\s?\d{2,4}|\bending in\b|\bcharge to\b/i,
    /token|password|secret/i,
  ];
  function clean(value, limit) {
    if (value == null) return '';
    const s = String(value).replace(/\s+/g, ' ').trim().slice(0, limit || 80);
    return PERSONAL.some(p => p.test(s)) ? '' : s;
  }

  function lines(text) {
    return String(text || '').split(/\n+/).map(l => l.trim()).filter(l => l);
  }

  // ---- Bottomless (a shop's /apps/bottomless/...) ---------------------------------------------
  function shipmentFromBottomless(o) {
    if (!o || typeof o !== 'object') return null;
    const coffee = clean(o.subproduct_name || o.product_name || (o.product && o.product.name) || o.name, 60);
    const track = Array.isArray(o.tracking_updates) && o.tracking_updates.length ? o.tracking_updates[o.tracking_updates.length - 1] : {};
    const detail = track && track.carrier_detail || {};
    const shipment = {
      coffee,
      ordered: ymd(o.date_initiated),
      shipped: ymd(o.date_fulfilled),
      delivered: ymd(o.date_arrived),
      eta: ymd(track && track.est_delivery_date) || ymd(detail.est_delivery_date_local),
      status: clean(o.shipping_status || (track && track.status) || o.status, 24),
    };
    return shipment.coffee || shipment.ordered ? shipment : null;
  }

  async function readBottomless(env, host) {
    const me = await env.fetchJson('/apps/bottomless/me');
    if (!me || typeof me !== 'object' || !('dumb_period' in me || 'product' in me || 'subscriptionType' in me)) return null;
    const plan = {
      name: clean(me.product && me.product.product && me.product.product.name, 60),
      every_days: +me.dumb_period || 0,
    };
    const upcoming = await env.fetchJson('/apps/bottomless/orders/upcoming');
    const last = await env.fetchJson('/apps/bottomless/orders/last-subscription');
    const shipments = [];
    const lastShipment = shipmentFromBottomless(last);
    if (lastShipment) shipments.push(lastShipment);
    const later = [];
    for (const o of (upcoming && Array.isArray(upcoming.orders) ? upcoming.orders : []).slice(0, 4)) {
      const s = shipmentFromBottomless(o);
      if (!s) continue;
      const date = ymd(o.date_initiated || o.scheduled_date || o.date || o.ship_date);
      later.push({date, items: [{name: s.coffee, coffee: true}]});
      if (s.shipped || s.eta) shipments.push(s);
    }
    // The next date is what the shop's subscription page shows ("Next reorder date").
    const text = env.text();
    const m = text.match(/next\s+(?:re)?order\s+date\s*:?\s*\n?\s*([^\n]+)/i);
    let nextDate = m ? ymd(m[1]) : 0;
    if (!nextDate && later.length) nextDate = later[0].date;
    if (!nextDate) {
      // Not on that page: follow the shop's own link to it, once.
      const link = env.links().find(l => /subscription/i.test(l.text) && sameSite(l.href, env.href) && l.href.split('#')[0] != env.href.split('#')[0]);
      // Else Bottomless's own page for it ("subscription by usage"), as shops install it.
      const page = new URL('/pages/subscription-by-usage', env.href).href;
      if (!env.followed) return {state: 'go', url: link ? link.href : page};
    }
    const next = {date: nextDate, items: plan.name ? [{name: plan.name, every_days: plan.every_days}] : []};
    return {state: 'done', data: {v: 1, site: host, platform: 'bottomless', plan, next, later, shipments}};
  }

  // ---- Recharge's customer portal (/tools/recurring/portal/...) -------------------------------------
  const SKIP = /^(send now|reschedule|skip|edit|manage|add|browse all|you may also like|estimated taxes|total|1 ×|\d+ ×|deliver to.*)$/i;

  function readRechargePortal(env, host) {
    const ls = lines(env.text());
    const start = ls.findIndex(l => /^your next order$/i.test(l));
    if (start < 0) return null;
    const date = ymd(ls[start + 1]);
    const items = [];
    let i = start + 2;
    // Past the address: everything from "Deliver to" up to its "Edit".
    for (; i < ls.length && !/^shipping\b/i.test(ls[i]) && !/isn.t in this order/i.test(ls[i]); ++i) {
      const l = ls[i];
      if (/^deliver to/i.test(l)) {
        while (i < ls.length && !/^edit$/i.test(ls[i])) ++i;
        continue;
      }
      if (SKIP.test(l) || /^\$/.test(l)) continue;
      const cadence = everyDays(ls[i + 1]);
      if (cadence) {
        const item = {name: clean(l, 60), every_days: cadence, variant: '', price_cents: 0};
        let k = i + 2;
        if (k < ls.length && !/^\d+ ×|^\$|^edit$/i.test(ls[k])) item.variant = clean(ls[k++], 40);
        // The last price before the next thing (the discounted one, when two are shown).
        for (; k < ls.length && !/^edit$/i.test(ls[k]); ++k) if (/^\$/.test(ls[k])) item.price_cents = cents(ls[k]);
        if (item.name) items.push(item);
        i = k;
      }
    }
    const later = [];
    const notIn = ls.findIndex(l => /isn.t in this order/i.test(l));
    if (notIn >= 0) {
      for (let k = notIn + 1; k < ls.length && !/^you may also like$/i.test(ls[k]); ++k) {
        const when = ls[k].match(/^in\s+(.+)$/i);
        if (!when) continue;
        // Name, then variant, then "In <date>".
        const name = clean(ls[k - 2], 60), variant = clean(ls[k - 1], 40);
        later.push({date: ymd(when[1]), items: [{name, variant}]});
      }
    }
    return {state: 'done', data: {v: 1, site: host, platform: 'recharge', plan: null, next: {date, items}, later, shipments: []}};
  }

  // ---- Trade (drinktrade.com) ----------------------------------------------------------------------
  function readTrade(env, host) {
    const ls = lines(env.text());
    const shipments = [];
    const recent = ls.findIndex(l => /^recent delivery$/i.test(l));
    if (recent >= 0) {
      let k = recent + 1;
      if (/^view past deliveries/i.test(ls[k])) ++k;
      const roaster = clean(ls[k], 40), coffee = clean(ls[k + 1], 60), notes = clean(ls[k + 2], 60);
      const when = ls.slice(k, k + 8).find(l => /^delivered on/i.test(l));
      if (coffee) shipments.push({coffee, roaster, notes, delivered: ymd(when), status: when ? 'delivered' : ''});
    }
    const later = [];
    const queue = ls.findIndex(l => /^(up next|next delivery|my queue)$/i.test(l));
    if (queue >= 0) {
      for (let k = queue + 1; k < ls.length && later.length < 4; ++k) {
        const d = ymd(ls[k]);
        if (d && ls[k + 1]) later.push({date: d, items: [{name: clean(ls[k + 2] || ls[k + 1], 60), coffee: true}]});
      }
    }
    const planGone = /explore plans|no active plan|start a plan/i.test(ls.join(' '));
    return {state: 'done', data: {v: 1, site: host, platform: 'trade', plan: planGone ? null : {name: 'Trade', every_days: 0},
                                  next: {date: later.length ? later[0].date : 0, items: later.length ? later[0].items : []}, later, shipments}};
  }

  // ---- Anything else: dates next to the words that matter ---------------------------------------------
  function readGeneric(env, host) {
    const ls = lines(env.text());
    let nextDate = 0;
    const shipments = [];
    for (let k = 0; k < ls.length; ++k) {
      const l = ls[k];
      const d = ymd(l) || (/(next|ships?|arriv|deliver)/i.test(l) ? ymd(ls[k + 1]) : 0);
      if (!d) continue;
      if (/next\s+(order|delivery|shipment|charge)|ships? on|renews?/i.test(l + ' ' + (ls[k - 1] || ''))) {
        if (!nextDate) nextDate = d;
      } else if (/delivered|arrived/i.test(l)) {
        shipments.push({coffee: '', delivered: d, status: 'delivered'});
      }
    }
    if (!nextDate && !shipments.length) return {state: 'none', why: 'no subscription details found on this page'};
    return {state: 'done', data: {v: 1, site: host, platform: 'page', plan: null, next: {date: nextDate, items: []}, later: [], shipments: shipments.slice(0, 2)}};
  }

  function sameSite(a, b) {
    try {
      const h = u => new URL(u).hostname.replace(/^www\./, '');
      return h(a) == h(b);
    } catch (e) {
      return false;
    }
  }

  function signInPage(env) {
    const t = env.text();
    return env.hasPassword || /^(log in|sign in|login)$/im.test(t) && !/your next order|next reorder date|my subscription/i.test(t);
  }

  async function bbwReadSubscription(env) {
    let host = '';
    try {
      host = new URL(env.href).hostname.replace(/^www\./, '');
    } catch (e) {
      return {state: 'none', why: 'not a web page'};
    }
    const html = env.html ? env.html() : '';
    if (/\/tools\/recurring\/portal\//.test(env.href)) {
      const r = readRechargePortal(env, host);
      if (r) return r;
      return signInPage(env) ? {state: 'wait', why: 'sign in'} : {state: 'none', why: 'the portal did not show the next order'};
    }
    if (/(^|\.)drinktrade\.com$/.test(host)) {
      if (signInPage(env)) return {state: 'wait', why: 'sign in'};
      if (!/account\.drinktrade\.com$/.test(host)) return {state: 'go', url: 'https://account.drinktrade.com/'};
      return readTrade(env, host);
    }
    // Bottomless first: its data answers only for a shop that runs it (and a signed-in customer), and a
    // shop's page may be too long to find its name in, or also load another subscription app's widget.
    const bottomless = await readBottomless(env, host);
    if (bottomless) return bottomless;
    if (/bottomless/i.test(html)) return {state: 'wait', why: 'sign in'};
    // Recharge's portal, when the shop has one (some load Recharge's scripts without it).
    if (/rechargecdn|rechargeapps|ReCharge/.test(html) && !/\/tools\/recurring\//.test(env.href) &&
        !env.followed && (!env.exists || await env.exists('/tools/recurring/login'))) {
      return {state: 'go', url: new URL('/tools/recurring/login', env.href).href};
    }
    if (signInPage(env)) return {state: 'wait', why: 'sign in'};
    if (!env.followed) {
      const link = env.links().find(l => /subscriptions?\b/i.test(l.text) && !/gift|give/i.test(l.text) &&
                                        sameSite(l.href, env.href) && l.href.split('#')[0] != env.href.split('#')[0]);
      if (link) return {state: 'go', url: link.href};
    }
    return readGeneric(env, host);
  }

  // The worker's rule for where it may go: https, and a site the controller named for this subscription.
  function allowedUrl(url, allowedHosts) {
    try {
      const u = new URL(url);
      if (u.protocol !== 'https:') return false;
      const host = u.hostname.replace(/^www\./, '');
      return allowedHosts.some(h => host == h || host.endsWith('.' + h));
    } catch (e) {
      return false;
    }
  }

  // The page's visible text, with what apps draw inside their own shadow roots (Bottomless's
  // <bottomless-app> holds the subscription there).
  function deepText() {
    const parts = [document.body ? document.body.innerText : ''];
    for (const el of document.querySelectorAll('*')) {
      if (!el.shadowRoot) continue;
      for (const child of el.shadowRoot.children) if (child.innerText) parts.push(child.innerText);
    }
    return parts.join('\n');
  }

  // In a page: run with the page itself as env.
  async function readHere() {
    // An app that draws the subscription after the page has loaded: give it a few seconds.
    if (document.querySelector('bottomless-app')) {
      for (let i = 0; i < 16 && !/next\s+(re)?order\s+date/i.test(deepText()); ++i) await new Promise(r => setTimeout(r, 500));
    }
    const env = {
      href: location.href,
      followed: !!sessionStorage.getItem('bbwFollowed'),
      text: deepText,
      html: () => document.documentElement ? document.documentElement.outerHTML : '',
      links: () => [...document.querySelectorAll('a[href]')].slice(0, 400).map(a => ({text: a.innerText.trim(), href: a.href})),
      hasPassword: !!document.querySelector('input[type=password]'),
      exists: async path => {
        try {
          const r = await fetch(path, {credentials: 'include', redirect: 'manual'});
          return r.type === 'opaqueredirect' || (r.status > 0 && r.status < 400);
        } catch (e) {
          return false;
        }
      },
      fetchJson: async path => {
        try {
          const r = await fetch(path, {credentials: 'include', headers: {Accept: 'application/json'}});
          if (!r.ok) return null;
          return await r.json();
        } catch (e) {
          return null;
        }
      },
    };
    const result = await bbwReadSubscription(env);
    if (result.state == 'go') sessionStorage.setItem('bbwFollowed', '1');
    return result;
  }

  const api = {bbwReadSubscription, allowedUrl, ymd, everyDays, readHere};
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  root.bbwReader = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
