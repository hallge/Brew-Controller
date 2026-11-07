// Grind advice in words, ported from the controller page's loadGrind (src/net/WebPage.h:1388-1430)
// and the recommendation / missing-setting banners (WebPage.h:1380-1386). Used by Home and Grinder.
import { f1, fdn, fdf, fdu, fs, fsn } from "./format.js";

/** {title, cls, short, why, apply, age, ageShort, rated, sensitivity} from /api/grind and the grinder `g`. */
export function adviceText(G, g) {
  if (!G) return null;
  const F = (v) => fs(g, v), D = (v) => fdn(g, v), U = fdu(g);
  let title, cls = "", why, short, apply = null;
  const basis = `Average ${G.time_basis == "pour" ? "pour" : "total"} time ${f1(G.average_time_s)} s over ${G.shots_used} shot${G.shots_used == 1 ? "" : "s"} at ${F(G.current_um)}, target ${G.target_time_s} s ± ${G.tolerance_s} s.`;
  const fdo = G.first_drop_one_off_s || 0;
  const fdw = fdo ? `its first drops came ${f1(Math.abs(fdo))} s ${fdo < 0 ? "early" : "late"}${G.usual_first_drop_s ? " (usually " + f1(G.usual_first_drop_s) + " s)" : ""}` : "";
  const out = G.outliers_ignored ? ` ${G.outliers_ignored} one-off shot${G.outliers_ignored == 1 ? "" : "s"} left out (${fdw ? "the newest: " + fdw + ", the puck rather than the grind" : "far from the others at this setting"}).` : "";
  const drift = G.drift_s_per_day && Math.abs(G.drift_s_per_day) >= 0.1 ? ` This coffee has been running about ${f1(Math.abs(G.drift_s_per_day))} s ${G.drift_s_per_day > 0 ? "slower" : "faster"} a day at one setting; older shots are adjusted for it.` : "";
  const rev = G.newest_after_reversal && G.reversal_weight < 1 ? ` The newest shot was the first after turning the dial back: counted at ${Math.round(100 * G.reversal_weight)}% weight.` : "";
  const play = G.play_um ? ` The move turns the dial back, so it includes about ${D(G.play_um)} ${U} of play in the adjustment: turn straight to ${F(G.suggested_um)}.` : "";
  const sens = G.sensitivity_source === "measured"
    ? ` Sensitivity ${fdf(g, G.um_per_s)} ${U} per second, measured from ${G.learn_shots} of your shots${G.data_weight < 0.99 ? " (" + Math.round(100 * G.data_weight) + "% from this coffee, the rest from what this grinder has shown)" : ""}.`
    : G.sensitivity_source === "grinder"
      ? ` Sensitivity ${fdf(g, G.um_per_s)} ${U} per second, from what this grinder has shown on ${G.grinder_coffees} other coffee(s) (${G.grinder_shots} shots); this coffee's own shots take over as they come.`
      : ` Sensitivity ${fdf(g, G.um_per_s)} ${U} per second is the starting guess; it is measured once shots exist at two or more grind settings.`;
  if (G.action == "no_grind_set") { title = "Enter the grinder's setting"; why = short = "Shots are stored with the grind setting entered here. Advice starts with the first valid shot."; }
  else if (G.action == "no_data" && fdo) { title = "Last shot left out: the puck, not the grind"; cls = "warn"; short = `The last shot at ${F(G.current_um)} ran ${f1(G.newest_time_s)} s, but ${fdw}. Check dose, distribution and tamp, and pull another at ${F(G.current_um)}.`; why = short + " A shot whose first drops come this far from the usual and that runs off the target the same way says more about the puck than about the grind. Choose Count on the shot to use it anyway."; }
  else if (G.action == "no_data") { title = "No shot at this setting yet"; short = `Pull a shot at ${F(G.current_um)}.`; why = short + " It counts when it stops on the weight and runs from 15 s to the target time + 15 s."; }
  else if (G.action == "inconsistent") {
    title = "Brew times vary: no advice"; cls = "warn";
    short = `The last ${G.shots_used} shots at ${F(G.current_um)} ran from ${f1(G.min_time_s)} s to ${f1(G.max_time_s)} s against ${G.target_time_s} s ± ${G.tolerance_s} s. Check dose, distribution and tamp before moving the grind.`;
    why = `The last ${G.shots_used} shots at ${F(G.current_um)} ran from ${f1(G.min_time_s)} s to ${f1(G.max_time_s)} s against a target of ${G.target_time_s} s ± ${G.tolerance_s} s, some fast and some slow. Their average (${f1(G.average_time_s)} s) describes none of them, so it is not used. If the grinder was changed between them, enter its setting: shots are only compared at the same setting. Otherwise look at dose, distribution and tamp. A shot can be left out on the Shots tab.` + out;
  }
  else if (G.out_of_range) { const fin = G.action == "finer"; title = `Go ${fin ? "finer" : "coarser"} ${D(G.delta_um)} ${U}, to ${F(G.suggested_um)}`; cls = "warn"; why = short = `The last shot ran ${fin ? "too short (under 15 s)" : "more than 15 s over the target"} to be counted, so its time cannot size the change. The direction is clear: this is the largest single change. The next shot gets the usual advice.`; }
  else if (G.action == "hold" && G.between_steps) { title = "Keep " + F(G.current_um) + " (closest step)"; cls = "ok"; why = basis + " The target lies between this setting and the next one, which ran on the other side of it; there is no setting between them, so the closer one is kept." + out; short = basis; }
  else if (G.action == "hold" && G.at_limit) { title = "Keep " + F(G.current_um) + " (end of the range)"; cls = "warn"; why = basis + " The shots ask for a change past the end of this grinder's range, so there is nothing left to turn: try a different dose, or a fresher or older coffee." + out; short = basis; }
  else if (G.action == "hold") { title = "Keep " + F(G.current_um); cls = "ok"; why = basis + " Inside tolerance." + out + drift + rev; short = basis; }
  else {
    const fin = G.action == "finer";
    title = `Go ${fin ? "finer" : "coarser"} ${D(G.delta_um)} ${U}, to ${F(G.suggested_um)}${G.refined ? " (to your liked time)" : ""}`; cls = "warn";
    short = basis + ` Running ${f1(Math.abs(G.error_s))} s ${fin ? "fast" : "slow"}.`;
    why = short + sens + (G.step_limited ? " Limited to the largest single change; expect another step." : "") + out + drift + rev + play;
    apply = G.suggested_um;
  }
  const c = G.confidence;
  if (c && (G.action == "finer" || G.action == "coarser") && !G.out_of_range) {
    const pc = Math.round(100 * c.score), lv = pc >= 60 ? "high" : pc >= 35 ? "medium" : "low";
    short += ` Window ± ${D(c.window)} ${U}, confidence ${lv} (${pc}%).`;
    why += ` Confidence ${pc}% from ${c.followed} change${c.followed == 1 ? "" : "s"} that ${c.followed == 1 ? "was" : "were"} followed` + (c.followed ? `, landing ${f1(c.rms_s)} s from the target, typically` : "") + "." + (c.on ? (c.scaled ? (pc < 50 ? " The step is made smaller while advice is still being proved." : " The step is allowed to be larger because past advice landed close.") : "") : " Sizing by confidence is off.");
  }
  // bean age
  const ag = G.bean_age_days, ds = G.days_since_brewed, sh = G.age_shift;
  let age = "", ageShort = "";
  if (!G.coffee) age = "";
  else if (ag == null || ag < 0) age = "Bean age: no roast date is entered for this bag, so its age cannot be taken into account.";
  else {
    ageShort = "Bag " + ag + " day" + (ag == 1 ? "" : "s") + " off roast";
    age = "Bean age: this bag is " + ag + " day" + (ag == 1 ? "" : "s") + " off roast.";
    if (ag < 5) age += " Very fresh coffee is still giving off gas: expect shots to vary for a few days, and settings found now to need revisiting.";
    if (ds >= 3) {
      age += " It is " + ds + " days since a shot of it last counted here. Coffee runs faster as it ages, so the grind usually has to go finer.";
      if (sh) { age += " From what contributors to the shared library have measured, expect about " + D(sh) + " " + U + " " + (sh < 0 ? "finer" : "coarser") + " than last time" + (G.current_um ? " (around " + F(G.current_um + sh) + ")" : "") + ". A guide only: the advice above follows from your next shot."; ageShort += ", " + ds + " days since last brewed: about " + D(sh) + " " + U + " " + (sh < 0 ? "finer" : "coarser") + " expected"; }
      else { age += G.age_rate_known ? " Not enough is known about this grinder to put a number on it." : " The shared library has not measured how fast yet, so no number is offered: the advice above follows from your next shot."; ageShort += ", " + ds + " days since last brewed"; }
    }
  }
  // ratings
  let rated = "";
  if (G.coffee && G.rated_target_on) {
    const n = G.rated_shots, rs = G.rated_state, T = f1(G.rated_time_s), tol = G.tolerance_s;
    if (rs == "on_liked") rated = "Your " + n + " best-rated shots (4 or 5 stars) took about " + T + " s, and the average is on that time (within ± " + f1(G.rated_band_s) + " s).";
    else if (rs == "refining") rated = "The time advice says keep, but your " + n + " best-rated shots took about " + T + " s and the average is " + f1(G.average_time_s) + " s, so a small move towards the time you liked is suggested. It stays inside the ± " + tol + " s the time advice allows.";
    else if (rs == "base_moving") rated = "Your " + n + " best-rated shots took about " + T + " s. Ratings only refine the advice once the time advice says keep.";
    else if (rs == "outside_band") rated = "Your " + n + " best-rated shots took about " + T + " s, outside the ± " + tol + " s around the recipe's " + f1(G.target_time_s) + " s, so the time advice stands. If " + T + " s is what you want, change it in the recipe.";
    else if (n > 0) rated = n + " of the 3 shots rated 4 or 5 stars that are needed before ratings can refine the advice.";
  }
  if (G.taste_hint && G.coffee) {
    const sour = G.taste_hint < 0, agrees = (G.action == "finer" && sour) || (G.action == "coarser" && !sour);
    rated += (rated ? " " : "") + "The last " + G.taste_shots + " shots at this setting tasted " + (sour ? "sour" : "bitter") + ". " + (G.action == "hold" ? "The time is on target, so try a little " + (sour ? "finer" : "coarser") + " only if you agree." : agrees ? "That agrees with the advice." : "That disagrees with the time, so something other than the grind may be the cause: dose, distribution, temperature, or the age of the coffee.");
  }
  const sensitivity = "Sensitivity: " + (G.sensitivity_source === "measured" ? "measured, " : G.sensitivity_source === "grinder" ? "from this grinder, " : "starting guess, ") + fdf(g, G.um_per_s) + " " + U + " per second" + (G.between_steps ? "; the closer of two steps is kept" : "");
  return { title, cls, short, why, apply, age, ageShort, rated, sensitivity, current: fsn(g, G.current_um) || "--" };
}

/** New grind recommendation banner text, or null (WebPage.h:1383-1386). */
export function recommendationText(G, g) {
  const r = G && G.recommendation;
  if (!r) return null;
  const why = r.source == "community" ? "from the community (" + r.people + " " + (r.people == 1 ? "person" : "people") + ")" : r.source == "similar" ? "from your similar coffee " + r.from : "a first guess from your grinder's range";
  return {
    text: `New grind recommendation for ${G.coffee || "this bag"}: change the grinder to ${fs(g, r.setting)} (± ${fdn(g, r.range)}), ${why}. ${r.current ? "It is at " + fs(g, r.current) + " now." : "No setting yet."} Grind advice takes over from the first shot.`,
    use: "Use " + fs(g, r.setting), keep: r.current ? "Keep " + fs(g, r.current) : "Not now",
  };
}

/** The pair in use has no grind setting: banner text, or null (WebPage.h:1380-1382). */
export function noGrindText(G, g) {
  if (!G || G.current_um || G.recommendation) return null;
  const last = G.last_um || 0;
  return { text: `No grind setting for ${G.coffee || "this recipe"}. Set it before you brew, so the shot counts for grind advice.` + (last ? " The last shot was at " + fs(g, last) + "." : ""), last };
}
