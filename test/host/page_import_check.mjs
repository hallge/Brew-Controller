// The Brew Controller helper's reader (browser-extension/reader.js) on pages laid out as the shops lay
// them out, with made-up names, dates, amounts and people. Checks what it picks out, that nothing
// personal ever comes out, and where the worker may go.
//    node test/host/page_import_check.mjs
import {createRequire} from 'module';
const require = createRequire(import.meta.url);
const reader = require('../../browser-extension/reader.js');

let checks = 0, failed = 0;
function check(ok, what) {
  ++checks;
  if (!ok) {
    ++failed;
    console.log('  FAIL ' + what);
  }
}

function env({href, text = '', html = '', links = [], json = {}, hasPassword = false, followed = false}) {
  return {href, followed, hasPassword, text: () => text, html: () => html, links: () => links,
          fetchJson: async path => (path in json ? json[path] : null)};
}

const PERSONAL = /example\.com|555|Elm Street|\*0938|4242|token|Jane|Doe/i;
const clean = (r, what) => check(!PERSONAL.test(JSON.stringify(r)), what + ': nothing personal in the output');

// ---- Bottomless, on a shop's subscription page ---------------------------------------------------
{
  const me = {product: {product: {name: 'Example Coffee Club', vendor_name: 'Example Roasters'}}, dumb_period: 28,
              subscriptionType: 'fixed', first_name: 'Jane', last_name: 'Doe', local: {email: 'jane@example.com'}};
  const last = {status: 'fulfilled', subproduct_name: 'HOUSE BLEND', date_initiated: '2026-10-13T20:05:31.521Z',
                date_fulfilled: '2026-10-14T08:10:11.962Z', date_arrived: '2026-10-16T18:34:28.000Z', shipping_status: 'delivered',
                address: {street1: '12 Elm Street', phone: '555-010-0199'},
                tracking_updates: [{status: 'delivered', est_delivery_date: null, carrier: 'USPS', tracking_code: '9400TOKEN'}]};
  const upcoming = {orders: [{subproduct_name: 'GUATEMALA FINCA EXAMPLE', date_initiated: '2026-11-12T10:00:00Z', status: 'pending'}]};
  const page = 'Your Subscription\nWe detected your phone number:\n+15550100199\nVerify\nFrequency\nNext reorder date\nNov 12, 2026\nProduct Preference\nExample Coffee Club';
  const r = await reader.bbwReadSubscription(env({
    href: 'https://shop.example.org/pages/subscription-by-usage#subscription', text: page,
    html: '<script src="https://cdn.bottomless.com/x.js"></script>',
    json: {'/apps/bottomless/me': me, '/apps/bottomless/orders/last-subscription': last, '/apps/bottomless/orders/upcoming': upcoming}}));
  check(r.state == 'done' && r.data.platform == 'bottomless', 'bottomless: read');
  check(r.data.plan.name == 'Example Coffee Club' && r.data.plan.every_days == 28, 'bottomless: plan');
  check(r.data.next.date == 20261112, 'bottomless: next date from the page');
  check(r.data.shipments[0].coffee == 'HOUSE BLEND' && r.data.shipments[0].delivered == 20261016 &&
        r.data.shipments[0].shipped == 20261014 && r.data.shipments[0].status == 'delivered', 'bottomless: last shipment');
  check(r.data.later[0].date == 20261112 && r.data.later[0].items[0].name == 'GUATEMALA FINCA EXAMPLE', 'bottomless: upcoming coffee');
  clean(r, 'bottomless');
  // On the shop's home page: the next date is on its subscription page; follow the shop's own link once.
  const home = await reader.bbwReadSubscription(env({
    href: 'https://shop.example.org/', html: 'bottomless', text: 'Shop',
    links: [{text: 'Coffee Club', href: 'https://shop.example.org/collections/club'},
            {text: 'Manage Subscription', href: 'https://shop.example.org/pages/subscription-by-usage'},
            {text: 'Subscription help', href: 'https://elsewhere.example.net/subscription'}],
    json: {'/apps/bottomless/me': me, '/apps/bottomless/orders/last-subscription': last, '/apps/bottomless/orders/upcoming': {orders: []}}}));
  check(home.state == 'go' && home.url == 'https://shop.example.org/pages/subscription-by-usage', 'bottomless: follows its own link');
  // No link to it on the page: Bottomless's own page for the subscription.
  const bare = await reader.bbwReadSubscription(env({
    href: 'https://shop.example.org/', html: 'bottomless', text: 'Shop', links: [],
    json: {'/apps/bottomless/me': me, '/apps/bottomless/orders/last-subscription': last, '/apps/bottomless/orders/upcoming': {orders: []}}}));
  check(bare.state == 'go' && bare.url == 'https://shop.example.org/pages/subscription-by-usage', 'bottomless: goes to its subscription page');
  // Already followed once and still no date: what is known comes back, without a date.
  const after = await reader.bbwReadSubscription(env({
    href: 'https://shop.example.org/pages/subscription-by-usage', html: 'bottomless', text: 'Shop', followed: true,
    json: {'/apps/bottomless/me': me, '/apps/bottomless/orders/last-subscription': last, '/apps/bottomless/orders/upcoming': {orders: []}}}));
  check(after.state == 'done' && after.data.next.date == 0 && after.data.shipments.length == 1, 'bottomless: no loop');
  // Not signed in: the app answers nothing.
  const out = await reader.bbwReadSubscription(env({href: 'https://shop.example.org/', html: 'bottomless', text: 'Log in'}));
  check(out.state == 'wait', 'bottomless: not signed in waits');
}

// ---- Recharge's customer portal --------------------------------------------------------------------
{
  const text = ['Your next order', 'MON, DECEMBER 7, 2026', 'Send now', 'Reschedule', 'Skip',
    'Deliver to 12 Elm Street', 'Springfield, ST 00000', 'Edit',
    'Portafilter Paper Filters (100 Count)', 'Every 14 weeks', 'Size J / 100', '1 ×', '$8.99 USD', '$8.09 USD', 'Edit',
    'Shipping: Ground Advantage', '$6.70 USD', 'Estimated taxes', '$0.00 USD', 'Total', '$14.79 USD', 'Charge to *0938', 'Edit',
    '1 of your product isn’t in this order', 'Espresso Paper Filters (100 Count Per Pack)', 'Size D / 100 (1 Pack)',
    'In December 27, 2026', 'Manage', 'You may also like', 'Browse all', 'Example Single Origin', '$14.99 USD', 'Add'].join('\n');
  const r = await reader.bbwReadSubscription(env({href: 'https://shop.example.org/tools/recurring/portal/0123abcd/overview', text}));
  check(r.state == 'done' && r.data.platform == 'recharge', 'recharge: read');
  check(r.data.next.date == 20261207, 'recharge: next date');
  const it = r.data.next.items[0] || {};
  check(r.data.next.items.length == 1 && it.name == 'Portafilter Paper Filters (100 Count)' && it.every_days == 98 &&
        it.variant == 'Size J / 100' && it.price_cents == 809, 'recharge: the item, its cadence, variant and price');
  check(r.data.later.length == 1 && r.data.later[0].date == 20261227 && r.data.later[0].items[0].variant == 'Size D / 100 (1 Pack)',
        'recharge: the item in another order');
  clean(r, 'recharge');
  // A Recharge shop page that is not the portal: go to the portal's sign-in.
  const shop = await reader.bbwReadSubscription(env({href: 'https://shop.example.org/', html: '<script src="https://static.rechargecdn.com/a.js">', text: 'Shop'}));
  check(shop.state == 'go' && shop.url == 'https://shop.example.org/tools/recurring/login', 'recharge: goes to the portal');
  const login = await reader.bbwReadSubscription(env({href: 'https://shop.example.org/tools/recurring/portal/x/overview', text: 'Log in\nEmail', hasPassword: true}));
  check(login.state == 'wait', 'recharge: sign-in waits');
}

// ---- Trade ----------------------------------------------------------------------------------------
{
  const text = ['My Trade', 'Hi, Jane!', 'You have up to 3 free bags waiting!', 'Explore Plans', 'Recent Delivery', 'View Past Deliveries',
    'EXAMPLE ROASTERS', 'Honduras Example', 'Chocolaty & Sweet', 'Rate this coffee', 'Delivered On Apr 21, 2025', 'Add to Cart',
    'Shipping Address', 'Jane Doe', '12 Elm Street', 'Payment Method', '**** 4242'].join('\n');
  const r = await reader.bbwReadSubscription(env({href: 'https://account.drinktrade.com/', text}));
  check(r.state == 'done' && r.data.platform == 'trade' && r.data.plan === null, 'trade: read, no active plan');
  check(r.data.shipments[0].coffee == 'Honduras Example' && r.data.shipments[0].roaster == 'EXAMPLE ROASTERS' &&
        r.data.shipments[0].delivered == 20250421, 'trade: recent delivery');
  clean(r, 'trade');
  const shop = await reader.bbwReadSubscription(env({href: 'https://www.drinktrade.com/', text: 'Shop coffee'}));
  check(shop.state == 'go' && shop.url == 'https://account.drinktrade.com/', 'trade: goes to the account pages');
}

// ---- any other page --------------------------------------------------------------------------------
{
  const r = await reader.bbwReadSubscription(env({href: 'https://roaster.example.org/account', text: 'Your plan\nNext delivery\nNovember 20, 2026\nContact jane@example.com'}));
  check(r.state == 'done' && r.data.next.date == 20261120, 'generic: next delivery');
  clean(r, 'generic');
  const none = await reader.bbwReadSubscription(env({href: 'https://roaster.example.org/', text: 'Welcome'}));
  check(none.state == 'none', 'generic: nothing found');
}

// ---- dates, cadences, where the worker may go -------------------------------------------------------
check(reader.ymd('MON, DECEMBER 7, 2026') == 20261207 && reader.ymd('Dec 27, 2026') == 20261227 &&
      reader.ymd('2026-10-16T18:34:28.000Z') == 20261016 && reader.ymd('7 Dec 2026') == 20261207 && reader.ymd('soon') == 0, 'dates');
check(reader.everyDays('Every 14 weeks') == 98 && reader.everyDays('every 2 weeks') == 14 && reader.everyDays('Every month') == 30 &&
      reader.everyDays('weekly') == 0, 'cadences');
const hosts = ['shop.example.org', 'shopify.com'];
check(reader.allowedUrl('https://shop.example.org/pages/x', hosts) && reader.allowedUrl('https://www.shop.example.org/', hosts) &&
      reader.allowedUrl('https://accounts.shopify.com/', hosts), 'worker: the shop and its own subdomains');
check(!reader.allowedUrl('http://shop.example.org/', hosts) && !reader.allowedUrl('https://evil.example.net/?shop.example.org', hosts) &&
      !reader.allowedUrl('https://shop.example.org.evil.net/', hosts) && !reader.allowedUrl('javascript:alert(1)', hosts),
      'worker: refuses http, other sites, look-alikes and scripts');

console.log(`page import: ${checks} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
