#pragma once
// One place that turns a ShotRecord into its export formats, so the serial
// log, the CSV download and the JSON API can never disagree.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>

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
         "region,varietal,grind_vote,coffee_id,recipe_id,bloom_pause_s,grinder_calibration\n";
}

inline size_t shotCsvLine(const ShotRecord &s, char *out, size_t size) {
  char stamp[24];
  isoTimestamp(s.epochS, stamp, sizeof(stamp));
  // Text fields are quoted: region and varietal contain commas. Double
  // quotes are removed from them when they are entered (web layer).
  return snprintf(out, size,
                  "%u,%s,\"%s\",%.1f,%.2f,%.2f,%.2f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f,%u,%s,%d,%d,%d,%d,"
                  "%.2f,%.2f,%u,%.1f,%.1f,%.2f,%.3f,%s,\"%s\",%s,%u,%.1f,%u,%s,"
                  "%.2f,%.2f,%.2f,%.2f,%.3f,\"%s\",\"%s\",%s,%u,%u,%.1f,%u\n",
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
                  s.bloomPauseMs / 1000.0f, static_cast<unsigned>(s.grinderId));
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

}  // namespace bbw
