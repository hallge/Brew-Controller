# What the shared library receives, and on what terms

> **DRAFT, 13 Oct 2026.** A plain description of what leaves a controller, and a proposal for the terms it would leave under. The proposal needs the
> owner's decision and review by a lawyer (data licence, personal-data rules such as the GDPR where they apply). It is not legal advice.

## What is sent, and only if switched on

Sharing is off until the owner turns it on in Setup (`docs/COMMUNITY.md`). Each kind has its own switch.

| Kind | Content | Switch | Not included |
| --- | --- | --- | --- |
| key | the controller's public key and its fingerprint | with any sharing | the private key (it never leaves the board) |
| coffee | name, roaster, origin, region, producer, varietal, process, roast level, altitude, notes | "coffees I add or change" | the roast date of your bag, anything personal |
| grinder / machine | model, burrs, unit, microns per step, how a setting is read; how a machine is operated | when sent | serial numbers |
| dial-in | where a coffee and recipe settled: setting, average time, spread, roast date, days frozen, recipe, grinder | "settled dial-ins" | any individual shot |
| shot | one shot that counts for advice: times, weight, flow shape, recipe | "every shot" (off by default) | the curve itself; name, address, network details, MAC |
| taste | rating and verdict, with the shot | "taste ratings" (off by default) | free text |

A random per-controller key (not the Wi-Fi or Bluetooth address) signs each record. A signature shows a record has not been altered
and comes from one controller; it does not show who owns it, and records are **signed, not encrypted**: anyone who can read the library can read them.

## Proposed terms (for decision)

1. **You keep ownership** of what you contribute and **grant the project a worldwide, royalty-free, irrevocable licence to publish, copy,
   adapt and redistribute it** as part of the library, under the licence the library chooses.
2. **The library's data licence**: the research recommends **CC0** for factual catalogue fields (coffees, grinder and machine descriptions) and **CC BY 4.0 or CC0** for
   contributed dial-ins and shots (`research/license-and-risk-register.md`). Not decided.
3. **Removal.** Deleting a record from the library removes it from the current index; it stays in git history unless history is rewritten. The proposal is to
   **anonymise rather than delete** shared measurements (as Caliburr does: the link to a key is removed, the measurement stays so that others' dial-ins keep their evidence),
   and to remove a key's name from `contributors`/`keys/` on request. Not built.
4. **No resale of personal data**, no tracking, no accounts: the library has no user database.

## Open questions for counsel

Whether a pseudonymous key id is personal data; whether anonymise-on-request satisfies an erasure request for data already in public git history; the right
data licence for contributed measurements; the repository host's own terms.
