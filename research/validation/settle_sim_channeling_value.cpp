// Run (from the project root): .venv/Scripts/python.exe -m ziglang c++ -std=c++17 -O1 -Isrc research/validation/settle_sim_channeling_value.cpp -o sim4.exe
//
// Question (owner, 16 Oct 2026): how much would detecting a channelled / badly prepared shot from the
// scale's flow help the two-shot goal?
//
// Same synthetic grinder as settle_sim_two_shot_goal.cpp (power law, noise, start error), plus:
//   with probability pBad a shot is "bad prep": it runs FAST by U(3, 8) s whatever the grind.
//   (the size and rate of this are ASSUMPTIONS; real values need real shots)
// Controller policies (the real adviseGrind underneath, damped first correction, step cap 10% of setting):
//   blind     treats every shot as a real reading of the grind
//   detect    a flag with recall r and false-alarm rate f; a flagged shot is left out of the advice and the
//             setting is NOT changed (the owner is told to improve the prep and pull again). It still costs a shot.
//   perfect   recall 1, false alarms 0 (bound)
// Reported: P(done by shot 2), P(done by shot 3), mean shots until a shot that is NOT bad-prep and is inside the
// tolerance, setting unchanged by a flag.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
#include "core/GrindAdvisor.h"
using namespace bbw;

static int run(std::mt19937 &rng, float xIdeal, float startSd, float noise, float pBad, float recall,
               float falseAlarm, bool useDetector, float target) {
  std::normal_distribution<float> unit(0.0f, 1.0f);
  std::uniform_real_distribution<float> u01(0.0f, 1.0f);
  const float eps = 0.9f * std::exp(0.25f * unit(rng));
  float current = xIdeal * std::exp(startSd * unit(rng));
  std::vector<GrindShot> hist;
  for (int shot = 1; shot <= 12; ++shot) {
    const bool bad = u01(rng) < pBad;
    float t = target * std::pow(current / xIdeal, -eps) + noise * unit(rng);
    if (bad) t -= 3.0f + 5.0f * u01(rng);
    const bool flagged = useDetector && (bad ? u01(rng) < recall : u01(rng) < falseAlarm);
    if (flagged) continue;  // left out; grinder unchanged; the shot is spent
    GrindShot g;
    g.timeS = t;
    g.grindUm = static_cast<uint16_t>(current + 0.5f);
    hist.insert(hist.begin(), g);
    GrindConfig cfg;
    float k = 1.0f;
    if (hist.size() == 1) {
      const float sp = startSd * 0.9f * target;
      k = (sp * sp) / (sp * sp + 1.5f * 1.5f);
    }
    cfg.defaultUmPerS = k * g.grindUm / (0.9f * target);
    float cap = 0.10f * g.grindUm; if (cap < 20.0f) cap = 20.0f; if (cap > 250.0f) cap = 250.0f;
    cfg.maxStepUm = static_cast<uint8_t>(cap);
    GrindAdvice a = adviseGrind(hist.data(), hist.size(), g.grindUm, target, cfg);
    if (a.action == GrindAction::HOLD && !bad) return shot;
    if (a.action == GrindAction::HOLD && bad) continue;  // a bad shot happened to land in tolerance: not a real dial-in
    if (a.action == GrindAction::FINER || a.action == GrindAction::COARSER) current = a.suggestedUm;
  }
  return 12;
}

int main() {
  const float target = 28.0f;
  struct Policy { const char *name; bool detect; float recall, fa; };
  const Policy pol[] = {{"blind", false, 0, 0}, {"detect r=0.5 f=0.05", true, 0.5f, 0.05f},
                        {"detect r=0.8 f=0.05", true, 0.8f, 0.05f}, {"detect r=0.8 f=0.15", true, 0.8f, 0.15f},
                        {"perfect", true, 1.0f, 0.0f}};
  printf("xIdeal,startSd,noiseS,pBad,policy,P2,P3,mean\n");
  for (float ideal : {70.0f, 230.0f})
    for (float sd : {0.10f, 0.15f, 0.25f})
      for (float pBad : {0.0f, 0.10f, 0.20f})
        for (const Policy &p : pol) {
          std::mt19937 rng(99);
          const int N = 6000;
          int c2 = 0, c3 = 0; double sum = 0;
          for (int i = 0; i < N; ++i) {
            const int s = run(rng, ideal, sd, 1.5f, pBad, p.recall, p.fa, p.detect, target);
            c2 += s <= 2; c3 += s <= 3; sum += s;
          }
          printf("%.0f,%.2f,1.5,%.2f,%s,%.3f,%.3f,%.2f\n", ideal, sd, pBad, p.name, double(c2) / N, double(c3) / N, sum / N);
        }
  return 0;
}
