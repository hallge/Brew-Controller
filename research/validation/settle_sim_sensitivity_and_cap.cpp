// Run as for settle_sim_start_error.cpp.
// Does the size of the starting sensitivity guess (um per second) matter, and would a guess
// scaled to the setting help? Uses the real adviseGrind. Synthetic response (power law):
//   time = target * (um / idealUm) ^ -eps + noise      (eps = how strongly time falls as the burrs open)
// The two regimes are chosen to mimic the two coffees in the owner's history (about 70 um and
// about 230 um); the same eps is used for both on purpose, so that any difference between regimes
// comes from the SCALE of the setting, which is what is being tested.
#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>
#include "core/GrindAdvisor.h"
using namespace bbw;

enum Variant { BASE, SCALED, SCALED_CAP };

static int run(std::mt19937 &rng, float startErrPct, float eps, float idealUm, float noiseSd,
               float target, Variant v, float epsAssumed, int &outside) {
  std::normal_distribution<float> n(0.0f, noiseSd);
  std::vector<GrindShot> hist;
  float current = idealUm * (1.0f + startErrPct / 100.0f);
  outside = 0;
  for (int shot = 1; shot <= 20; ++shot) {
    float t = target * std::pow(current / idealUm, -eps) + n(rng);
    GrindShot g; g.timeS = t; g.grindUm = static_cast<uint16_t>(current + 0.5f);
    hist.insert(hist.begin(), g);
    GrindConfig cfg;
    if (v != BASE) cfg.defaultUmPerS = g.grindUm / (epsAssumed * target);
    if (v == SCALED_CAP) {
      float cap = 0.10f * g.grindUm; if (cap < 20.0f) cap = 20.0f; if (cap > 250.0f) cap = 250.0f;
      cfg.maxStepUm = static_cast<uint8_t>(cap);
    }
    if (std::fabs(t - target) > cfg.toleranceS) ++outside;
    GrindAdvice a = adviseGrind(hist.data(), hist.size(), g.grindUm, target, cfg);
    if (a.action == GrindAction::HOLD) return shot;
    if (a.action == GrindAction::FINER || a.action == GrindAction::COARSER) current = a.suggestedUm;
  }
  return 20;
}

int main() {
  const float target = 28.0f, noise = 1.5f;
  const float ideals[] = {70.0f, 230.0f, 400.0f};
  const float pcts[] = {5, 10, 20, 30, 40};
  const float epsTrue[] = {0.9f, 0.6f, 1.3f};
  printf("idealUm,epsTrue,epsAssumed,startErrPct,variant,medianShots,meanShots,p90,meanOutsideTol\n");
  for (float ideal : ideals)
    for (float et : epsTrue)
      for (float pct : pcts)
        for (int vi = 0; vi < 3; ++vi) {
          std::mt19937 rng(777);
          std::vector<int> shots; double outsideSum = 0;
          for (int i = 0; i < 3000; ++i) {
            int out = 0;
            float signedPct = (i % 2 ? pct : -pct);
            shots.push_back(run(rng, signedPct, et, ideal, noise, target, static_cast<Variant>(vi), 0.9f, out));
            outsideSum += out;
          }
          std::sort(shots.begin(), shots.end());
          double mean = 0; for (int s : shots) mean += s; mean /= shots.size();
          const char *names[] = {"base", "scaled", "scaled+cap"};
          printf("%.0f,%.1f,0.9,%.0f,%s,%d,%.2f,%d,%.2f\n", ideal, et, pct, names[vi], shots[shots.size() / 2], mean,
                 shots[shots.size() * 9 / 10], outsideSum / shots.size());
        }
  return 0;
}
