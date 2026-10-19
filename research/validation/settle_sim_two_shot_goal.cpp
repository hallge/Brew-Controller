// Run (from the project root): .venv/Scripts/python.exe -m ziglang c++ -std=c++17 -O1 -Isrc research/validation/settle_sim_two_shot_goal.cpp -o sim3.exe
//
// Question (owner, 16 Oct 2026): the goal is "solid suggestion on cup zero, solid correction after
// cup one, dialled in on cup one's correction 75-80% of the time" (that is, the SECOND shot lands
// inside the tolerance). What start accuracy and what correction rule does that need, and what
// ceiling does shot-to-shot noise put on it?
//
// Uses the project's real adviseGrind. The grinder is SYNTHETIC:
//   time(x) = target * (x / xIdeal)^(-eps) + N(0, noise)        x = burr gap in um
//   eps varies per coffee: eps = epsMean * exp(N(0, epsCv))
//   the first-shot setting is xIdeal * exp(N(0, startSd))      (relative error of the suggestion)
// Success counts: P(shots to "keep" <= 1), <= 2, <= 3, and the mean.
//
// Rules compared (only the sensitivity used for the FIRST correction differs; everything after is the
// unchanged advisor):
//   fixed3   current default: 3 um per second of error
//   scaled   sensitivity proportional to the setting: x / (epsAssumed * target)
//   damped   scaled, times k = sp^2/(sp^2+noise^2), sp = the start's error in seconds: do not chase noise
//            when the start is good (Bayesian shrinkage of a single noisy shot toward the start)
//   oracle   exact sensitivity for this coffee (upper bound, not achievable)
//   damped_overconfident  damped, but the controller believes its start is twice as good as it is
//   damped+cap  damped, and the largest single move grows with the setting (10% of it, at least 20 um)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
#include "core/GrindAdvisor.h"
using namespace bbw;

enum Rule { FIXED3, SCALED, DAMPED, ORACLE, DAMPED_CAP, DAMPED_OVER };
static const char *RULE_NAMES[] = {"fixed3", "scaled", "damped", "oracle", "damped+cap", "damped_overconfident"};

static int run(std::mt19937 &rng, Rule rule, float xIdeal, float epsMean, float epsCv, float startSd,
               float noise, float target, float epsAssumed, float noiseAssumed) {
  std::normal_distribution<float> unit(0.0f, 1.0f);
  const float eps = epsMean * std::exp(epsCv * unit(rng));
  float current = xIdeal * std::exp(startSd * unit(rng));
  if (current < 5.0f) current = 5.0f;
  std::vector<GrindShot> hist;
  for (int shot = 1; shot <= 12; ++shot) {
    const float t = target * std::pow(current / xIdeal, -eps) + noise * unit(rng);
    GrindShot g;
    g.timeS = t;
    g.grindUm = static_cast<uint16_t>(current + 0.5f);
    hist.insert(hist.begin(), g);
    GrindConfig cfg;
    const float scaled = g.grindUm / (epsAssumed * target);
    switch (rule) {
      case FIXED3: cfg.defaultUmPerS = 3.0f; break;
      case SCALED: cfg.defaultUmPerS = scaled; break;
      case DAMPED_OVER:
      case DAMPED_CAP:
      case DAMPED: {
        float k = 1.0f;
        if (shot == 1) {
          const float claimed = rule == DAMPED_OVER ? 0.5f * startSd : startSd;
          const float sp = claimed * epsAssumed * target;  // the start's error, in seconds
          k = (sp * sp) / (sp * sp + noiseAssumed * noiseAssumed);
        }
        cfg.defaultUmPerS = scaled * k;
        break;
      }
      case ORACLE: cfg.defaultUmPerS = g.grindUm / (eps * target); break;
    }
    if (rule == DAMPED_CAP) {
      float cap = 0.10f * g.grindUm; if (cap < 20.0f) cap = 20.0f; if (cap > 250.0f) cap = 250.0f;
      cfg.maxStepUm = static_cast<uint8_t>(cap);
    }
    GrindAdvice a = adviseGrind(hist.data(), hist.size(), g.grindUm, target, cfg);
    if (a.action == GrindAction::HOLD) return shot;
    if (a.action == GrindAction::FINER || a.action == GrindAction::COARSER) current = a.suggestedUm;
  }
  return 12;
}

int main() {
  const float target = 28.0f;
  const float ideals[] = {70.0f, 230.0f};
  const float startSds[] = {0.05f, 0.10f, 0.15f, 0.25f, 0.40f};
  const float noises[] = {1.0f, 1.5f, 2.0f};
  const float cvs[] = {0.0f, 0.25f};
  printf("xIdeal,epsCv,noiseS,startSd,rule,P1,P2,P3,mean\n");
  for (float ideal : ideals)
    for (float cv : cvs)
      for (float nz : noises)
        for (float sd : startSds)
          for (int r = 0; r < 6; ++r) {
            std::mt19937 rng(4242);
            const int N = 6000;
            int c1 = 0, c2 = 0, c3 = 0;
            double sum = 0;
            for (int i = 0; i < N; ++i) {
              const int s = run(rng, static_cast<Rule>(r), ideal, 0.9f, cv, sd, nz, target, 0.9f, 1.5f);
              c1 += s <= 1; c2 += s <= 2; c3 += s <= 3; sum += s;
            }
            printf("%.0f,%.2f,%.1f,%.2f,%s,%.3f,%.3f,%.3f,%.2f\n", ideal, cv, nz, sd, RULE_NAMES[r],
                   double(c1) / N, double(c2) / N, double(c3) / N, sum / N);
          }
  return 0;
}
