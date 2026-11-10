# Draft: request to Open Brew by Weight's authors

Dated 2026-11-10. A draft for the project owner to read, change and send themself, by e-mail or as
a GitHub issue or discussion on OBBW's repository. Claude has not sent it. A lawyer should look at
the permission wording (the block below) before it is offered (⚖). It is modelled on the
app-store exceptions other GPL projects have used, but it isn't a vetted text.

Before sending, check two things:

- **Who holds the copyright** in the files: Felipe Urzúa alone, or other contributors as well. Ask
  everyone listed in the files' history, or ask Felipe to say whether OBBW's contributor terms let
  him grant this alone.
- **Who to address.** The letter names Felipe Urzúa because the `LICENSE` file does. If OBBW's
  repository or contributor list shows other holders, add them.

---

**Subject:** Permission to ship Open Brew by Weight's scale tables and stop maths in an App Store app

Hello Felipe, and OBBW's contributors,

I maintain Brew Controller (github.com/hallge/Brew-Controller), brew-by-weight firmware for an
ESP32-S3 board that drives a Breville Dual Boiler with a SwitchBot button pusher and an Acaia Lunar.
It is AGPL-3.0-or-later because it builds on your work, and says so in its README:

- `libraries/EspressoScaleBLE` scale tables, vendored in `lib/EspressoScaleProtocol`;
- the `bbwEwma` prediction, offset learning and adaptive selector, adapted in
  `src/core/BbwAlgorithms.h`.

Thank you for it. The scale tables, in particular, saved months.

I would now like to offer the same controller as an iPhone and iPad app. The App Store's terms of
use add restrictions that the GPL family doesn't allow, so I can't ship AGPL code I don't hold the
copyright to without your permission. I'm asking whether you would grant an additional permission
under AGPL-3.0 section 7, limited to:

- the files of `EspressoScaleBLE` as vendored in my `lib/EspressoScaleProtocol`, and
- the parts of `ShotStopperBbwEwma.h` adapted in my `src/core/BbwAlgorithms.h`,

so that I may distribute them, as part of Brew Controller, through Apple's App Store and similar app
stores, under those stores' standard terms.

In return:

- **The firmware and the app's source stay AGPL-3.0-or-later and public**, and so do my changes to
  your files.
- Your copyright notice and a link to OBBW appear in the app's About screen and its App Store page.
- The permission covers only Brew Controller's distribution through app stores. Anyone may still use
  your code under the AGPL as today.

Wording I would propose (happy to use yours instead):

> As a special exception, the copyright holders of the files listed in `OBBW-EXCEPTION.md` give
> permission to Gregory Hall and the Brew Controller project to distribute those files, and works
> based on them, as part of the Brew Controller application through Apple's App Store and other
> application stores, under the terms of service of those stores, even where those terms add
> restrictions not permitted by the GNU Affero General Public License. This exception does not
> otherwise change the license of those files.

If you'd rather not, I'll understand. Two smaller requests:

1. **Ideas and constants only.** Would you be comfortable with the app using the same rules and
   numbers (for example a 10-sample least-squares cut prediction, offset learning by a fixed
   factor, two confirming samples, flow on/off at 0.6 and 0.2 g/s), reimplemented in my own
   independent code? I believe these aren't protected expression, but I'd rather have your blessing
   than assume.
2. **Any other route.** If you have plans of your own for an app, or would like the scale tables to
   be published under a permissive licence for all apps, I'd gladly help with that instead.

Without a permission, I'll replace those parts with independently written code before any App Store
release, and the firmware will keep using yours under the AGPL as today.

Thank you for considering it.

Gregory Hall
Brew Controller, github.com/hallge/Brew-Controller

---

## Notes for the owner

- `OBBW-EXCEPTION.md` would be a new file in the repository root. It lists the exact files and
  copies the permission, and is added once one is granted. Keep the authors' reply, an e-mail or a
  comment, with it.
- If only request 1 is granted, use `StopMath.h` and `lib/ScaleWire` (route B in
  `research/obbw-provenance.md` §5) and keep the constants.
- The two modified vendored files now carry AGPL §5(a) notices, and the "unmodified" wording is
  corrected (`research/obbw-provenance.md` §1).
