# Barista-assistant features: what is viable on this controller

A review of fifteen candidate features against what this hardware can
actually observe. Read the first section before the table: almost every
limit below comes from it.

## What the controller can and cannot see

**It sees one signal:** weight in the cup against time, about ten readings a
second at 0.1 g resolution, from the moment the machine is started. Everything
else is derived from that (flow, first drops, pour time) or typed in by you
(dose, grind setting, coffee, roast date, rating).

**It does not see:** pressure, water temperature, pump flow into the puck,
the puck itself, or what the coffee tastes like. Machines that diagnose shots
well (Decent DE1, Gaggiuino builds) do it with pressure and pump-flow
sensors. Decent's own documentation notes that even a flow meter struggles
because flow "changes drastically and instantly" when a channel forms.

**Two consequences:**

1. Cup-side flow lags and smooths what happens at the puck. A channel that
   opens for a second shows up as a modest, late bump in cup flow, not a spike.
2. With only weight, time and the grind setting, several different causes
   produce the same curve. A fast shot can be a coarse grind, a low dose,
   stale coffee, a channel, or a cooler machine. The controller can narrow it
   down; it cannot settle it.

So the honest framing for every "intelligence" feature is: **flag what is
unusual compared with your own recent shots, say which explanations fit, and
let you decide.** Anything that claims more would be guessing.

## Summary

| # | Feature | Status | Verdict |
| --- | --- | --- | --- |
| 1 | Smart dial-in | Built | Works now; amount improves as history builds |
| 2 | Shot assessment | Partly built | Viable as a comparison with your own baseline |
| 3 | Flow-rate analysis | Partly built | Curve features now stored per shot; nothing uses them yet |
| 4 | Time to first drip | Built | Works; worth tracking as its own trend |
| 5 | Puck-prep consistency | Not built | Viable as a consistency score, not a diagnosis |
| 6 | Channeling flagging | Not built | Viable only as "abnormal flow" flag; needs your own calibration shots |
| 7 | Grind vs puck-prep | Partly built | One-off shots are left out of grind advice; cannot prove puck prep |
| 8 | Grind-setting history | Built | Done |
| 9 | Grind vs performance | Partly built | Viable; needs a chart, the data is there |
| 10 | Bean tracking | Built | Coffee stored per shot; grind advice is per coffee and roast date |
| 11 | Recipe management | Built | Done, except temperature (cannot be read; can be a note) |
| 12 | Shot history | Built | Summary kept for 500 shots; full curves only for the last shot |
| 13 | Taste rating | Partly built | Rating is stored; nothing uses it yet |
| 14 | Maintenance intelligence | Not built | Viable for shot counters; weak for diagnosis |
| 15 | Maintenance anomalies | Not built | Viable as "slow drift" detection that suppresses grind advice |

## Feature by feature

### 1. Smart dial-in
**Now:** after each shot the device says `BREW FAST  GRIND FINER`,
`BREW SLOW  GRIND COARSER` or `BREW TIME OK  KEEP GRIND`, and the web page
gives microns and the reasoning. Method and sources: `RESEARCH.md` section 8.

**Limit:** the micron amount starts from a guess (3 um per second) until you
have shots at two grind settings for the same recipe; then it is measured.

**Next:** split the measurement by coffee (see 10), and hold advice back when
a shot is flagged as abnormal (see 6, 7).

### 2. Shot intelligence / shot assessment
**Viable as:** a short verdict built from independent checks, each against
your own recent shots of the same recipe and coffee:

- yield: over / under target (have it)
- time: fast / slow / in tolerance (have it)
- first drip: early / late / normal (data there, comparison not built)
- flow shape: steady, accelerating, or erratic (needs 3)
- stop: confirmed, retried, by hand (have it)

**Not viable:** a single quality score. Without taste or pressure, a score
would only restate "close to target time and weight". If you rate shots
(13), a score can later be fitted to *your* ratings.

### 3. Flow-rate analysis
**Now:** live flow, peak flow, average flow over the pour. The full
weight curve exists only for the most recent shot, in memory.

**Next:** compute and store per shot, at the moment it finishes:

- flow at 25 %, 50 % and 75 % of the yield
- acceleration: late flow divided by early flow (a typical shot speeds up as
  the puck erodes; how much is a useful fingerprint)
- roughness: how far the flow curve strays from a smooth fit

That is about 20 bytes per shot and keeps history small. Storing whole curves
for 500 shots would need roughly 1 MB of the 4 MB filesystem: possible, but
only worth it if you want to re-analyse old shots.

**Limit:** cup flow is not puck flow. Drips, stream break-up and the cup's
position all add noise. Flow must be taken over about a second, so features
shorter than that are invisible.

### 4. Time-to-first-drip
**Now:** stored for every shot (`first_drop_s`), defined as two consecutive
readings of 0.3 g or more. Pour time is derived from it.

**Limit worth knowing:** on a scale, "first drip" really means "first 0.3 g
landed". Forum testing of first-drip timers on scales shows the detection
point wanders with how the first drops fall. Expect about half a second of
scatter that has nothing to do with the puck.

**Next:** show it as a trend per recipe and coffee, and use it in 5, 7 and 15.
With hold-the-button pre-infusion the first-drip time is partly set by the
hold, so shots are only compared with others using the same pre-infusion.

### 5. Puck-prep consistency detection
**Viable as:** a consistency measure, not a diagnosis. For shots with the
same recipe, coffee, grind and pre-infusion, first-drip time and brew time
should repeat. Their spread shot to shot is mostly puck preparation (dose
accuracy, distribution, tamp) plus coffee ageing.

- Report it as a number you can watch fall: "last 10 shots: first drip
  +/- 0.4 s, brew time +/- 1.1 s".
- Flag a single shot that lands far outside that spread.

**Not viable:** saying *what* was wrong with the puck.

### 6. Channeling detection / flagging
I found no published method for detecting channeling from cup weight alone,
and no thresholds. What a scale can plausibly catch:

- flow that jumps and stays high part-way through, beyond the shot's normal
  acceleration
- a shot far faster than the previous ones with no grind change
- very early first drops followed by fast flow

**Viable as:** an "abnormal flow" flag, worded as such: "flow rose sharply
at 18 s; possible channel or uneven puck". It will miss channels that do not
change total flow much, and it will sometimes fire on a clean shot.

**Needs before it is trustworthy:** a handful of deliberately bad pucks
(uneven tamp, a poked hole) watched with a bottomless portafilter, to set the
thresholds on your machine. Until then it should be off or clearly labelled
experimental.

### 7. Distinguishing grind problems from puck-prep problems
The useful half of this is achievable: **do not blame the grind for a
one-off.** Rules that follow from the data:

- One odd shot among normal ones at the same setting: treat as puck prep or
  an outlier; leave it out of the grind average. (Mahlkoenig's Grind-by-Sync
  has no such rule, and Prima Coffee's review notes it overcompensates after
  a channelled shot.)
- A shift that persists across three or more shots: grind, or the coffee
  ageing. Recommend a grind change.
- A slow drift over many days with one coffee: ageing. Expected; follow it
  with the grind.
- A shift that appears across *different* coffees at once: not the grind.
  Points at the machine or the grinder (see 15).

**Not viable:** positively identifying puck prep as the cause. The
controller can only say the grind is unlikely to be it.

### 8. Grind-setting history
**Done.** Every shot stores the recipe's grind setting; it can be corrected
afterwards in the history table and is in the CSV and JSON.

### 9. Grind setting vs shot performance
**Now:** the advisor fits brew time against grind setting internally to get
microns per second.

**Next:** show it. A scatter of brew time against grind setting per recipe
and coffee, with the fitted line and the target time marked, makes the next
setting obvious and shows when the fit is poor. All data is already stored;
this is a web-page chart.

### 10. Bean / coffee tracking
**Now:** each recipe carries a coffee name, roast level and roast date, and
every shot stores a copy of them, so history stays correct when a recipe is
edited.

**Missing:** the grind advisor still groups by recipe, target and dose, not
by coffee. Changing the coffee on a recipe therefore mixes two beans in one
average and one sensitivity fit.

**Next:** group by coffee and roast date as well, so a new bag starts a clean
comparison automatically. Also worth adding: days since roast on each shot's
row, since brew time against days-off-roast is the ageing curve.

### 11. Recipe management
**Done:** up to 16 recipes with dose, target or ratio, grind setting, target
brew time, pre-infusion (the machine's own, or holding the button for a set
time), coffee, roast level and date, maximum shot time and stop offset.
Selected from a scrolling list on the device or the web page.

**Temperature:** the Breville's brew temperature is set on its own panel and
cannot be read or changed from here. It can be added as a note field so the
recipe reminds you what to set, nothing more.

### 12. Shot history
**Done** for the summary: 500 shots in internal flash with every measured
and entered value, as CSV and JSON. The weight curve is kept only for the
latest shot. See 3 for the choice between stored curve features and whole
curves.

### 13. Taste / result rating
**Now:** each shot can be given 1 to 5 and sour / balanced / bitter from the
history table. It is stored and exported. Nothing acts on it.

**Next, and the most valuable step on this list:** this is the only route to
knowing whether a shot was *good* rather than merely on target.

- Per coffee, show which brew time and yield you rated highest, and offer it
  as the recipe's target time.
- Sour at target time suggests going finer or longer; bitter, coarser or
  shorter. This is standard dial-in guidance, and here it can be tied to
  your own ratings instead of a generic time.
- Rating stays in the web history table only; it is not offered on the
  device.

**Limit:** it needs perhaps ten rated shots per coffee before it says
anything worth reading.

### 14. Maintenance intelligence
**Viable, and simple:** counters and reminders, which need no inference.

- shots since the last backflush, last descale, last grinder clean, with a
  "done" button for each
- total shots on the Bot's battery and on the machine

I could not confirm Breville's own figures (owners report the CLEAN ME
prompt at about 200 shots; Breville's guide only says the display will ask).
The intervals should be yours to set.

**Weak:** inferring that maintenance is due from shot behaviour. See 15.

### 15. Maintenance-related anomaly detection
**Viable as drift detection.** With grind, dose and coffee recorded, the
controller can notice a change that the recorded inputs do not explain:

- brew time or first-drip time drifting steadily over weeks **across
  coffees** at unchanged settings
- shot-to-shot spread widening over time
- drip after the stop growing (the learned stop offset creeping up is a free
  indicator; it reflects valve and puck behaviour)

**What it can say:** "shots have slowed by about 3 s over six weeks on all
coffees with no grind change. Possible causes: scale in the machine, blocked
shower screen, worn burrs. Grind advice paused." That last part matters:
it is what stops every change being treated as a grind problem.

**What it cannot say:** which of those it is. Scale, a blocked screen, a
tired pump and dull burrs all look alike from the cup.

**Needs:** months of history and several coffees before it means anything.
It is cheap to add early and let it stay quiet.

## Suggested order

1. **Group grind advice by coffee** (10). Small change; removes a real source
   of wrong advice as soon as you change beans.
2. **Per-shot flow features and first-drip trend** (3, 4). Everything in 5,
   6, 7 and 15 depends on having them in history, so the sooner they are
   recorded the sooner there is data.
3. **Outlier rule in the grind average** (7). Uses the above; stops one bad
   puck moving the grind.
4. **Rating-based targets** (13), from ratings entered in the history table.
5. **Grind chart and consistency numbers on the web page** (5, 9).
6. **Maintenance counters** (14), then drift detection (15).
7. **Channeling flag** (6), last, and only after calibration shots on the
   real machine.

Items 1 to 3 change the stored shot record. Doing them together means one
more history reset rather than three.

## Sources

- Decent Espresso, "How the DE1 measures" (flow estimation, behaviour when a
  channel forms)
- Home-Barista, "Accuracy of first drip scales" (scatter of scale-based
  first-drip timing)
- Prima Coffee, "Mahlkonig Grind-by-Sync first impressions" (overcorrection
  after channelling; purge after large changes)
- Mahlkoenig Grind-by-Sync software manual and Sync Scale manual (valid brew
  events, tolerance, moving average)
- Breville BES920 manual (MANUAL button, pre-infusion by holding) and
  Breville's descale guide; Home-Barista owner threads for the CLEAN ME count
