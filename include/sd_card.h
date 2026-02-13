#ifndef WRAPPER_H
#define WRAPPER_H

// ─────────────────────────────────────────────────────────────────────────────
//  FaceGuard Pro  –  sd_card.h
//  SD / persistence layer built on greiman/SdFat (v2) instead of Arduino
//  SD_MMC, giving full long-filename (LFN) support on FAT32 / exFAT cards.
// ─────────────────────────────────────────────────────────────────────────────

#include "fr_forward.h"
#include <SdFat.h>          // greiman/SdFat v2 – long-filename FAT32/exFAT
#include <ArduinoJson.h>
#include "global.h"         // UserRecord, AttendanceRecord, EnrollContext, …

namespace Bridge {

    // ── Directory helper ────────────────────────────────────────────────────
    // NOTE: fs::FS parameter removed – SdFat manages the volume internally.
    void listDir(const char *dirname, uint8_t levels);

    // ── SD initialisation (creates /db, /atd, /cfg, /db/users.txt if missing)
    void initSD();

    // ── Face-ID binary persistence ──────────────────────────────────────────
    void read_face_id_name_list_sdcard(face_id_name_list *l, const char *path);
    void write_face_id_name_list_sdcard(face_id_name_list *l, const char *path);

    // ── User database (JSON array in /db/users.txt) ──────────────────────────
    // saveUserToDB now takes a single UserRecord struct instead of 4 Strings.
    bool   saveUserToDB(const UserRecord &user);
    bool   deleteUserFromDB(const char *name);
    String getUsersJSON();      // full JSON array
    int    getUserCount();      // fast count without full parse

    // ── Attendance logging ────────────────────────────────────────────────────
    // Both functions now accept an AttendanceRecord struct.
    // logAttendance  – automatic (face-rec path); date/time/status auto-filled.
    // manualAttendance – admin override; date/time/status taken from rec fields.
    void logAttendance(const AttendanceRecord &rec);
    bool manualAttendance(const AttendanceRecord &rec);

    // ── Attendance queries ───────────────────────────────────────────────────
    // getLogsJSON – filtered; any param can be "" to skip that filter.
    String getLogsJSON(String date, String dept,
                       String status, String search);

    // getLogsRange – returns per-day summary for chart rendering.
    // JSON: { "labels":["Mon",...], "present":[n,...], "absent":[n,...],
    //         "late":[n,...], "total": N, "userCount": N }
    String getLogsRange(int days);

    // Legacy helpers (kept for backwards-compat with old HTTP handlers)
    String getAttendanceLogs();                      // today's logs, no filter
    String downloadAttendanceCSV(String date = "");  // raw CSV string
    bool   clearAttendanceLogs(String date = "");    // delete log file

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
