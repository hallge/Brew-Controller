// The power schedule's next switch-on and switch-off. The same rule as the integration's
// api.py next_power_events (tested there): `/api/power` schedule[] is {enabled, on, minute (local,
// 0..1439), days (bit 0 Sunday)}, and the controller's local time is UTC + utc_offset_min_now.

/** {next_on, next_off}: epoch seconds, or null. */
export function nextPowerEvents(power, nowEpoch = Math.floor(Date.now() / 1000)) {
  const out = { next_on: null, next_off: null };
  if (!power || !Array.isArray(power.schedule) || power.clock_set === false) return out;
  const offset = (power.utc_offset_min_now ?? power.utc_offset_min ?? 0) * 60;
  const localNow = nowEpoch + offset, dayStart = localNow - (localNow % 86400);
  for (const e of power.schedule) {
    if (!e || !e.enabled || e.minute == null || e.days == null) continue;
    const key = e.on === false ? "next_off" : "next_on";
    for (let k = 0; k < 8; k++) {
      const start = dayStart + k * 86400, weekday = (Math.floor(start / 86400) + 4) % 7; // 1 Jan 1970: Thursday
      const t = start + e.minute * 60;
      if (t > localNow && (e.days & (1 << weekday))) {
        const at = t - offset;
        if (out[key] == null || at < out[key]) out[key] = at;
        break;
      }
    }
  }
  return out;
}

const DAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];

/** "Mon–Fri", "every day", "Sat, Sun" for a days bitmask (bit 0 Sunday). */
export function daysText(days) {
  if ((days & 0x7f) === 0x7f) return "every day";
  if ((days & 0x7f) === 0b0111110) return "Mon–Fri";
  if ((days & 0x7f) === 0b1000001) return "weekends";
  return [1, 2, 3, 4, 5, 6, 0].filter((d) => days & (1 << d)).map((d) => DAYS[d]).join(", ") || "never";
}

/** "07:30" from minutes after midnight. */
export const hhmm = (minute) => String(Math.floor(minute / 60)).padStart(2, "0") + ":" + String(minute % 60).padStart(2, "0");

/** "today 06:30", "tomorrow 06:30", "Mon 06:30" for an epoch, in the browser's time. */
export function whenText(epoch) {
  if (!epoch) return "";
  const d = new Date(epoch * 1000), now = new Date();
  const day0 = (x) => new Date(x.getFullYear(), x.getMonth(), x.getDate()).getTime();
  const diff = Math.round((day0(d) - day0(now)) / 864e5);
  const t = d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  return (diff === 0 ? "today" : diff === 1 ? "tomorrow" : d.toLocaleDateString([], { weekday: "short" })) + " " + t;
}
