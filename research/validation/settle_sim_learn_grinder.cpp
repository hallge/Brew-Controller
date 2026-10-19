// Run (from the project root): .venv/Scripts/python.exe -m ziglang c++ -std=c++17 -O1 -Isrc research/validation/settle_sim_learn_grinder.cpp -o sim5.exe
//
// Question (logic review, 16 Oct 2026): is the grind-adjustment logic reliable across grinders and over
// the life of many bags, and does "learn the grinder" (core/GrindAdvisor.h: unit-aware limits, weighed
// sensitivity, sensitivity pooled across coffees, no stepping past a known setting) make it more so?
//
// Uses the project's real adviseGrind / grindConfigForGrinder / pooledGrindElasticity. The grinder and
// the coffee are SYNTHETIC (assumptions, stated):
//   time = target * (setting / ideal)^(-eps) + N(0, noise)       eps: grinder's, varied per coffee (CV 15 %)
//   ideal drifts finer as the bag ages: ideal_day = ideal_0 * (1 - 0.004 * day)   (0.4 % a day)
//   two shots a day for 14 days per bag; 8 bags in a row; the owner always sets what the advice says
//   the first setting of each bag is the right one times exp(N(0, startSd))       (15 % or 35 % start error)
//   as on the controller, a shot under 15 s or more than 15 s over the target is not counted (GrindUse);
//   with "learn the grinder" such a shot still gets the largest step in its direction (adviseOutOfRange)
// Grinders:
//   E64-70    microns (1 um a step), espresso around 70 um
//   E64-230   microns, around 230 um (a coarser regime)
//   dial-15   a numbered dial read in whole numbers, around 15, microns per step NOT known
//   dial-8    the same, around 8: one step moves the time by more than the 2 s tolerance
//   dial-150  a dial read in tenths (150 stored steps = 15.0), microns NOT known
// Measured per bag: shots until the first shot inside the tolerance that the advice also keeps
// ("done"), share of all shots inside the tolerance, and overshoots (a move after which the true
// error changed sign and ended outside the tolerance).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
#include "core/GrindAdvisor.h"
using namespace bbw;

struct GrinderCase { const char *name; float ideal; float umPerStep; float epsTrue; };
struct Totals { double done = 0, done2 = 0, inTol = 0, shots = 0, overshoot = 0, moves = 0, bags = 0, neverDone = 0; };

static void runOwner(std::mt19937 &rng, const GrinderCase &g, bool learn, float noise, float startSd, Totals &t) {
  std::normal_distribution<float> unit(0.0f, 1.0f);
  const float target = 28.0f;
  std::vector<GrinderShot> pool;   // every valid shot so far on this grinder (all bags)
  for (int bag = 0; bag < 8; ++bag) {
    const float eps = g.epsTrue * std::exp(0.15f * unit(rng));
    const float ideal0 = g.ideal * std::exp(0.10f * unit(rng));   // coffees differ
    float setting = std::round(ideal0 * std::exp(startSd * unit(rng)));
    if (setting < 1) setting = 1;
    std::vector<GrindShot> hist;   // this bag, newest first
    int doneAt = 0;
    // What the grinder's other bags say (this bag's own shots are weighed inside the advisor).
    const GrinderElasticity el = learn ? pooledGrindElasticity(pool.data(), pool.size()) : GrinderElasticity{};
    for (int shot = 0; shot < 28; ++shot) {
      const float day = shot / 2;
      const float ideal = ideal0 * (1.0f - 0.004f * day);
      const float time = target * std::pow(setting / ideal, -eps) + noise * unit(rng);
      GrindShot gs;
      gs.timeS = time;
      gs.grindUm = static_cast<uint16_t>(setting);
      const bool tooShort = time <= 15.0f, tooLong = time >= target + 15.0f;
      if (!tooShort && !tooLong) hist.insert(hist.begin(), gs);
      t.shots += 1;
      const bool inTol = std::fabs(time - target) <= 2.0f;
      if (inTol) t.inTol += 1;
      GrindConfig base;
      const GrindConfig cfg = learn ? grindConfigForGrinder(base, g.umPerStep, gs.grindUm, target, el) : base;
      const size_t n = hist.size() < 40 ? hist.size() : 40;
      GrindAdvice a = adviseGrind(hist.data(), n, gs.grindUm, target, cfg);
      if (learn && (tooShort || tooLong)) adviseOutOfRange(a, tooLong ? GrindUse::TOO_LONG : GrindUse::TOO_SHORT, gs.grindUm, cfg);
      if (!doneAt && inTol && a.action == GrindAction::HOLD) doneAt = shot + 1;
      if (a.action == GrindAction::FINER || a.action == GrindAction::COARSER) {
        const float before = target * std::pow(setting / ideal, -eps) - target;
        const float next = a.suggestedUm;
        const float after = target * std::pow(next / ideal, -eps) - target;
        t.moves += 1;
        if (before * after < 0 && std::fabs(after) > 2.0f) t.overshoot += 1;
        setting = next;
      }
    }
    for (const GrindShot &h : hist) pool.push_back({h.timeS, h.grindUm, static_cast<uint16_t>(bag + 1)});
    t.bags += 1;
    if (doneAt) {
      t.done += doneAt;
      if (doneAt <= 2) t.done2 += 1;
    } else {
      t.neverDone += 1;
      t.done += 28;
    }
  }
}

int main() {
  const GrinderCase cases[] = {
      {"E64-70", 70.0f, 1.0f, 0.9f},  {"E64-230", 230.0f, 1.0f, 0.9f},
      {"dial-15", 15.0f, 0.0f, 0.9f}, {"dial-8", 8.0f, 0.0f, 0.9f},
      {"dial-150", 150.0f, 0.0f, 0.9f},
      {"E64-70 eps0.5", 70.0f, 1.0f, 0.5f}, {"E64-70 eps1.5", 70.0f, 1.0f, 1.5f},
  };
  printf("grinder,start_sd,noise,policy,mean_shots_to_done,P_done_by_2,never_done_pct,in_tolerance_pct,overshoot_per_move_pct\n");
  for (const GrinderCase &g : cases)
    for (float startSd : {0.15f, 0.35f})
    for (float noise : {1.0f, 1.5f, 2.0f})
      for (int learn = 0; learn < 2; ++learn) {
        std::mt19937 rng(2026);
        Totals t;
        for (int owner = 0; owner < 300; ++owner) runOwner(rng, g, learn != 0, noise, startSd, t);
        printf("%s,%.2f,%.1f,%s,%.2f,%.0f,%.1f,%.0f,%.1f\n", g.name, startSd, noise, learn ? "learn-grinder" : "current",
               t.done / t.bags, 100 * t.done2 / t.bags, 100 * t.neverDone / t.bags, 100 * t.inTol / t.shots,
               t.moves > 0 ? 100 * t.overshoot / t.moves : 0.0);
      }
  return 0;
}
