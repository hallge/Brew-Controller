// The phone's time zone as the POSIX rule the controller reads (core/TimeZone.h), a port of the web page's
// tzPosix(): this year's two clock changes, written as month.week.weekday (week 5: the last one in the month).
// The controller's power schedule follows daylight saving with it. Sent once per app start with
// POST /api/time (see MachineStore.syncClock).
import Foundation

enum MachineTimeZone {
    /// e.g. "<-05>5<-04>4,M3.2.0,M11.1.0"; a fixed offset such as "<+01>-1" when there is no daylight saving.
    static func posix(zone: TimeZone = .current, year: Int? = nil) -> String {
        var utc = Calendar(identifier: .gregorian)
        utc.timeZone = TimeZone(secondsFromGMT: 0) ?? .gmt
        let y = year ?? utc.component(.year, from: Date())

        // Times in minutes since the epoch.
        func minutes(_ year: Int, _ month: Int, _ day: Int) -> Int {
            let date = utc.date(from: DateComponents(year: year, month: month, day: day)) ?? Date(timeIntervalSince1970: 0)
            return Int(date.timeIntervalSince1970 / 60)
        }
        func offset(_ t: Int) -> Int {
            zone.secondsFromGMT(for: Date(timeIntervalSince1970: Double(t) * 60)) / 60
        }
        func two(_ n: Int) -> String { n < 10 ? "0\(n)" : String(n) }
        func nm(_ m: Int) -> String {
            let a = abs(m)
            return "<" + (m < 0 ? "-" : "+") + two(a / 60) + (a % 60 != 0 ? two(a % 60) : "") + ">"
        }
        func po(_ m: Int) -> String {
            let a = abs(m)
            return (m > 0 ? "-" : "") + String(a / 60) + (a % 60 != 0 ? ":" + two(a % 60) : "")
        }

        let day = 1440
        let a = minutes(y, 1, 1)
        let b = minutes(y + 1, 1, 1)
        var changes: [(t: Int, from: Int, to: Int)] = []
        var previous = offset(a)
        var t = a + day
        while t <= b {
            let o = offset(t)
            if o != previous {
                var lo = t - day
                var hi = t
                while hi - lo > 1 {
                    let m = lo + (hi - lo) / 2
                    if offset(m) == previous { lo = m } else { hi = m }
                }
                changes.append((t: hi, from: previous, to: o))
                previous = o
            }
            t += day
        }
        if changes.count != 2 || changes[0].from != changes[1].to {
            let o = offset(minutes(y, 7, 1))
            return nm(o) + po(o)
        }
        let standard = min(changes[0].from, changes[0].to)
        let summer = max(changes[0].from, changes[0].to)

        func when(_ c: (t: Int, from: Int, to: Int)) -> String {
            let d = Date(timeIntervalSince1970: Double(c.t + c.from) * 60)
            let parts = utc.dateComponents([.month, .day, .weekday, .hour, .minute], from: d)
            let dayOfMonth = parts.day ?? 1
            let month = parts.month ?? 1
            let last = utc.range(of: .day, in: .month, for: d)?.count ?? 31
            let seconds = (parts.hour ?? 0) * 3600 + (parts.minute ?? 0) * 60
            let week = dayOfMonth + 7 > last ? 5 : (dayOfMonth + 6) / 7
            var text = ",M\(month).\(week).\((parts.weekday ?? 1) - 1)"
            if seconds != 7200 {
                text += "/" + String(seconds / 3600) + (seconds % 3600 != 0 ? ":" + two(seconds % 3600 / 60) : "")
            }
            return text
        }
        guard let start = changes.first(where: { $0.to == summer }), let end = changes.first(where: { $0.to == standard }) else {
            let o = offset(minutes(y, 7, 1))
            return nm(o) + po(o)
        }
        return nm(standard) + po(standard) + nm(summer) + (summer == standard + 60 ? "" : po(summer)) + when(start) + when(end)
    }
}
