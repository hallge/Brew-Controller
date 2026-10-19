// Run: .venv/Scripts/python.exe -m ziglang c++ -std=c++17 -O1 -Isrc research/validation/settle_sim_start_error.cpp -o sim.exe  (from the project root)
// Simulation: how many shots does the REAL adviseGrind need to settle, as a function of how far
// the first-shot setting starts from the right one? Response model is synthetic (stated in the
// research doc): time = target - (um - idealUm) / sTrue + noise.
#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>
#include "core/GrindAdvisor.h"
using namespace bbw;

struct Result { int shotsToHold; int outside; };

static Result run(std::mt19937 &rng, float startErrUm, float sTrue, float noiseSd, float target,
                  const GrindConfig &cfg, float idealUm) {
  std::normal_distribution<float> n(0.0f, noiseSd);
  std::vector<GrindShot> hist;  // newest first
  float current = idealUm + startErrUm;
  int outside = 0;
  for (int shot = 1; shot <= 20; ++shot) {
    float t = target - (current - idealUm) / sTrue + n(rng);
    GrindShot g;
    g.timeS = t;
    g.grindUm = static_cast<uint16_t>(current + 0.5f);
    hist.insert(hist.begin(), g);
    if (t < 15.0f) t = 15.0f;  // flush floor, not modelled further
    GrindAdvice a = adviseGrind(hist.data(), hist.size(), g.grindUm, target, cfg);
    if (std::fabs(t - target) > cfg.toleranceS) ++outside;
    if (a.action == GrindAction::HOLD) return {shot, outside};
    if (a.action == GrindAction::FINER || a.action == GrindAction::COARSER) current = a.suggestedUm;
  }
  return {20, outside};
}

int main() {
  GrindConfig cfg;  // library defaults: tol 2 s, window 3, step cap 20 um, 3 um/s
  const float target = 28.0f, ideal = 400.0f;
  const float errs[] = {0, 5, 10, 15, 20, 30, 40, 60, 80, 120};
  const float sens[] = {1.5f, 3.0f, 6.0f};
  const float noises[] = {1.0f, 2.0f};
  printf("noiseSd,sTrue,startErrUm,medianShots,meanShots,p90Shots,meanOutsideTol\n");
  for (float nz : noises)
    for (float st : sens)
      for (float e : errs) {
        std::mt19937 rng(12345);
        std::vector<int> shots; double outsideSum = 0;
        for (int i = 0; i < 4000; ++i) {
          // undershoot and overshoot symmetric
          float signedErr = (i % 2 ? e : -e);
          Result r = run(rng, signedErr, st, nz, target, cfg, ideal);
          shots.push_back(r.shotsToHold); outsideSum += r.outside;
        }
        std::sort(shots.begin(), shots.end());
        double mean = 0; for (int s : shots) mean += s; mean /= shots.size();
        printf("%.1f,%.1f,%.0f,%d,%.2f,%d,%.2f\n", nz, st, e, shots[shots.size() / 2], mean,
               shots[shots.size() * 9 / 10], outsideSum / shots.size());
      }
  return 0;
}
