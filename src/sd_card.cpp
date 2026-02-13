// sd_card.cpp  –  FaceGuard Pro  (ESP32-CAM)
// All SD / persistence / time functions.
// Uses greiman/SdFat v2 for full long-filename (LFN) support on FAT32/exFAT.

#include "fr_forward.h"

#undef min
#undef max
#include "Arduino.h"

// ── SdFat replaces Arduino SD_MMC + FS ───────────────────────────────────────
#include <SdFat.h>          // greiman/SdFat v2
#include <sdios.h>

#include "SPI.h"
#include <ArduinoJson.h>
#include <time.h>
#include <WiFi.h>
#include "global.h"
#include "sd_card.h"

// ═══════════════════════════════════════════════════════════════════════════════
//  Module-level state
// ═══════════════════════════════════════════════════════════════════════════════
static bool    _sdOk      = false;
static time_t  _bootEpoch = 0;   // epoch at first NTP sync (for uptime calc)

// SdFs supports FAT16 / FAT32 / exFAT and enables LFN automatically.
// FIFO_SDIO uses the ESP32 hardware SDMMC peripheral (same pins as SD_MMC).
static SdFs sd;

// ─── Internal line-reader helper ─────────────────────────────────────────────
// Reads one '\n'-terminated line from an open FsFile into a String.
static String sdReadLine(FsFile &f) {
    String line;
    line.reserve(128);
    int c;
    while ((c = f.read()) >= 0) {
        if (c == '\n') break;
        if (c != '\r') line += (char)c;  // skip Windows CR
    }
    return line;
}

// ─── Internal whole-file reader ──────────────────────────────────────────────
static String sdReadAll(FsFile &f) {
    uint32_t sz = (uint32_t)f.fileSize();
    if (sz == 0) return "";
    char *buf = (char*)malloc(sz + 1);
    if (!buf) return "";
    f.read(buf, sz);
    buf[sz] = '\0';
    String s = String(buf);
    free(buf);
    return s;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  NTP / time helpers
// ═══════════════════════════════════════════════════════════════════════════════
namespace Bridge {

void syncNTP() {
    configTime(gSettings.gmtOffsetSec, 0, gSettings.ntpServer);
    struct tm ti;
    int retries = 0;
    while (!getLocalTime(&ti) && retries++ < 10) delay(500);
    if (retries < 10) {
        ntpSynced  = true;
        _bootEpoch = time(nullptr);
        Serial.printf("[NTP] Synced. Date: %04d-%02d-%02d  Time: %02d:%02d:%02d\n",
            ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday,
            ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        ntpSynced = false;
        Serial.println("[NTP] Sync failed – using millis fallback");
    }
}

// Returns "YYYY-MM-DD"
String getCurrentDateStr() {
    if (ntpSynced) {
        struct tm ti;
        if (getLocalTime(&ti)) {
            char buf[12];
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                     ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday);
            return String(buf);
        }
    }
    // Fallback: unique day index since boot
    unsigned long days = millis() / (24UL * 60 * 60 * 1000);
    return "day_" + String(days);
}

// Returns "HH:MM:SS"
String getCurrentTimeStr() {
    if (ntpSynced) {
        struct tm ti;
        if (getLocalTime(&ti)) {
            char buf[10];
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                     ti.tm_hour, ti.tm_min, ti.tm_sec);
            return String(buf);
        }
    }
    unsigned long s = millis() / 1000;
    char buf[20];
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
             (s/3600)%24, (s/60)%60, s%60);
    return String(buf);
}

// Returns "HH:MM"
String getCurrentHHMM() {
    return getCurrentTimeStr().substring(0, 5);
}

// Returns label like "Mon", "Tue" … for a date N days ago
static String dayLabel(int daysAgo) {
    if (!ntpSynced) return "D-" + String(daysAgo);
    time_t now  = time(nullptr);
    time_t then = now - (time_t)daysAgo * 86400;
    struct tm *ti = localtime(&then);
    const char *days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    return String(days[ti->tm_wday]);
}

// Returns date string N days ago ("YYYY-MM-DD")
static String dateStrDaysAgo(int daysAgo) {
    if (!ntpSynced) {
        unsigned long today = millis() / (24UL * 60 * 60 * 1000);
        return "day_" + String(today - daysAgo);
    }
    time_t now  = time(nullptr);
    time_t then = now - (time_t)daysAgo * 86400;
    struct tm *ti = localtime(&then);
    char buf[12];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
             ti->tm_year+1900, ti->tm_mon+1, ti->tm_mday);
    return String(buf);
}

// Attendance status based on current time and settings
static String computeStatus(const char *hhmm) {
    if (!hhmm || hhmm[0] == '\0' || strcmp(hhmm, "--") == 0) return "Absent";
    if (strcmp(hhmm, gSettings.absentTime) >= 0) return "Absent";
    if (strcmp(hhmm, gSettings.lateTime)   >= 0) return "Late";
    return "Present";
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SD Initialisation
// ═══════════════════════════════════════════════════════════════════════════════
void initSD() {
    // SdioConfig(FIFO_SDIO) uses ESP32 hardware SDMMC peripheral.
    // Use DMA_SDIO for slightly better throughput if FIFO gives issues.
    if (!sd.begin(SdioConfig(FIFO_SDIO))) {
        Serial.println("[SD] Mount failed – check card and connections");
        _sdOk = false;
        return;
    }
    _sdOk = true;

    // Create required directories (sd.mkdir does nothing if already present)
    const char *dirs[] = { "/db", "/atd", "/cfg", nullptr };
    for (int i = 0; dirs[i]; i++) {
        if (!sd.exists(dirs[i])) {
            sd.mkdir(dirs[i]);
            Serial.printf("[SD] Created %s\n", dirs[i]);
        }
    }

    // Bootstrap empty user DB
    if (!sd.exists("/db/users.txt")) {
        FsFile f;
        if (f.open("/db/users.txt", O_WRONLY | O_CREAT | O_TRUNC)) {
            f.print("[]");
            f.close();
        }
    }
    Serial.println("[SD] Ready  (LFN enabled via SdFat)");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Directory listing
// ═══════════════════════════════════════════════════════════════════════════════
void listDir(const char *dirname, uint8_t levels) {
    FsFile dir, entry;
    if (!dir.open(dirname, O_RDONLY)) return;

    while (entry.openNext(&dir, O_RDONLY)) {
        char fname[256] = {0};
        entry.getName(fname, sizeof(fname));

        if (entry.isDirectory()) {
            Serial.printf("  DIR : %s\n", fname);
            if (levels > 0) {
                char childPath[512];
                snprintf(childPath, sizeof(childPath), "%s/%s", dirname, fname);
                listDir(childPath, levels - 1);
            }
        } else {
            Serial.printf("  FILE: %-40s  %lu bytes\n",
                          fname, (unsigned long)entry.fileSize());
        }
        entry.close();
    }
    dir.close();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Face-ID binary persistence
// ═══════════════════════════════════════════════════════════════════════════════
void write_face_id_name_list_sdcard(face_id_name_list *l, const char *path) {
    if (!_sdOk) return;
    FsFile f;
    if (!f.open(path, O_WRONLY | O_CREAT | O_TRUNC)) {
        Serial.println("[SD] write FACE.BIN failed");
        return;
    }

    f.write((uint8_t)l->count);
    if (l->count > 0 && l->head) {
        f.write((uint8_t)l->confirm_times);
        face_id_node *p  = l->head;
        uint8_t       saved = 0;
        const int     vsz   = FACE_ID_SIZE * sizeof(float);
        while (p && saved < l->count) {
            f.write((const uint8_t*)p->id_name, ENROLL_NAME_LEN);
            if (p->id_vec && p->id_vec->item)
                f.write((const uint8_t*)p->id_vec->item, vsz);
            saved++;
            p = p->next;
        }
        Serial.printf("[SD] Saved %d face(s)\n", saved);
    }
    f.close();
}

void read_face_id_name_list_sdcard(face_id_name_list *l, const char *path) {
    if (!_sdOk || !sd.exists(path)) {
        Serial.println("[SD] No FACE.BIN found");
        return;
    }
    FsFile f;
    if (!f.open(path, O_RDONLY)) return;

    uint8_t count = 0;
    if (f.read(&count, 1) != 1 || count == 0) { f.close(); return; }

    uint8_t confirmTimes = 0;
    f.read(&confirmTimes, 1);
    l->confirm_times = confirmTimes;
    l->count = 0; l->head = nullptr; l->tail = nullptr;
    const int vsz = FACE_ID_SIZE * sizeof(float);

    for (uint8_t i = 0; i < count; i++) {
        face_id_node *node = (face_id_node *)dl_lib_calloc(1, sizeof(face_id_node), 0);
        if (!node) break;
        f.read((uint8_t*)node->id_name, ENROLL_NAME_LEN);
        node->id_vec = dl_matrix3d_alloc(1, 1, 1, FACE_ID_SIZE);
        if (!node->id_vec) { dl_lib_free(node); break; }
        f.read((uint8_t*)node->id_vec->item, vsz);
        node->next = nullptr;
        if (!l->head) { l->head = node; l->tail = node; }
        else          { l->tail->next = node; l->tail = node; }
        l->count++;
    }
    f.close();
    Serial.printf("[SD] Loaded %d face(s)\n", l->count);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  User database  (JSON array at /db/users.txt)
// ═══════════════════════════════════════════════════════════════════════════════
String getUsersJSON() {
    if (!_sdOk) return "[]";
    FsFile f;
    if (!f.open("/db/users.txt", O_RDONLY)) return "[]";
    String s = sdReadAll(f);
    f.close();
    return (s.length() > 0) ? s : "[]";
}

int getUserCount() {
    String j = getUsersJSON();
    int c = 0, depth = 0;
    bool inStr = false;
    for (char ch : j) {
        if (ch == '"') inStr = !inStr;
        if (inStr) continue;
        if (ch == '{') { if (++depth == 1) c++; }
        else if (ch == '}') depth--;
    }
    return c;
}

// saveUserToDB – takes a UserRecord struct instead of four loose Strings.
bool saveUserToDB(const UserRecord &user) {
    if (!_sdOk) return false;

    FsFile f;
    DynamicJsonDocument doc(8192);
    if (f.open("/db/users.txt", O_RDONLY)) {
        if (deserializeJson(doc, f) != DeserializationError::Ok)
            doc.to<JsonArray>();
        f.close();
    } else {
        doc.to<JsonArray>();
    }

    JsonArray arr = doc.as<JsonArray>();
    // Reject duplicate names
    for (JsonObject u : arr)
        if (strcmp(u["name"] | "", user.name) == 0) {
            Serial.println("[DB] User already exists");
            return false;
        }

    JsonObject nu  = arr.createNestedObject();
    nu["id"]      = user.id;
    nu["name"]    = user.name;
    nu["dept"]    = user.dept;
    nu["role"]    = (user.role[0] != '\0') ? user.role : "Student";
    nu["regDate"] = getCurrentDateStr();
    nu["faces"]   = 5;   // updated after enrolment completes

    if (!f.open("/db/users.txt", O_WRONLY | O_CREAT | O_TRUNC)) return false;
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    Serial.printf("[DB] Saved user: %s  id: %s\n", user.name, user.id);
    return ok;
}

// deleteUserFromDB – takes const char* name instead of String.
bool deleteUserFromDB(const char *name) {
    if (!_sdOk) return false;

    FsFile f;
    if (!f.open("/db/users.txt", O_RDONLY)) return false;
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return false; }
    f.close();

    JsonArray arr = doc.as<JsonArray>();
    for (int i = 0; i < (int)arr.size(); i++) {
        if (strcmp(arr[i]["name"] | "", name) == 0) {
            arr.remove(i);
            if (!f.open("/db/users.txt", O_WRONLY | O_CREAT | O_TRUNC)) return false;
            bool ok = serializeJson(doc, f) > 0;
            f.close();
            return ok;
        }
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Attendance logging
// ═══════════════════════════════════════════════════════════════════════════════

// logAttendance – auto-recognised path. date/time/status are filled here;
// only rec.uid, rec.name, rec.dept (and optionally rec.confidence) are used.
void logAttendance(const AttendanceRecord &rec) {
    if (!_sdOk) return;

    String date    = getCurrentDateStr();
    String timeStr = getCurrentHHMM();
    String status  = computeStatus(timeStr.c_str());
    String fname   = "/atd/l_" + date + ".csv";

    // Duplicate check – skip if uid already logged today
    if (sd.exists(fname.c_str())) {
        FsFile chk;
        if (chk.open(fname.c_str(), O_RDONLY)) {
            sdReadLine(chk);  // skip header
            while (chk.available()) {
                String line = sdReadLine(chk);
                if (line.indexOf(rec.uid) >= 0) {
                    chk.close();
                    Serial.printf("[ATD] %s already logged today\n", rec.name);
                    return;
                }
            }
            chk.close();
        }
    }

    bool needHeader = !sd.exists(fname.c_str());
    FsFile f;
    // Try append first; fall back to create
    if (!f.open(fname.c_str(), O_WRONLY | O_CREAT | O_APPEND)) {
        Serial.println("[ATD] Failed to open log file");
        return;
    }
    if (needHeader) f.println("UID,Name,Department,Date,Time,Status,Confidence");

    const char *conf = (rec.confidence[0] != '\0') ? rec.confidence : "92%";
    char line[256];
    snprintf(line, sizeof(line), "%s,%s,%s,%s,%s,%s,%s\n",
             rec.uid, rec.name, rec.dept,
             date.c_str(), timeStr.c_str(), status.c_str(), conf);
    f.print(line);
    f.close();
    Serial.printf("[ATD] Logged: %s – %s – %s\n",
                  rec.name, timeStr.c_str(), status.c_str());
}

// manualAttendance – admin override.
// rec.date / rec.time / rec.status are used as supplied (auto-filled if empty).
bool manualAttendance(const AttendanceRecord &rec) {
    if (!_sdOk) return false;

    // Work with mutable copies so we can fill defaults
    char date[12]   = {0};
    char timeStr[6] = {0};
    char status[16] = {0};
    char name[64]   = {0};

    strncpy(date,    rec.date[0]   ? rec.date   : getCurrentDateStr().c_str(), 11);
    strncpy(timeStr, rec.time[0]   ? rec.time   : getCurrentHHMM().c_str(),     5);
    strncpy(status,  rec.status[0] ? rec.status : "Present",                   15);
    strncpy(name,    rec.name,                                                  63);

    // If name not in record, look it up from DB by uid
    if (name[0] == '\0') {
        String j = getUsersJSON();
        DynamicJsonDocument doc(8192);
        if (deserializeJson(doc, j) == DeserializationError::Ok) {
            for (JsonObject u : doc.as<JsonArray>()) {
                if (strcmp(u["id"] | "", rec.uid) == 0) {
                    strncpy(name, u["name"] | "", 63);
                    break;
                }
            }
        }
    }

    String fname = "/atd/l_" + String(date) + ".csv";
    bool needHeader = !sd.exists(fname.c_str());

    // If record exists for this uid today, overwrite the status field in-memory
    if (!needHeader) {
        FsFile r;
        if (r.open(fname.c_str(), O_RDONLY)) {
            String header  = sdReadLine(r);
            String rebuilt = header + "\n";
            bool replaced  = false;

            while (r.available()) {
                String line = sdReadLine(r);
                if (line.length() == 0) continue;

                if (line.startsWith(String(rec.uid) + ",")) {
                    // CSV: UID,Name,Department,Date,Time,Status,Confidence
                    //        0   1      2        3    4     5        6
                    int f0 = line.indexOf(',');
                    int f1 = line.indexOf(',', f0+1);
                    int f2 = line.indexOf(',', f1+1);
                    int f3 = line.indexOf(',', f2+1);
                    int f4 = line.indexOf(',', f3+1);
                    int f5 = line.indexOf(',', f4+1);
                    // Rebuild: keep fields 0-3, replace time+status, keep confidence
                    String newLine = line.substring(0, f3+1)
                                   + String(timeStr) + ","
                                   + String(status)
                                   + (f5 >= 0 ? line.substring(f5) : "");
                    rebuilt += newLine + "\n";
                    replaced = true;
                } else {
                    rebuilt += line + "\n";
                }
            }
            r.close();

            if (replaced) {
                FsFile w;
                if (!w.open(fname.c_str(), O_WRONLY | O_CREAT | O_TRUNC)) return false;
                w.print(rebuilt);
                w.close();
                Serial.printf("[ATD] Manual override: %s -> %s\n", rec.uid, status);
                return true;
            }
        }
    }

    // Append new record
    FsFile f;
    if (!f.open(fname.c_str(), O_WRONLY | O_CREAT | O_APPEND)) return false;
    if (needHeader) f.println("UID,Name,Department,Date,Time,Status,Confidence");

    char lineBuf[256];
    snprintf(lineBuf, sizeof(lineBuf), "%s,%s,,%s,%s,%s,Manual\n",
             rec.uid, name, date, timeStr, status);
    f.print(lineBuf);
    f.close();
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Attendance queries
// ═══════════════════════════════════════════════════════════════════════════════

// Helper: parse a CSV log file into a JsonArray
static void parseLogFile(const String &fname, JsonArray &arr,
                         const String &deptFilt, const String &statusFilt,
                         const String &search) {
    if (!sd.exists(fname.c_str())) return;
    FsFile f;
    if (!f.open(fname.c_str(), O_RDONLY)) return;
    sdReadLine(f);   // skip header

    while (f.available()) {
        String line = sdReadLine(f);
        if (line.length() < 3) continue;
        // UID,Name,Department,Date,Time,Status,Confidence
        int c0 = line.indexOf(',');
        int c1 = line.indexOf(',', c0+1);
        int c2 = line.indexOf(',', c1+1);
        int c3 = line.indexOf(',', c2+1);
        int c4 = line.indexOf(',', c3+1);
        int c5 = line.indexOf(',', c4+1);
        if (c0<0||c1<0||c2<0||c3<0||c4<0) continue;

        String uid    = line.substring(0, c0);
        String name   = line.substring(c0+1, c1);
        String dept   = line.substring(c1+1, c2);
        String date   = line.substring(c2+1, c3);
        String time   = line.substring(c3+1, c4);
        String status = line.substring(c4+1, c5>0 ? c5 : (int)line.length());
        String conf   = c5>0 ? line.substring(c5+1) : "";
        status.trim(); conf.trim();

        if (deptFilt   != "" && dept   != deptFilt)  continue;
        if (statusFilt != "" && status != statusFilt) continue;
        if (search     != "") {
            String sl = search; sl.toLowerCase();
            String nl = name;   nl.toLowerCase();
            String ul = uid;    ul.toLowerCase();
            if (nl.indexOf(sl) < 0 && ul.indexOf(sl) < 0) continue;
        }

        JsonObject obj  = arr.createNestedObject();
        obj["uid"]        = uid;
        obj["name"]       = name;
        obj["dept"]       = dept;
        obj["date"]       = date;
        obj["time"]       = time;
        obj["status"]     = status;
        obj["confidence"] = conf;
    }
    f.close();
}

String getLogsJSON(String date, String dept, String status, String search) {
    if (date == "") date = getCurrentDateStr();
    String fname = "/atd/l_" + date + ".csv";
    DynamicJsonDocument doc(16384);
    JsonArray arr = doc.to<JsonArray>();
    parseLogFile(fname, arr, dept, status, search);
    String out;
    serializeJson(doc, out);
    return out;
}

String getAttendanceLogs() {
    return getLogsJSON(getCurrentDateStr(), "", "", "");
}

String getLogsRange(int days) {
    DynamicJsonDocument doc(4096);
    JsonObject root    = doc.to<JsonObject>();
    JsonArray  labels  = root.createNestedArray("labels");
    JsonArray  present = root.createNestedArray("present");
    JsonArray  absent  = root.createNestedArray("absent");
    JsonArray  late    = root.createNestedArray("late");

    int total   = getUserCount();
    int sumPres = 0;

    for (int i = days-1; i >= 0; i--) {
        String dStr = dateStrDaysAgo(i);
        String lbl  = (days <= 14) ? dayLabel(i) : dStr.substring(5);
        labels.add(lbl);

        String fname = "/atd/l_" + dStr + ".csv";
        int p=0, a=0, l=0;
        if (sd.exists(fname.c_str())) {
            FsFile f;
            if (f.open(fname.c_str(), O_RDONLY)) {
                sdReadLine(f);   // skip header
                while (f.available()) {
                    String line = sdReadLine(f);
                    if (line.length() < 3) continue;
                    // Quick status parse: 5th comma field
                    int c=0, pos=-1;
                    for (int j=0; j<(int)line.length(); j++) {
                        if (line[j]==',') { c++; if(c==5){pos=j; break;} }
                    }
                    if (pos > 0) {
                        int end = line.indexOf(',', pos+1);
                        String s = line.substring(pos+1, end>0 ? end : (int)line.length());
                        s.trim();
                        if      (s == "Present") p++;
                        else if (s == "Late")    l++;
                        else                     a++;
                    }
                }
                f.close();
            }
        }
        present.add(p);
        absent.add(a);
        late.add(l);
        sumPres += p + l;
    }

    root["total"]     = total;
    root["userCount"] = total;
    root["avgRate"]   = total > 0 ? (sumPres * 100 / (total * days)) : 0;

    String out;
    serializeJson(doc, out);
    return out;
}

String downloadAttendanceCSV(String date) {
    if (date == "") date = getCurrentDateStr();
    String fname = "/atd/l_" + date + ".csv";
    if (!sd.exists(fname.c_str()))
        return "UID,Name,Department,Date,Time,Status,Confidence\n";
    FsFile f;
    if (!f.open(fname.c_str(), O_RDONLY))
        return "UID,Name,Department,Date,Time,Status,Confidence\n";
    String c = sdReadAll(f);
    f.close();
    return c;
}

bool clearAttendanceLogs(String date) {
    if (date == "") date = getCurrentDateStr();
    String fname = "/atd/l_" + date + ".csv";
    if (!sd.exists(fname.c_str())) return true;
    return sd.remove(fname.c_str());
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Dashboard stats
// ═══════════════════════════════════════════════════════════════════════════════
String getStatsJSON() {
    String date  = getCurrentDateStr();
    String fname = "/atd/l_" + date + ".csv";
    int p=0, a=0, l=0;

    if (sd.exists(fname.c_str())) {
        FsFile f;
        if (f.open(fname.c_str(), O_RDONLY)) {
            sdReadLine(f);  // skip header
            while (f.available()) {
                String line = sdReadLine(f);
                if (line.length() < 3) continue;
                int c=0, pos=-1;
                for (int j=0; j<(int)line.length(); j++) {
                    if (line[j]==',') { c++; if(c==5){pos=j; break;} }
                }
                if (pos > 0) {
                    int end = line.indexOf(',', pos+1);
                    String s = line.substring(pos+1, end>0?end:(int)line.length());
                    s.trim();
                    if      (s=="Present") p++;
                    else if (s=="Late")    l++;
                    else                   a++;
                }
            }
            f.close();
        }
    }

    int total = getUserCount();

    // Storage (SdFat API)
    String storageStr = "--";
    if (_sdOk) {
        uint64_t used = (uint64_t)sd.card()->sectorCount() * 512
                      - (uint64_t)sd.vol()->freeClusterCount()
                          * sd.vol()->bytesPerCluster();
        storageStr = String((double)used / 1024.0) + " KB";
    }

    // Uptime
    unsigned long sec = millis() / 1000;
    char upbuf[20];
    snprintf(upbuf, sizeof(upbuf), "%luh %02lum", sec/3600, (sec/60)%60);

    DynamicJsonDocument doc(512);
    doc["total"]     = total;
    doc["present"]   = p;
    doc["absent"]    = a;
    doc["late"]      = l;
    doc["storage"]   = storageStr;
    doc["uptime"]    = String(upbuf);
    doc["ntpSynced"] = ntpSynced;
    doc["date"]      = date;
    doc["time"]      = getCurrentHHMM();

    String out;
    serializeJson(doc, out);
    return out;
}

String getStorageJSON() {
    DynamicJsonDocument doc(256);
    if (_sdOk) {
        uint64_t totalBytes = (uint64_t)sd.card()->sectorCount() * 512;
        uint64_t freeBytes  = (uint64_t)sd.vol()->freeClusterCount()
                                       * sd.vol()->bytesPerCluster();
        uint64_t usedBytes  = totalBytes - freeBytes;

        doc["total"] = String((double)totalBytes / (1024.0*1024.0)) + " MB";
        doc["used"]  = String((double)usedBytes  / (1024.0*1024.0)) + " MB";
        doc["free"]  = String((double)freeBytes  / (1024.0*1024.0)) + " MB";
        doc["pct"]   = (totalBytes > 0) ? (int)((double)usedBytes/totalBytes*100) : 0;
    } else {
        doc["total"] = "--"; doc["used"] = "--"; doc["free"] = "--"; doc["pct"] = 0;
    }
    String out;
    serializeJson(doc, out);
    return out;
}

String getStatusJSON() {
    DynamicJsonDocument doc(256);
    doc["camera"]    = true;
    doc["wifi"]      = (WiFi.status() == WL_CONNECTED);
    doc["model"]     = (id_list.count > 0);
    doc["faceCount"] = (int)id_list.count;
    doc["ip"]        = WiFi.localIP().toString();
    doc["ssid"]      = String(gSettings.ssid);
    doc["ntpSynced"] = ntpSynced;
    String out;
    serializeJson(doc, out);
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Settings persistence
// ═══════════════════════════════════════════════════════════════════════════════
bool saveSettings(const AttendanceSettings &s) {
    if (!_sdOk) return false;
    DynamicJsonDocument doc(512);
    doc["startTime"]     = s.startTime;
    doc["endTime"]       = s.endTime;
    doc["lateTime"]      = s.lateTime;
    doc["absentTime"]    = s.absentTime;
    doc["confidence"]    = s.confidence;
    doc["buzzerEnabled"] = s.buzzerEnabled;
    doc["autoMode"]      = s.autoMode;
    doc["gmtOffsetSec"]  = s.gmtOffsetSec;
    doc["ntpServer"]     = s.ntpServer;

    FsFile f;
    if (!f.open("/cfg/settings.json", O_WRONLY | O_CREAT | O_TRUNC)) return false;
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    Serial.println("[CFG] Settings saved");
    return ok;
}

bool loadSettings(AttendanceSettings &s) {
    setDefaultSettings(s);   // always start with defaults
    if (!_sdOk || !sd.exists("/cfg/settings.json")) return false;

    FsFile f;
    if (!f.open("/cfg/settings.json", O_RDONLY)) return false;
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return false; }
    f.close();

    if (doc.containsKey("startTime"))     strncpy(s.startTime,   doc["startTime"],  5);
    if (doc.containsKey("endTime"))       strncpy(s.endTime,     doc["endTime"],    5);
    if (doc.containsKey("lateTime"))      strncpy(s.lateTime,    doc["lateTime"],   5);
    if (doc.containsKey("absentTime"))    strncpy(s.absentTime,  doc["absentTime"], 5);
    if (doc.containsKey("confidence"))    s.confidence    = doc["confidence"];
    if (doc.containsKey("buzzerEnabled")) s.buzzerEnabled = doc["buzzerEnabled"];
    if (doc.containsKey("autoMode"))      s.autoMode      = doc["autoMode"];
    if (doc.containsKey("gmtOffsetSec"))  s.gmtOffsetSec  = doc["gmtOffsetSec"];
    if (doc.containsKey("ntpServer"))     strncpy(s.ntpServer, doc["ntpServer"], 63);
    Serial.println("[CFG] Settings loaded");
    return true;
}

} // namespace Bridge
