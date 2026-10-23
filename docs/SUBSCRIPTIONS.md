# Subscriptions: coffee and supplies that come by post

Status: **built**, not yet released.

Many people get coffee, paper filters or cleaning supplies by subscription, often from several shops.
The **Subscriptions** page keeps them in one place:
- what comes next;
- what it all costs;
- how many filters are left;
- a link to where each one is managed.

Subscription services are also shared like coffees. Anyone can describe a new one, a reviewer lists it,
everyone can add it in one tap, people review the services they use, and the controller recommends
services whose coffees suit your taste.

## Why skip and pause happen on the shop's page

Shops run their subscriptions on systems built for the shop, not the customer. The common ones:
- Recharge, Ordergroove, Skio, Loop, Appstle;
- Shopify's own.

Their programming interfaces take the shop's private key. A customer's app, and so this controller, has
no way in: it cannot skip a delivery, pause a plan or change it.

So each card links to the right page (**Manage**), and the controller keeps its own record:
- when the next charge or delivery comes;
- what it costs;
- what arrived;
- how much is left.

**Arrived** and **Skip one** move the date on here. Skipping at the shop is still the owner's to do,
and the reminder says so.

## Updates from the shops: the Brew Controller helper

Each shop shows the next delivery and the coffee coming only to a customer who is signed in, so the
controller cannot fetch them itself: it would need your shop logins. The **Brew Controller helper**, a
small Chrome extension in `browser-extension/`, reads them in your own browser instead.

**Installing it.** The controller serves the helper at `/extension.zip`, and the Subscriptions page
says how to install it:
1. Download it from the page and unzip it, which gives a folder, `brew-controller-helper`.
2. In Chrome, open `chrome://extensions`, turn on Developer mode, choose **Load unpacked**, and pick
   that folder.
3. Pin the helper, click it, and choose **Connect to this controller**.

The zip is packed from `browser-extension/` when the firmware is built (`browser-extension/make_zip.py`,
run by `scripts/gen_extension.py`), so it always matches the firmware. It works in desktop Chrome, Edge
and Brave; phones do not run extensions.

**Using it.** **Update** on a card, or **Update all** (active subscriptions only; a paused or cancelled
one is read only from its own card):
- The helper opens the shop in a tab. If the shop asks, you sign in as usual; the helper waits up to 10
  minutes.
- It reads only the next date, how often, items and coffees, prices and delivery dates, then closes
  the tab.
- The page lists what differs, each line ticked or not, and **Apply** makes the changes.

**What it understands:**
- **Bottomless** shops, through the shop's own `/apps/bottomless/` data and its subscription page;
- the **Recharge** customer portal;
- **Trade**'s account pages;
- the next-delivery wording of other pages.

**Rules:**
- Chrome asks once for each shop it may read.
- It opens only https pages of the subscription's own websites.
- It answers only the paired controller's page, which it is told about in its popup.
- It never sends an address, phone, email, payment detail or token: what it reads is filtered before it
  leaves the shop's tab.

## Coffee on its way

A bag can be **on its way**: ordered, with an expected date, not on the shelf yet. The import offers
the coffee a shop says is coming that way. **It arrived** (on the Coffees page, or Arrived on the
subscription) makes it an unopened bag on the shelf, and asks for its roast date.

A bag on its way is never opened, never makes a finished coffee show again, and is never dropped to
make room. The bag record's state is 0 (in use or past), 1 (unopened) or 2 (on its way), and it gains
`expectedDate`. Older records read 0.

## A card from a link

Paste the shop's link and press **Read the page**.
- The controller fetches that page once and reads its `<head>` (at most 192 KB):
  - title, site name, description and picture (Open Graph);
  - icon and theme colour;
  - which subscription system the page loads. From that it suggests where to manage it; Recharge's
    customer portal is at `/tools/recurring/login` on every shop, the others under `/account`.
- **Certificates:** the fetch is HTTPS checked against Mozilla's root certificates, which are built into
  the firmware: `certs/x509_crt_bundle.bin`, made by `certs/gen_cert_bundle.py` from curl's copy of
  the list.
- **Bot filters:** the controller's browser-style agent ends in `BrewController/1.0`, because some shops
  turn away agents they do not recognise.
- **Blocked:** if a shop turns the fetch away anyway, the card uses its initial and a colour.
- **Editable:** every field stays editable.
- **The pictures** are loaded by your browser, straight from the shop.

## What the controller keeps (`/subs.json`)

**Per subscription:**
- name, links, kind (coffee, supplies, other) and status (active, paused, cancelled);
- how often it comes, the next date, price, note, and the last delivery;
- supplies only: what it is, the stock, how many each shot uses (1 for a paper filter), and how many a
  delivery brings.

**What it works out** (`core/Subscriptions.h`):
- **Monthly spend:** the active subscriptions' prices, scaled to an average month.
- **Reminders:** 3 days before an active subscription's next date, and when one is overdue.
- **Filter stock:** each saved shot takes a filter off. How long the stock lasts comes from the shots
  of the last 14 days, with a warning when it runs out before the next delivery.
- **The next date:** after Arrived or Skip it moves on one period from the day it was due, and is never
  in the past.

## Coffee that arrives: unopened bags

**Arrived** on a coffee subscription puts the bag on the Coffees shelf **unopened**: either a new
coffee, or one already on the shelf (a re-order). The bag says which subscription it came with.

Nothing is tracked until the bag is put in use: no age, no run of shots for grind advice. That
happens:
- **by hand:** "Open this bag" on the Coffees page;
- **by choosing its coffee**, on the controller or the web, when that coffee has no other bag in use;
- **at BAG FINISHED:**
  - another bag of the same coffee waiting: it goes into use;
  - otherwise the controller moves on to the oldest-roasted bag on the shelf. If that is an unopened
    bag of a coffee not on the controller's list, the coffee is added with the same recipe, if the list
    (16) has room.

The bag record gains `unopened` and `subscriptionId` at its end. Libraries saved before read both as 0,
and an older firmware ignores them.

## The shared catalog

**A `service` record:** sent when the owner presses **Send to the shared library** on a service they set
up. It is signed, and goes to `services/<key id>/s<local id>-<record>.json`:

    {"t":"service","v":1,"at":...,"record":N,"local_id":N,
     "name":"...","url":"https://...","manage_url":"https://...","platform":"recharge"|...,
     "kind":"coffee"|"supplies"|"other",
     "ships_to":["US","CA"],"cadences":[14,28],          days
     "price_from_cents":1500,"currency":"USD",          a hint only
     "style":["multi_roaster","matched",...],
     "sourcing":["fair_trade","direct_trade","organic","farm_named","prices_published",
                 "sustainable_packaging","carbon_neutral","b_corp"],
     "image":"https://...","icon":"https://...","color":"#rrggbb","about":"..."}

**Never sent:** the owner's own price, dates, stock and notes, or the owner's own links. A pasted
link often belongs to one customer (a portal address with a customer id and a login token, a shop page
with tracking in it). Before sending, the query and fragment are dropped, and a path that looks
personal is replaced by the shop's general page: Recharge's sign-in page for a manage link, else the
shop's account page, or its home page. The library refuses links that still carry a query, a token or
a long customer id.

**The library's handling:**
- **Intake** checks it and opens a review for `services/<slug>.json`. The pull request asks the
  reviewer to check the links, where it ships, and each sourcing claim against the shop's site, and to
  remove any claim without evidence. It also lists services with a similar name or the same website.
- **The outcome** reaches the owner as it does for coffees: in review, published under its id, declined
  with a note, or refused by the checks.
- **The listing:** `index/services.json` lists what was merged. The controller reads it with the
  library's other lists and shows it under **From the shared library**, where **Add to my
  subscriptions** makes a prefilled card that carries the service's id.

## Reviews

One review per drinker per service:
- stars;
- six aspects, each 1 to 5 and optional: value, quality, consistency, reliability (on time, well
  packed), flexibility (skipping, pausing, changing) and transparency (where it comes from, what farmers
  are paid);
- a note that stays on the controller.

**Storage and sharing:**
- Reviews are kept in `/svcreviews.json`.
- With *Share ratings* on, a review of a service in the library is sent as a signed `service_review`
  record: `{service, drinker (a number), stars, aspects}`.
- `index/services.json` publishes a service's review count, mean stars and mean aspects once two or
  more controllers have reviewed it.

## Recommendations

**Taste:** a coffee rating of a bag that came with a library-listed service carries `"service":"<id>"`.
From those ratings `index/taste.json` gains, once two or more controllers have rated coffees from a
service:
- `services`: the mean taste of the coffees each service sent;
- in each anonymous drinker's row, `svc:<id>`: how much they liked that service's coffees.

**Which services are considered:** the library's services you do not already have (matched by id or
website) and that ship to your country, if you set one.

**The score** (`serviceScore` in `core/TasteProfile.h`):
1. how the service's coffees taste against your profile, as for coffees;
2. what drinkers whose coffee stars agree with yours made of its coffees;
3. its reviews;
4. a lift for each sourcing value you marked under **What matters to you** that it claims, at most
   three.

Until you have rated three coffees, or before any service has a taste, services are ranked by reviews
and the values that matter, and the page says so.

Every reason is shown on the card, for example "fruity · more acidity · farms named · 4.7★ from 3
reviews". These are fixed rules; no AI model is involved.
