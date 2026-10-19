#pragma once
// One place that turns a ShotRecord into its export formats, so the serial
// log, the CSV download and the JSON API can never disagree.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>

#include "core/Community.h"
#include "core/GrindAdvisor.h"
#include "core/ShotContext.h"
#include "core/Types.h"

namespace bbw {

inline void isoTimestamp(uint32_t epochS, char *out, size_t size) {
  if (epochS == 0) {
    out[0] = '\0';
    return;
  }
  const time_t t = static_cast<time_t>(epochS);
  struct tm tmUtc;
  gmtime_r(&t, &tmUtc);
  strftime(out, size, "%Y-%m-%dT%H:%M:%SZ", &tmUtc);
}

inline const char *shotCsvHeader() {
  return "id,timestamp,recipe,target_weight_g,stop_weight_g,final_weight_g,shot_time_s,"
         "average_flow_g_s,peak_flow_g_s,drip_weight_g,stop_offset_g,next_stop_offset_g,"
         "first_drop_s,stop_latency_ms,end_reason,final_valid,learned,stop_confirmed,"
         "stop_retried,learning_factor,next_learning_factor,grind_um,dose_g,target_time_s,"
         "pour_time_s,pour_flow_g_s,time_basis,"
         "coffee,roast_level,roast_date,preinfusion_hold_s,rating,taste,"
         "flow_25_g_s,flow_50_g_s,flow_75_g_s,flow_acceleration,flow_roughness_g_s,"
         "region,varietal,grind_vote,coffee_id,recipe_id,bloom_pause_s,grinder_calibration,tds_pct,extraction_pct\n";
}

inline size_t shotCsvLine(const ShotRecord &s, char *out, size_t size) {
  char stamp[24], tds[12] = "", ey[12] = "";
  isoTimestamp(s.epochS, stamp, sizeof(stamp));
  if (s.tdsX100) snprintf(tds, sizeof(tds), "%.2f", s.tdsX100 / 100.0f);
  if (shotExtractionPct(s) > 0.0f) snprintf(ey, sizeof(ey), "%.2f", shotExtractionPct(s));
  // Text fields are quoted: region and varietal contain commas. Double
  // quotes are removed from them when they are entered (web layer).
  return snprintf(out, size,
                  "%u,%s,\"%s\",%.1f,%.2f,%.2f,%.2f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f,%u,%s,%d,%d,%d,%d,"
                  "%.2f,%.2f,%u,%.1f,%.1f,%.2f,%.3f,%s,\"%s\",%s,%u,%.1f,%u,%s,"
                  "%.2f,%.2f,%.2f,%.2f,%.3f,\"%s\",\"%s\",%s,%u,%u,%.1f,%u,%s,%s\n",
                  static_cast<unsigned>(s.id), stamp, s.recipe, s.targetWeightG, s.stopWeightG,
                  s.finalWeightG, s.shotTimeS, s.averageFlowGS, s.peakFlowGS, s.dripWeightG,
                  s.stopOffsetG, s.nextStopOffsetG, s.firstDropS,
                  static_cast<unsigned>(s.stopLatencyMs),
                  endReasonName(static_cast<EndReason>(s.endReason)),
                  (s.flags & SHOT_FLAG_FINAL_VALID) != 0, (s.flags & SHOT_FLAG_LEARNED) != 0,
                  (s.flags & SHOT_FLAG_STOP_CONFIRMED) != 0,
                  (s.flags & SHOT_FLAG_STOP_RETRIED) != 0, s.alphaUsed / 100.0f,
                  s.alphaNext / 100.0f, static_cast<unsigned>(s.grindUm), s.doseG,
                  s.targetTimeS, s.pourTimeS, s.pourFlowGS, s.timeBasis ? "pour" : "total",
                  s.coffee, roastLevelName(s.roastLevel), static_cast<unsigned>(s.roastDate),
                  s.preinfusionHoldMs / 1000.0f, static_cast<unsigned>(s.rating),
                  tasteName(s.taste), s.flow25GS, s.flow50GS, s.flow75GS, s.flowAcceleration,
                  s.flowRoughnessGS, s.region, s.varietal, grindVoteName(s.grindVote),
                  static_cast<unsigned>(s.coffeeId), static_cast<unsigned>(s.recipeId),
                  s.bloomPauseMs / 1000.0f, static_cast<unsigned>(s.grinderId), tds, ey);
}

inline float round2(float v) { return roundf(v * 100.0f) / 100.0f; }

inline void shotToJson(const ShotRecord &s, JsonObject o) {
  char stamp[24];
  isoTimestamp(s.epochS, stamp, sizeof(stamp));
  o["id"] = s.id;
  o["timestamp"] = stamp;
  o["recipe"] = s.recipe;
  o["target_weight_g"] = round2(s.targetWeightG);
  o["stop_weight_g"] = round2(s.stopWeightG);
  o["final_weight_g"] = round2(s.finalWeightG);
  o["shot_time_s"] = round2(s.shotTimeS);
  o["average_flow_g_s"] = round2(s.averageFlowGS);
  o["peak_flow_g_s"] = round2(s.peakFlowGS);
  o["drip_weight_g"] = round2(s.dripWeightG);
  o["stop_offset_g"] = round2(s.stopOffsetG);
  o["next_stop_offset_g"] = round2(s.nextStopOffsetG);
  o["first_drop_s"] = round2(s.firstDropS);
  o["stop_latency_ms"] = s.stopLatencyMs;
  o["end_reason"] = endReasonName(static_cast<EndReason>(s.endReason));
  o["final_valid"] = (s.flags & SHOT_FLAG_FINAL_VALID) != 0;
  o["learned"] = (s.flags & SHOT_FLAG_LEARNED) != 0;
  o["stop_confirmed"] = (s.flags & SHOT_FLAG_STOP_CONFIRMED) != 0;
  o["stop_retried"] = (s.flags & SHOT_FLAG_STOP_RETRIED) != 0;
  o["learning_factor"] = s.alphaUsed / 100.0f;
  o["next_learning_factor"] = s.alphaNext / 100.0f;
  o["grind_um"] = s.grindUm;
  o["dose_g"] = round2(s.doseG);
  o["target_time_s"] = round2(s.targetTimeS);
  o["pour_time_s"] = round2(s.pourTimeS);
  o["pour_flow_g_s"] = round2(s.pourFlowGS);
  o["time_basis"] = s.timeBasis ? "pour" : "total";
  o["coffee"] = s.coffee;
  o["region"] = s.region;
  o["varietal"] = s.varietal;
  o["roast_level"] = roastLevelName(s.roastLevel);
  o["roast_date"] = s.roastDate;
  o["preinfusion_hold_s"] = s.preinfusionHoldMs / 1000.0f;
  o["rating"] = s.rating;
  o["taste"] = tasteName(s.taste);
  if (s.tdsX100) o["tds_pct"] = round2(s.tdsX100 / 100.0f);
  if (shotExtractionPct(s) > 0.0f) o["extraction_pct"] = round2(shotExtractionPct(s));
  o["grind_vote"] = grindVoteName(s.grindVote);
  o["coffee_id"] = s.coffeeId;
  o["recipe_id"] = s.recipeId;
  o["bloom_pause_s"] = s.bloomPauseMs / 1000.0f;
  o["grinder_calibration"] = s.grinderId;
  o["stop_started_pump"] = (s.flags & SHOT_FLAG_STOP_STARTED_PUMP) != 0;
  o["flow_25_g_s"] = round2(s.flow25GS);
  o["flow_50_g_s"] = round2(s.flow50GS);
  o["flow_75_g_s"] = round2(s.flow75GS);
  o["flow_acceleration"] = round2(s.flowAcceleration);
  o["flow_roughness_g_s"] = round2(s.flowRoughnessGS);
}

// The circumstances of a shot (core/ShotContext.h), for the learning work. Zero / -1 mean "not known".
inline void shotContextToJson(const ShotContext &c, JsonObject o) {
  o["shot_id"] = c.shotId;
  o["engine_version"] = c.engineVersion;
  o["age_days"] = c.ageDays;                 // effective: frozen time weighted
  o["calendar_days"] = c.calendarDays;       // since roast, frozen time counted in full
  o["frozen_days"] = c.frozenDays;
  o["bean_frozen"] = (c.beanFlags & CTX_BEAN_FROZEN) != 0;
  o["since_last_shot_s"] = c.sinceLastShotS;
  o["machine_on_s"] = c.machineOnS;
  o["grinder_clean_shots"] = c.grinderCleanShots;
  o["start_setting"] = c.startSetting;
  o["start_range"] = c.startRange;
  o["start_basis"] = startBasisName(static_cast<StartBasis>(c.startBasis));
  o["similar_setting"] = c.similarSetting;
  o["similar_range"] = c.similarRange;
  o["similar_fitted"] = c.similarFitted != 0;
  o["similar_samples"] = c.similarSamples;
  o["advice_um"] = c.adviceUm;
  o["advice_delta_um"] = c.adviceDeltaUm;
  o["advice_um_per_s"] = c.adviceUmPerSx100 / 100.0f;
  o["advice_action"] = grindActionName(static_cast<GrindAction>(c.adviceAction));
  o["advice_base_action"] = grindActionName(static_cast<GrindAction>(c.adviceBaseAction));
  o["advice_shots"] = c.adviceShots;
  o["advice_learn_shots"] = c.adviceLearnShots;
  o["advice_learned"] = (c.adviceFlags & 1) != 0;
  o["advice_step_limited"] = (c.adviceFlags & 2) != 0;
  o["advice_refined"] = (c.adviceFlags & 4) != 0;
  o["sample_interval_ms"] = c.sampleIntervalMs;
  o["curve_points"] = c.curvePoints;
  o["weight_step_g"] = c.weightStepCg / 100.0f;
  o["tolerance_s"] = c.toleranceDs / 10.0f;
  o["window"] = c.window;
  o["max_step_um"] = c.maxStepUm;
  o["default_um_per_s"] = c.defaultUmPerSx100 / 100.0f;
  o["rated_target"] = c.ratedTarget != 0;
  o["learn_grinder"] = c.learnGrinder != 0;
  o["dose_actual_g"] = c.doseActualDg / 10.0f;
  o["prep_flags"] = c.prepFlags;
  o["purged"] = (c.prepFlags & CTX_PREP_PURGED) != 0;
  o["uneven"] = (c.prepFlags & CTX_PREP_UNEVEN) != 0;
}

}  // namespace bbw
