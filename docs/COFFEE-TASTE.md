# Coffee taste: ratings, taste profiles and recommendations

Status: **built** (all four phases), not yet released.

The controller already judges each shot: stars, and whether it tasted sour, balanced or bitter. That
is about the extraction. This adds the question of whether you liked the **coffee**. The answers build
a taste profile, shown as radar charts, and lead to recommendations for coffees you have not tried.

## What is asked

A coffee is rated on the web page (Coffees), any time, once per bag. A later rating of the same bag
replaces the earlier one.

| Part | Values |
|---|---|
| Overall | 1 to 5 stars |
| Cup | acidity, sweetness, body, bitterness, finish: 1 to 5 each, or left blank |
| Flavours | tap any of: fruity, floral, sweet/caramel, chocolate/nutty, spicy, roasty/smoky, fermented/winey, earthy/green |

The flavour families are the first ring of the SCA coffee flavour wheel, in plain words.

**Drinkers.** By default every rating belongs to the household. With *Named drinkers* on (Settings),
each rating says who gave it, from up to 8 names. Each drinker then gets their own profile, radar
charts and recommendations, and the household view combines them.

## Profiles

**A coffee's taste** is the 5 cup values and the 8 flavour families. It comes from, in this order:
1. your own ratings of it;
2. the community's average (below);
3. its description. A dictionary maps the roaster's tasting notes to families ("grape jam" → fruity,
   sweet; "burnt sugars" → sweet/caramel, roasty; "jasmine" → floral). Process, roast level and origin
   add mild hints: a natural leans fruity and fermented, a dark roast roasty and bitter.

Each coffee's taste says where it came from, and the less certain sources count for less.

**Your taste profile** is the average of the coffees you rated, weighted by your stars. Coffees you
loved pull it towards their taste; coffees you disliked push it away. Recent ratings count a little
more. It shows from 3 rated coffees, and is called settled from 8.

**Insights › Your coffee taste:**
- two radar charts: the cup (5 spokes) and the flavours (8 spokes);
- a drinker picker, when named drinkers is on;
- your best-rated coffees.

## Sharing

With *Share ratings* on (Setup › Shared library), each coffee rating is sent to the shared library as
a signed `taste` record. It carries the coffee, the roast date, the stars, the cup values, the
flavours, and an anonymous number for the drinker. Names never leave the controller. The library's
Intake checks and files it, like the other records.

The library's index adds `index/taste.json`: per coffee, the average taste and star spread, published
only once at least two contributors have rated it. It also holds an anonymised table of how similar
drinkers' tastes are. `docs/DATA-TERMS.md` is updated before this is switched on.

## Recommendations

Candidates are every coffee in the shared library and every coffee on your shelf you have not rated.
Finished bags you loved come back as "buy again". Each candidate is scored by:
1. how close its taste is to your profile (cosine similarity), discounted when its taste is only
   guessed from a description;
2. what drinkers with tastes like yours thought of it, once the library knows enough;
3. a small lift for coffees the community rates well.

The top five show on Insights with their reason, for example: *"fruity, floral naturals, like the
Rwanda you gave ★5"*.

Recommendations are worked out by fixed, inspectable rules, with no AI model involved. They never
touch the grind advice.

## Phases

1. **Rate coffees.** Storage on the controller (a ring of 500 ratings), the API, the drinker list and
   setting, and the rating panel on the Coffees page.
2. **Profiles and radar charts.** Coffee tastes from ratings and descriptions, drinker profiles, and
   the Insights card.
3. **Sharing.** The `taste` record, the library tools (check, file, index), and the controller
   fetching `index/taste.json`.
4. **Recommendations.** Scoring, reasons, and the Insights card.
