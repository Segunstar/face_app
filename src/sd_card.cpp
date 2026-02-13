// sd_card.cpp  –  FaceGuard Pro  (ESP32-CAM)
// All SD / persistence / time functions for the attendance system.

#include "fr_forward.h"

#undef min
#undef max
#include "Arduino.h"
#include "FS.h"
#include "SD_MMC.h"
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

// ═══════════════════════════════════════════════════════════════════════════════
//  NTP / time helpers
// ═══════════════════════════════════════════════════════════════════════════════
namespace Bridge {

void syncNTP() {
    configTime(gSettings.gmtOffsetSec, 0, gSettings.ntpServer);
    // Wait up to 5 s for sync
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
            char buf[36]; // Increased buffer size to avoid truncation
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                     ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday);
            return String(buf);
        }
    }
    // Fallback: days since boot (not calendar date, but unique per day)
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
    String t = getCurrentTimeStr();
    return t.substring(0, 5);
}

// Returns label like "Mon", "Tue" … for a date N days ago
static String dayLabel(int daysAgo) {
    if (!ntpSynced) return "D-" + String(daysAgo);
    time_t now = time(nullptr);
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
    char buf[36];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
             ti->tm_year+1900, ti->tm_mon+1, ti->tm_mday);
    return String(buf);
}

// Attendance status based on current time and settings
static String computeStatus(String hhmm) {
    if (hhmm == "" || hhmm == "--") return "Absent";
    // Compare "HH:MM" strings lexicographically (works because zero-padded)
    if (hhmm >= String(gSettings.absentTime)) return "Absent";
    if (hhmm >= String(gSettings.lateTime))   return "Late";
    return "Present";
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SD Initialisation
// ═══════════════════════════════════════════════════════════════════════════════
void initSD() {
    if (!SD_MMC.begin()) {
        Serial.println("[SD] Mount failed");
        _sdOk = false;
        return;
    }
    _sdOk = (SD_MMC.cardType() != CARD_NONE);
    if (!_sdOk) { Serial.println("[SD] No card"); return; }

    // Create required directories
    const char *dirs[] = { "/db", "/atd", "/cfg", nullptr };
    for (int i = 0; dirs[i]; i++) {
        if (!SD_MMC.exists(dirs[i])) {
            SD_MMC.mkdir(dirs[i]);
            Serial.printf("[SD] Created %s\n", dirs[i]);
        }
    }
    // Bootstrap empty user DB
    if (!SD_MMC.exists("/db/users.txt")) {
        File f = SD_MMC.open("/db/users.txt", FILE_WRITE);
        if (f) { f.print("[]"); f.close(); }
    }
    Serial.println("[SD] Ready");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Directory listing
// ═══════════════════════════════════════════════════════════════════════════════
void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
    File root = fs.open(dirname);
    if (!root || !root.isDirectory()) return;
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.printf("  DIR : %s\n", file.name());
            if (levels) listDir(fs, file.path(), levels - 1);
        } else {
            Serial.printf("  FILE: %-30s  %d bytes\n", file.name(), file.size());
        }
        file = root.openNextFile();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Face-ID binary persistence (unchanged logic, improved error handling)
// ═══════════════════════════════════════════════════════════════════════════════
void write_face_id_name_list_sdcard(face_id_name_list *l, const char *path) {
    if (!_sdOk) return;
    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) { Serial.println("[SD] write FACE.BIN failed"); return; }

    f.write((uint8_t)l->count);
    if (l->count > 0 && l->head) {
        f.write((uint8_t)l->confirm_times);
        face_id_node *p = l->head;
        uint8_t saved = 0;
        const int vsz = FACE_ID_SIZE * sizeof(float);
        while (p && saved < l->count) {
            f.write((uint8_t*)p->id_name, ENROLL_NAME_LEN);
            if (p->id_vec && p->id_vec->item)
                f.write((uint8_t*)p->id_vec->item, vsz);
            saved++; p = p->next;
        }
        Serial.printf("[SD] Saved %d face(s)\n", saved);
    }
    f.close();
}

void read_face_id_name_list_sdcard(face_id_name_list *l, const char *path) {
    if (!_sdOk || !SD_MMC.exists(path)) {
        Serial.println("[SD] No FACE.BIN found");
        return;
    }
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) return;

    uint8_t count = f.read();
    if (count == 0) { f.close(); return; }

    l->confirm_times = f.read();
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
//  User database
// ═══════════════════════════════════════════════════════════════════════════════
String getUsersJSON() {
    if (!_sdOk) return "[]";
    File f = SD_MMC.open("/db/users.txt");
    if (!f) return "[]";
    String s = f.readString();
    f.close();
    return s;
}

int getUserCount() {
    String j = getUsersJSON();
    int c = 0, depth = 0;
    bool inStr = false;
    for (char ch : j) {
        if (ch == '"' && (depth == 0 || inStr)) inStr = !inStr;
        if (inStr) continue;
        if (ch == '{') { if (++depth == 1) c++; }
        else if (ch == '}') depth--;
    }
    return c;
}

bool saveUserToDB(String id, String name, String dept, String role) {
    if (!_sdOk) return false;
    File f = SD_MMC.open("/db/users.txt", FILE_READ);
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, f) != DeserializationError::Ok) {
        f.close();
        // Recover with empty array
        doc.to<JsonArray>();
    } else f.close();

    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject u : arr)
        if (u["name"] == name) { Serial.println("[DB] User exists"); return false; }

    JsonObject nu = arr.createNestedObject();
    nu["id"]       = id;
    nu["name"]     = name;
    nu["dept"]     = dept;
    nu["role"]     = role;
    nu["regDate"]  = getCurrentDateStr();
    nu["faces"]    = 5;          // updated after enrollment confirms

    f = SD_MMC.open("/db/users.txt", FILE_WRITE);
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    Serial.printf("[DB] Saved user %s\n", name.c_str());
    return ok;
}

bool deleteUserFromDB(String name) {
    if (!_sdOk) return false;
    File f = SD_MMC.open("/db/users.txt", FILE_READ);
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return false; }
    f.close();

    JsonArray arr = doc.as<JsonArray>();
    for (int i = 0; i < (int)arr.size(); i++) {
        if (arr[i]["name"] == name) {
            arr.remove(i);
            f = SD_MMC.open("/db/users.txt", FILE_WRITE);
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
void logAttendance(String uid, String name, String dept) {
    if (!_sdOk) return;
    String date    = getCurrentDateStr();
    String timeStr = getCurrentHHMM();
    String status  = computeStatus(timeStr);
    String fname   = "/atd/l_" + date + ".csv";

    // Duplicate check
    if (SD_MMC.exists(fname.c_str())) {
        File chk = SD_MMC.open(fname.c_str(), FILE_READ);
        if (chk) {
            chk.readStringUntil('\n'); // skip header
            while (chk.available()) {
                String line = chk.readStringUntil('\n');
                if (line.indexOf(uid) >= 0) {
                    chk.close();
                    Serial.printf("[ATD] %s already logged today\n", name.c_str());
                    return;
                }
            }
            chk.close();
        }
    }

    bool needHeader = !SD_MMC.exists(fname.c_str());
    File f = SD_MMC.open(fname.c_str(), FILE_APPEND);
    if (!f) { f = SD_MMC.open(fname.c_str(), FILE_WRITE); needHeader = true; }
    if (!f) { Serial.println("[ATD] Failed to open log file"); return; }
    if (needHeader) f.println("UID,Name,Department,Date,Time,Status,Confidence");
    String conf = "92%";   // MTMN doesn't expose a simple % – placeholder
    f.printf("%s,%s,%s,%s,%s,%s,%s\n",
             uid.c_str(), name.c_str(), dept.c_str(),
             date.c_str(), timeStr.c_str(), status.c_str(), conf.c_str());
    f.close();
    Serial.printf("[ATD] Logged: %s – %s – %s\n", name.c_str(), timeStr.c_str(), status.c_str());
}

bool manualAttendance(String uid, String name, String date,
                      String time, String status, String notes) {
    if (!_sdOk) return false;
    if (date == "") date = getCurrentDateStr();
    if (time == "") time = getCurrentHHMM();

    // Lookup name from DB if not provided
    if (name == "") {
        String j = getUsersJSON();
        DynamicJsonDocument doc(8192);
        if (deserializeJson(doc, j) == DeserializationError::Ok) {
            for (JsonObject u : doc.as<JsonArray>()) {
                if (u["id"] == uid) { name = u["name"].as<String>(); break; }
            }
        }
    }

    String fname = "/atd/l_" + date + ".csv";
    bool needHeader = !SD_MMC.exists(fname.c_str());

    // If record already exists for this uid+date, overwrite status in memory
    if (!needHeader) {
        File r = SD_MMC.open(fname.c_str(), FILE_READ);
        String header = r.readStringUntil('\n');
        String rebuilt = header + "\n";
        bool replaced = false;
        while (r.available()) {
            String line = r.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;
            if (line.startsWith(uid + ",")) {
                // replace status field (index 5)
                // "UID,Name,Department,Date,Time,Status,Confidence"
                //   0   1      2        3    4     5       6
                int f0=line.indexOf(',');
                int f1=line.indexOf(',',f0+1);
                int f2=line.indexOf(',',f1+1);
                int f3=line.indexOf(',',f2+1);
                int f4=line.indexOf(',',f3+1);
                int f5=line.indexOf(',',f4+1);
                String newLine = line.substring(0,f3+1) + time + "," +
                                 status + line.substring(f5);
                rebuilt += newLine + "\n";
                replaced = true;
            } else {
                rebuilt += line + "\n";
            }
        }
        r.close();
        if (replaced) {
            File w = SD_MMC.open(fname.c_str(), FILE_WRITE);
            if (!w) return false;
            w.print(rebuilt);
            w.close();
            Serial.printf("[ATD] Manual override: %s -> %s\n", uid.c_str(), status.c_str());
            return true;
        }
    }

    // Append new record
    File f = SD_MMC.open(fname.c_str(), FILE_APPEND);
    if (!f) { f = SD_MMC.open(fname.c_str(), FILE_WRITE); needHeader = true; }
    if (!f) return false;
    if (needHeader) f.println("UID,Name,Department,Date,Time,Status,Confidence");
    f.printf("%s,%s,,%s,%s,%s,Manual\n",
             uid.c_str(), name.c_str(), date.c_str(), time.c_str(), status.c_str());
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
    if (!SD_MMC.exists(fname.c_str())) return;
    File f = SD_MMC.open(fname.c_str(), FILE_READ);
    if (!f) return;
    f.readStringUntil('\n'); // skip header
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
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
        String status = line.substring(c4+1, c5>0?c5:line.length());
        String conf   = c5>0 ? line.substring(c5+1) : "";
        status.trim(); conf.trim();

        // Apply filters
        if (deptFilt   != "" && dept   != deptFilt)   continue;
        if (statusFilt != "" && status != statusFilt)  continue;
        if (search     != "") {
            String sl = search; sl.toLowerCase();
            String nl = name;   nl.toLowerCase();
            String ul = uid;    ul.toLowerCase();
            if (nl.indexOf(sl) < 0 && ul.indexOf(sl) < 0) continue;
        }

        JsonObject obj = arr.createNestedObject();
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
    JsonObject root = doc.to<JsonObject>();
    JsonArray labels  = root.createNestedArray("labels");
    JsonArray present = root.createNestedArray("present");
    JsonArray absent  = root.createNestedArray("absent");
    JsonArray late    = root.createNestedArray("late");

    int total   = getUserCount();
    int sumPres = 0;

    for (int i = days-1; i >= 0; i--) {
        String dStr = dateStrDaysAgo(i);
        String lbl  = (days <= 14) ? dayLabel(i) : dStr.substring(5); // "MM-DD" for longer ranges
        labels.add(lbl);

        String fname = "/atd/l_" + dStr + ".csv";
        int p=0, a=0, l=0;
        if (SD_MMC.exists(fname.c_str())) {
            File f = SD_MMC.open(fname.c_str(), FILE_READ);
            if (f) {
                f.readStringUntil('\n');
                while (f.available()) {
                    String line = f.readStringUntil('\n');
                    line.trim();
                    if (line.length() < 3) continue;
                    // Quick status parse: find 5th comma
                    int c=0, pos=-1;
                    for (int j=0;j<(int)line.length();j++) {
                        if (line[j]==',') { c++; if(c==5){pos=j;break;} }
                    }
                    if (pos > 0) {
                        String s = line.substring(pos+1, line.indexOf(',', pos+1)>0?line.indexOf(',',pos+1):line.length());
                        s.trim();
                        if (s == "Present") p++;
                        else if (s == "Late") l++;
                        else a++;
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
    if (!SD_MMC.exists(fname.c_str())) return "UID,Name,Department,Date,Time,Status,Confidence\n";
    File f = SD_MMC.open(fname.c_str(), FILE_READ);
    if (!f) return "UID,Name,Department,Date,Time,Status,Confidence\n";
    String c = f.readString();
    f.close();
    return c;
}

bool clearAttendanceLogs(String date) {
    if (date == "") date = getCurrentDateStr();
    String fname = "/atd/l_" + date + ".csv";
    if (!SD_MMC.exists(fname.c_str())) return true;
    return SD_MMC.remove(fname.c_str());
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Dashboard stats
// ═══════════════════════════════════════════════════════════════════════════════
String getStatsJSON() {
    String date  = getCurrentDateStr();
    String fname = "/atd/l_" + date + ".csv";
    int p=0, a=0, l=0;
    if (SD_MMC.exists(fname.c_str())) {
        File f = SD_MMC.open(fname.c_str(), FILE_READ);
        if (f) {
            f.readStringUntil('\n');
            while (f.available()) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.length() < 3) continue;
                int c=0, pos=-1;
                for (int j=0;j<(int)line.length();j++) {
                    if (line[j]==',') { c++; if(c==5){pos=j;break;} }
                }
                if (pos > 0) {
                    String s = line.substring(pos+1,
                        line.indexOf(',',pos+1)>0?line.indexOf(',',pos+1):line.length());
                    s.trim();
                    if (s=="Present") p++;
                    else if (s=="Late") l++;
                    else a++;
                }
            }
            f.close();
        }
    }

    int total = getUserCount();

    // Storage
    String storageStr = "--";
    if (_sdOk) {
        uint64_t used = SD_MMC.usedBytes() / 1024;
        storageStr = String((double)used) + " KB";
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
        doc["total"] = String((double)SD_MMC.totalBytes() / (1024*1024)) + " MB";
        doc["used"]  = String((double)SD_MMC.usedBytes()  / (1024*1024)) + " MB";
        doc["free"]  = String((double)(SD_MMC.totalBytes()-SD_MMC.usedBytes()) / (1024*1024)) + " MB";
        doc["pct"]   = (int)((double)SD_MMC.usedBytes()/SD_MMC.totalBytes()*100);
    } else {
        doc["total"] = "--"; doc["used"] = "--"; doc["free"] = "--"; doc["pct"] = 0;
    }
    String out; serializeJson(doc, out); return out;
}

String getStatusJSON() {
    DynamicJsonDocument doc(256);
    doc["camera"]    = true;   // If we got here, camera initialised OK
    doc["wifi"]      = (WiFi.status() == WL_CONNECTED);
    doc["model"]     = (id_list.count > 0);
    doc["faceCount"] = (int)id_list.count;
    doc["ip"]        = WiFi.localIP().toString();
    doc["ssid"]      = String(gSettings.ssid);
    doc["ntpSynced"] = ntpSynced;
    String out; serializeJson(doc, out); return out;
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
    File f = SD_MMC.open("/cfg/settings.json", FILE_WRITE);
    if (!f) return false;
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    Serial.println("[CFG] Settings saved");
    return ok;
}

bool loadSettings(AttendanceSettings &s) {
    setDefaultSettings(s);   // always start with defaults
    if (!_sdOk || !SD_MMC.exists("/cfg/settings.json")) return false;
    File f = SD_MMC.open("/cfg/settings.json", FILE_READ);
    if (!f) return false;
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return false; }
    f.close();
    if (doc.containsKey("startTime"))     strncpy(s.startTime,    doc["startTime"],    5);
    if (doc.containsKey("endTime"))       strncpy(s.endTime,      doc["endTime"],      5);
    if (doc.containsKey("lateTime"))      strncpy(s.lateTime,     doc["lateTime"],     5);
    if (doc.containsKey("absentTime"))    strncpy(s.absentTime,   doc["absentTime"],   5);
    if (doc.containsKey("confidence"))    s.confidence    = doc["confidence"];
    if (doc.containsKey("buzzerEnabled")) s.buzzerEnabled = doc["buzzerEnabled"];
    if (doc.containsKey("autoMode"))      s.autoMode      = doc["autoMode"];
    if (doc.containsKey("gmtOffsetSec"))  s.gmtOffsetSec  = doc["gmtOffsetSec"];
    if (doc.containsKey("ntpServer"))     strncpy(s.ntpServer, doc["ntpServer"], 63);
    Serial.println("[CFG] Settings loaded");
    return true;
}

} // namespace Bridge
