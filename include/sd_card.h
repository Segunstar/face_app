#ifndef WRAPPER_H
#define WRAPPER_H

#include "fr_forward.h"
#include "FS.h"
#include "SD_MMC.h"
#include <ArduinoJson.h>
#include "global.h"

namespace Bridge {

    // ── Directory helper ────────────────────────────────────────────────────
    void listDir(fs::FS &fs, const char *dirname, uint8_t levels);

    // ── SD initialisation (creates /db, /atd, /db/users.txt if missing) ────
    void initSD();

    // ── Face-ID binary persistence ──────────────────────────────────────────
    void read_face_id_name_list_sdcard(face_id_name_list *l, const char *path);
    void write_face_id_name_list_sdcard(face_id_name_list *l, const char *path);

    // ── User database (JSON array in /db/users.txt) ─────────────────────────
    bool   saveUserToDB(String id, String name, String dept, String role = "Student");
    bool   deleteUserFromDB(String name);
    String getUsersJSON();      // full JSON array
    int    getUserCount();      // fast count without full parse

    // ── Attendance logging ──────────────────────────────────────────────────
    // date parameter format: "YYYY-MM-DD" (from NTP).  Empty = today.
    void   logAttendance(String uid, String name, String dept = "");
    bool   manualAttendance(String uid, String name, String date,
                            String time, String status, String notes = "");

    // ── Attendance queries ───────────────────────────────────────────────────
    // getLogsJSON – filtered; any param can be "" to skip that filter
    String getLogsJSON(String date, String dept,
                       String status, String search);

    // getLogsRange – returns per-day summary for chart rendering
    // JSON: { "labels":["Mon",...], "present":[n,...], "absent":[n,...],
    //         "late":[n,...], "total": N, "userCount": N }
    String getLogsRange(int days);

    // Legacy helpers (kept for backwards-compat with old handlers)
    String getAttendanceLogs();           // today's logs, no filter
    String downloadAttendanceCSV(String date = ""); // raw CSV
    bool   clearAttendanceLogs(String date = "");   // delete log file

    // ── Dashboard stats ─────────────────────────────────────────────────────
    // JSON: { "total":N, "present":N, "absent":N, "late":N,
    //         "storage":"X MB", "uptime":"Xh Ym",
    //         "ntpSynced":bool, "date":"YYYY-MM-DD", "time":"HH:MM" }
    String getStatsJSON();

    // ── Storage info ────────────────────────────────────────────────────────
    // JSON: { "total":"X MB", "used":"X MB", "free":"X MB", "pct": N }
    String getStorageJSON();

    // ── System status ───────────────────────────────────────────────────────
    // JSON: { "camera":bool, "wifi":bool, "model":bool,
    //         "faceCount":N, "ip":"x.x.x.x", "ssid":"name" }
    String getStatusJSON();

    // ── Settings ─────────────────────────────────────────────────────────────
    bool loadSettings(AttendanceSettings &s);
    bool saveSettings(const AttendanceSettings &s);

    // ── NTP helpers (call after WiFi connects) ───────────────────────────────
    void syncNTP();
    String getCurrentDateStr();   // "YYYY-MM-DD"
    String getCurrentTimeStr();   // "HH:MM:SS"
    String getCurrentHHMM();      // "HH:MM"  (for late-check comparisons)

} // namespace Bridge

#endif // WRAPPER_H
