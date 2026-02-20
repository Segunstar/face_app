// sd_card.cpp  –  FaceGuard Pro  (ESP32-CAM)
// All SD / persistence / time functions.
// Uses greiman/SdFat v2 for full long-filename (LFN) support on FAT32/exFAT.

#include "fr_forward.h"

#undef min
#undef max
#include "Arduino.h"

// ── SdFat replaces Arduino SD_MMC + FS ───────────────────────────────────────
// SdFat's SdioConfig(FIFO_SDIO) is Teensy-only; on ESP32-CAM we drive the card
// via SPI using the four MMC pins that are exposed in 1-bit mode.
#include <SdFat.h>          // greiman/SdFat v2 – LFN on FAT32/exFAT
#include <sdios.h>

#include "SPI.h"

// ── AI-Thinker ESP32-CAM SD SPI pin mapping ──────────────────────────────────
// In 1-bit SPI mode only DATA0/CLK/CMD/CS are used; DATA1-3 (GPIO 4,12 etc.)
// are left free, so the flash LED on GPIO 4 still works normally.
#define SD_CS    33   // CS  – moved to GPIO 33 to free GPIO 13 for red LED/buzzer
#define SD_MOSI  15   // CMD   / MOSI
#define SD_MISO   2   // DATA0 / MISO
#define SD_SCK   14   // CLK   / SCK

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

// ── In-memory user cache ──────────────────────────────────────────────────────
// getUsersJSON() is called on every /api/stats (via getUserCount) AND every
// /api/users request.  Reading users.txt from SD every 30 s while attendanceTask
// is also writing causes systematic 2-second mutex timeouts that make the portal
// return empty JSON on every endpoint -- the "portal blank after attendance mode"
// bug.  Fix: cache the JSON string in RAM; invalidate ONLY on writes.
// During attendance mode the user list never changes, so httpd gets a zero-cost
// RAM hit with no SD contention at all.
static String _usersCache   = "";
static bool   _usersCacheOk = false;

static inline void _invalidateUsersCache() {
    _usersCacheOk = false;
    _usersCache   = "";
}

// SdFat32 handles FAT32 (the format used by every ESP32-CAM microSD card)
// and enables full LFN (long filename) support when built with
// -D USE_LONG_FILE_NAMES=255.
// File handles are File32 — the native type for SdFat32.
static SdFat32 sd;

// ─── Cross-task SD access guard ──────────────────────────────────────────────
// The ATD task, stream handler (httpd task) and HTTP API handlers all access
// the SD card concurrently.  SdFat's SHARED_SPI mode serialises SPI-level
// transactions, but we also need to serialise at the SdFat object level
// (file descriptors, directory state, etc.).
// Using a recursive mutex so helper functions that call other SD helpers
// (e.g. getUserCount → getUsersJSON) don't self-deadlock.
static SemaphoreHandle_t _sdMutex = nullptr;

// Convenience macros — return a default value if the mutex can't be taken
// within 2 s (should never happen in normal operation).
#define SD_TAKE()   (xSemaphoreTakeRecursive(_sdMutex, pdMS_TO_TICKS(2000)) == pdTRUE)
#define SD_GIVE()   xSemaphoreGiveRecursive(_sdMutex)

// ─── Internal line-reader helper ─────────────────────────────────────────────
// Reads one '\n'-terminated line from an open File32 into a String.
static String sdReadLine(File32 &f) {
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
static String sdReadAll(File32 &f) {
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
    // getLocalTime(tm, timeout_ms) — pass 1000 ms so each attempt is short.
    // Without the explicit timeout it defaults to ~5000 ms per call, causing
    // up to 60 seconds of blocking when NTP is unreachable (10 × 6 s ≈ 60 s).
    while (!getLocalTime(&ti, 1000) && retries++ < 10) delay(200);
    if (retries <= 10 && getLocalTime(&ti, 1000)) {
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
            char buf[32];   // 32 > worst-case int digits + separators + NUL
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
            char buf[32];   // 32 > worst-case int digits + separators + NUL
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
    char buf[32];   // 32 > worst-case int digits + separators + NUL
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
//  SD Initialisation  –  with retry + bus-reset recovery
// ═══════════════════════════════════════════════════════════════════════════════

// Internal: attempt a single SD mount.
// Uses SHARED_SPI (not DEDICATED_SPI): DEDICATED_SPI holds the Arduino SPI
// mutex permanently, causing FreeRTOS asserts when other tasks call
// endTransaction from a different task context.
// clkMHz: start slow (10 MHz) for the first attempt to handle sluggish cards,
// then ramp to 20 MHz on subsequent attempts.
static bool _sdTryMount(uint8_t clkMHz) {
    // Re-init SPI bus to clear any lingering state from a failed previous attempt
    SPI.end();
    delay(20);
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    // Pull CS high for at least 74 clock cycles before SD init (SD spec §6.4.1)
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    delay(10);
    return sd.begin(SdSpiConfig(SD_CS, SHARED_SPI, SD_SCK_MHZ(clkMHz), &SPI));
}

// Internal: create required directory tree and bootstrap empty user DB.
static void _sdBootstrapFS() {
    const char *dirs[] = { "/db", "/atd", "/cfg", nullptr };
    for (int i = 0; dirs[i]; i++) {
        if (!sd.exists(dirs[i])) {
            sd.mkdir(dirs[i]);
            Serial.printf("[SD] Created dir %s\n", dirs[i]);
        }
    }
    if (!sd.exists("/db/users.txt")) {
        File32 f;
        if (f.open("/db/users.txt", O_WRONLY | O_CREAT | O_TRUNC)) {
            f.print("[]");
            f.close();
            Serial.println("[SD] Created /db/users.txt");
        }
    }
}

// Public: (re)mount the SD card.  Called from initSD() and from sdReinit()
// when a runtime operation fails.  Returns true on success.
// Retries up to SD_MOUNT_RETRIES times with an exponential-ish back-off and
// a deliberate SPI bus reset between each attempt.
#define SD_MOUNT_RETRIES 4
static bool _sdMount() {
    // Clock speeds to try (MHz): slow first to handle lazy cards, then faster
    const uint8_t clks[] = { 10, 10, 20, 20 };
    const uint32_t delays_ms[] = { 0, 500, 1000, 2000 };

    for (int attempt = 0; attempt < SD_MOUNT_RETRIES; attempt++) {
        if (attempt > 0) {
            Serial.printf("[SD] Retry %d/%d after %lu ms…\n",
                          attempt, SD_MOUNT_RETRIES - 1, (unsigned long)delays_ms[attempt]);
            delay(delays_ms[attempt]);
        }
        if (_sdTryMount(clks[attempt])) {
            Serial.printf("[SD] Mounted OK on attempt %d (clk=%d MHz)\n",
                          attempt + 1, clks[attempt]);
            return true;
        }
        Serial.printf("[SD] Attempt %d failed\n", attempt + 1);
    }
    return false;
}

void initSD() {
    // Create the cross-task SD mutex BEFORE any SD operation.
    // Recursive so helpers that call other helpers don't self-deadlock.
    if (!_sdMutex) {
        _sdMutex = xSemaphoreCreateRecursiveMutex();
        configASSERT(_sdMutex);
    }

    if (!_sdMount()) {
        Serial.println("[SD] *** Mount failed after all retries – SD features disabled ***");
        Serial.println("[SD]     Check: card seated? FAT32 formatted? SPI wiring (CS=33)?");
        _sdOk = false;
        return;
    }

    _sdOk = true;
    _sdBootstrapFS();
    Serial.println("[SD] Ready  (LFN, retry-init, runtime remount enabled)");
}

// sdReinit – called internally when a runtime SD operation fails unexpectedly.
// Attempts a silent remount without disturbing the mutex.
// Returns true if the card came back online.
static bool sdReinit() {
    Serial.println("[SD] Attempting runtime remount…");
    if (_sdMount()) {
        _sdOk = true;
        _sdBootstrapFS();
        Serial.println("[SD] Runtime remount succeeded");
        return true;
    }
    _sdOk = false;
    Serial.println("[SD] Runtime remount failed – SD offline");
    return false;
}

// ─── SD state ────────────────────────────────────────────────────────────────
bool sdIsOk() { return _sdOk; }

// ─── Raw FACE.BIN helpers ─────────────────────────────────────────────────────
size_t getFaceBinSize() {
    if (!_sdOk || !sd.exists("/FACE.BIN")) return 0;
    File32 f;
    if (!f.open("/FACE.BIN", O_RDONLY)) return 0;
    size_t sz = (size_t)f.fileSize();
    f.close();
    return sz;
}

bool readFaceBinRaw(uint8_t *buf, size_t len) {
    if (!_sdOk) return false;
    File32 f;
    if (!f.open("/FACE.BIN", O_RDONLY)) return false;
    bool ok = ((size_t)f.read(buf, len) == len);
    f.close();
    return ok;
}

bool writeFaceBinRaw(const uint8_t *buf, size_t len) {
    if (!_sdOk) return false;
    File32 f;
    if (!f.open("/FACE.BIN", O_WRONLY | O_CREAT | O_TRUNC)) return false;
    bool ok = ((size_t)f.write(buf, len) == len);
    f.close();
    return ok;
}

// ─── Date helper (public) ────────────────────────────────────────────────────
String getDateDaysAgo(int daysAgo) { return dateStrDaysAgo(daysAgo); }


void listDir(const char *dirname, uint8_t levels) {
    File32 dir, entry;
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
    if (!_sdOk) { if (!sdReinit()) return; }
    if (!SD_TAKE()) { Serial.println("[SD] Mutex timeout: write FACE.BIN"); return; }
    File32 f;
    if (!f.open(path, O_WRONLY | O_CREAT | O_TRUNC)) {
        Serial.println("[SD] write FACE.BIN open failed – attempting remount");
        SD_GIVE();
        if (sdReinit() && SD_TAKE()) {
            if (!f.open(path, O_WRONLY | O_CREAT | O_TRUNC)) {
                Serial.println("[SD] write FACE.BIN failed after remount");
                SD_GIVE(); return;
            }
        } else return;
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
    SD_GIVE();
}

void read_face_id_name_list_sdcard(face_id_name_list *l, const char *path) {
    if (!_sdOk || !sd.exists(path)) {
        Serial.println("[SD] No FACE.BIN found");
        return;
    }
    if (!SD_TAKE()) { Serial.println("[SD] Mutex timeout: read FACE.BIN"); return; }
    File32 f;
    if (!f.open(path, O_RDONLY)) { SD_GIVE(); return; }

    uint8_t count = 0;
    if (f.read(&count, 1) != 1 || count == 0) { f.close(); SD_GIVE(); return; }

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
    SD_GIVE();
    Serial.printf("[SD] Loaded %d face(s)\n", l->count);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  User database  (JSON array at /db/users.txt)
// ═══════════════════════════════════════════════════════════════════════════════
String getUsersJSON() {
    // Fast path: serve from in-memory cache.
    // Cache is valid during the entire attendance session (user list unchanging).
    // It is invalidated by saveUserToDB(), deleteUserFromDB(), and factoryReset().
    if (_usersCacheOk && _usersCache.length() > 0) return _usersCache;

    if (!_sdOk) return "[]";
    if (!SD_TAKE()) {
        // Mutex timeout -- return stale cache if available, otherwise empty
        Serial.println("[SD] getUsersJSON: mutex timeout, serving stale/empty");
        return (_usersCache.length() > 0) ? _usersCache : "[]";
    }
    File32 f;
    if (!f.open("/db/users.txt", O_RDONLY)) { SD_GIVE(); return "[]"; }
    String s = sdReadAll(f);
    f.close();
    SD_GIVE();

    // Populate cache
    _usersCache   = (s.length() > 0) ? s : "[]";
    _usersCacheOk = true;
    return _usersCache;
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
    if (!SD_TAKE()) return false;

    File32 f;
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
            SD_GIVE();
            return false;
        }

    JsonObject nu  = arr.createNestedObject();
    nu["id"]      = user.id;
    nu["name"]    = user.name;
    nu["dept"]    = user.dept;
    nu["role"]    = (user.role[0] != '\0') ? user.role : "Student";
    nu["regDate"] = getCurrentDateStr();
    nu["faces"]   = 5;   // updated after enrolment completes

    if (!f.open("/db/users.txt", O_WRONLY | O_CREAT | O_TRUNC)) { SD_GIVE(); return false; }
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    if (ok) _invalidateUsersCache();  // force re-read on next getUsersJSON call
    SD_GIVE();
    Serial.printf("[DB] Saved user: %s  id: %s\n", user.name, user.id);
    return ok;
}

// deleteUserFromDB – takes const char* name instead of String.
bool deleteUserFromDB(const char *name) {
    if (!_sdOk) return false;
    if (!SD_TAKE()) return false;

    File32 f;
    if (!f.open("/db/users.txt", O_RDONLY)) { SD_GIVE(); return false; }
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); SD_GIVE(); return false; }
    f.close();

    JsonArray arr = doc.as<JsonArray>();
    for (int i = 0; i < (int)arr.size(); i++) {
        if (strcmp(arr[i]["name"] | "", name) == 0) {
            arr.remove(i);
            if (!f.open("/db/users.txt", O_WRONLY | O_CREAT | O_TRUNC)) { SD_GIVE(); return false; }
            bool ok = serializeJson(doc, f) > 0;
            f.close();
            if (ok) _invalidateUsersCache();  // force re-read on next getUsersJSON call
            SD_GIVE();
            return ok;
        }
    }
    SD_GIVE();
    return false;
}

// getUserByName – looks up a user in /db/users.txt by their display name.
// Uses the in-memory user cache (populated by getUsersJSON) to avoid a per-
// recognition SD read -- attendanceTask calls this on every face match, which
// was a primary source of SD mutex contention during attendance mode.
bool getUserByName(const char *name, UserRecord &out) {
    if (!_sdOk) return false;

    // Ensure cache is populated (getUsersJSON handles mutex and SD read)
    String json = getUsersJSON();
    if (json.length() < 3) return false;  // empty array "[]" = nothing to search

    // Parse from cached string -- no SD access, no mutex required
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, json) != DeserializationError::Ok) return false;

    for (JsonObject u : doc.as<JsonArray>()) {
        if (strcmp(u["name"] | "", name) == 0) {
            memset(&out, 0, sizeof(out));
            strncpy(out.id,   u["id"]   | name, sizeof(out.id)   - 1);
            strncpy(out.name, u["name"] | name, sizeof(out.name) - 1);
            strncpy(out.dept, u["dept"] | "",   sizeof(out.dept) - 1);
            strncpy(out.role, u["role"] | "Student", sizeof(out.role) - 1);
            return true;
        }
    }
    return false;  // not found – caller falls back to name-only
}

// logAttendance – auto path.
// Uses rec.uid / rec.name / rec.dept as input.
// date / time / status are computed here from the current clock.
void logAttendance(const AttendanceRecord &rec) {
    if (!_sdOk) return;
    if (!SD_TAKE()) return;

    String date    = getCurrentDateStr();
    String timeStr = getCurrentHHMM();
    String status  = computeStatus(timeStr.c_str());
    String fname   = "/atd/l_" + date + ".csv";

    // Duplicate check – skip if uid already logged today
    if (sd.exists(fname.c_str())) {
        File32 chk;
        if (chk.open(fname.c_str(), O_RDONLY)) {
            sdReadLine(chk);  // skip header
            while (chk.available()) {
                String line = sdReadLine(chk);
                if (line.indexOf(rec.uid) >= 0) {
                    chk.close();
                    SD_GIVE();  // ← was missing: mutex was permanently locked here
                    Serial.printf("[ATD] %s already logged today\n", rec.name);
                    return;  // duplicate
                }
            }
            chk.close();
        }
    }

    bool needHeader = !sd.exists(fname.c_str());
    File32 f;
    if (!f.open(fname.c_str(), O_WRONLY | O_CREAT | O_APPEND)) {
        Serial.println("[ATD] Log open failed – attempting SD remount");
        SD_GIVE();
        if (!sdReinit()) return;
        if (!SD_TAKE()) return;
        needHeader = !sd.exists(fname.c_str());
        if (!f.open(fname.c_str(), O_WRONLY | O_CREAT | O_APPEND)) {
            Serial.println("[ATD] Log open failed after remount – dropping record");
            SD_GIVE(); return;
        }
    }
    if (needHeader) f.println("UID,Name,Department,Date,Time,Status,Confidence");

    const char *conf = (rec.confidence[0] != '\0') ? rec.confidence : "92%";
    char line[256];
    snprintf(line, sizeof(line), "%s,%s,%s,%s,%s,%s,%s\n",
             rec.uid, rec.name, rec.dept,
             date.c_str(), timeStr.c_str(), status.c_str(), conf);
    f.print(line);
    f.close();
    SD_GIVE();
    Serial.printf("[ATD] Logged: %s (%s) – %s – %s\n",
                  rec.name, rec.uid, timeStr.c_str(), status.c_str());
}

// manualAttendance – admin override.
bool manualAttendance(const AttendanceRecord &rec) {
    if (!_sdOk) return false;
    if (!SD_TAKE()) return false;

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
        File32 r;
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
                File32 w;
                if (!w.open(fname.c_str(), O_WRONLY | O_CREAT | O_TRUNC)) { SD_GIVE(); return false; }
                w.print(rebuilt);
                w.close();
                Serial.printf("[ATD] Manual override: %s -> %s\n", rec.uid, status);
                SD_GIVE();
                return true;
            }
        }
    }

    // Append new record
    File32 f;
    if (!f.open(fname.c_str(), O_WRONLY | O_CREAT | O_APPEND)) { SD_GIVE(); return false; }
    if (needHeader) f.println("UID,Name,Department,Date,Time,Status,Confidence");

    char lineBuf[256];
    snprintf(lineBuf, sizeof(lineBuf), "%s,%s,,%s,%s,%s,Manual\n",
             rec.uid, name, date, timeStr, status);
    f.print(lineBuf);
    f.close();
    SD_GIVE();
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
    File32 f;
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
    if (SD_TAKE()) {
        parseLogFile(fname, arr, dept, status, search);
        SD_GIVE();
    }
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

    int total   = getUserCount();   // getUserCount → getUsersJSON already takes mutex
    int sumPres = 0;

    if (!SD_TAKE()) {
        String out; serializeJson(doc, out); return out;
    }
    for (int i = days-1; i >= 0; i--) {
        String dStr = dateStrDaysAgo(i);
        String lbl  = (days <= 14) ? dayLabel(i) : dStr.substring(5);
        labels.add(lbl);

        String fname = "/atd/l_" + dStr + ".csv";
        int p=0, a=0, l=0;
        if (sd.exists(fname.c_str())) {
            File32 f;
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
    SD_GIVE();

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
    if (!SD_TAKE()) return "UID,Name,Department,Date,Time,Status,Confidence\n";
    if (!sd.exists(fname.c_str())) { SD_GIVE(); return "UID,Name,Department,Date,Time,Status,Confidence\n"; }
    File32 f;
    if (!f.open(fname.c_str(), O_RDONLY)) { SD_GIVE(); return "UID,Name,Department,Date,Time,Status,Confidence\n"; }
    String c = sdReadAll(f);
    f.close();
    SD_GIVE();
    return c;
}

bool clearAttendanceLogs(String date) {
    if (date == "") date = getCurrentDateStr();
    String fname = "/atd/l_" + date + ".csv";
    if (!SD_TAKE()) return false;
    if (!sd.exists(fname.c_str())) { SD_GIVE(); return true; }
    bool ok = sd.remove(fname.c_str());
    SD_GIVE();
    return ok;
}
// ─── factoryReset ─────────────────────────────────────────────────────────────
// Wipes the SD card to a completely clean slate:
//   * Deletes every file inside /atd/   (all attendance CSV logs)
//   * Deletes /FACE.BIN                 (all face embeddings)
//   * Resets /db/users.txt to []        (empty user database)
//   * Deletes /cfg/settings.json        (settings revert to firmware defaults)
// Directory structure (/atd, /db, /cfg) is recreated immediately after.
// Returns true on success. The caller must also clear the in-memory id_list.
bool factoryReset() {
    if (!_sdOk) return false;
    if (!SD_TAKE()) { Serial.println("[RESET] Mutex timeout"); return false; }

    Serial.println("[RESET] Factory reset started...");

    // 1. Delete all attendance CSV files
    if (sd.exists("/atd")) {
        File32 dir, entry;
        if (dir.open("/atd", O_RDONLY)) {
            while (entry.openNext(&dir, O_RDONLY)) {
                char fname[64] = {0};
                entry.getName(fname, sizeof(fname));
                bool isDir = entry.isDirectory();
                entry.close();
                if (!isDir) {
                    String path = "/atd/" + String(fname);
                    sd.remove(path.c_str());
                    Serial.printf("[RESET] Removed %s\n", path.c_str());
                }
            }
            dir.close();
        }
    }

    // 2. Delete face embeddings
    if (sd.exists("/FACE.BIN")) {
        sd.remove("/FACE.BIN");
        Serial.println("[RESET] Removed /FACE.BIN");
    }

    // 3. Reset user database to empty array
    {
        File32 f;
        if (f.open("/db/users.txt", O_WRONLY | O_CREAT | O_TRUNC)) {
            f.print("[]");
            f.close();
            Serial.println("[RESET] Reset /db/users.txt to []");
        } else {
            Serial.println("[RESET] WARNING: Could not reset /db/users.txt");
        }
    }

    // 4. Delete settings (device reverts to firmware defaults on reload)
    if (sd.exists("/cfg/settings.json")) {
        sd.remove("/cfg/settings.json");
        Serial.println("[RESET] Removed /cfg/settings.json");
    }

    SD_GIVE();

    // Recreate the directory skeleton
    _sdBootstrapFS();

    // Invalidate user cache -- DB is now an empty array
    _invalidateUsersCache();

    Serial.println("[RESET] Factory reset complete -- clean slate");
    return true;
}



// ═══════════════════════════════════════════════════════════════════════════════
//  Dashboard stats
// ═══════════════════════════════════════════════════════════════════════════════
String getStatsJSON() {
    String date  = getCurrentDateStr();
    String fname = "/atd/l_" + date + ".csv";
    int p=0, a=0, l=0;

    if (!SD_TAKE()) {
        // Return safe defaults if mutex unavailable
        DynamicJsonDocument d(256);
        d["total"]=0;d["present"]=0;d["absent"]=0;d["late"]=0;
        d["storage"]="--";d["uptime"]="--";d["ntpSynced"]=ntpSynced;
        d["date"]=date;d["time"]=getCurrentHHMM();
        String s; serializeJson(d,s); return s;
    }
    if (sd.exists(fname.c_str())) {
        File32 f;
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

    // Storage (SdFat32 API)
    String storageStr = "--";
    if (_sdOk) {
        uint32_t bytesPerCluster = sd.vol()->bytesPerCluster();
        uint32_t usedClusters    = sd.vol()->clusterCount()
                                 - sd.vol()->freeClusterCount();
        uint64_t usedBytes       = (uint64_t)usedClusters * bytesPerCluster;
        storageStr = String((double)usedBytes / 1024.0) + " KB";
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
    SD_GIVE();
    return out;
}

String getStorageJSON() {
    DynamicJsonDocument doc(256);
    if (_sdOk && SD_TAKE()) {
        uint32_t bytesPerCluster = sd.vol()->bytesPerCluster();
        uint32_t totalClusters   = sd.vol()->clusterCount();
        uint32_t freeClusters    = sd.vol()->freeClusterCount();
        uint64_t totalBytes      = (uint64_t)totalClusters * bytesPerCluster;
        uint64_t freeBytes       = (uint64_t)freeClusters  * bytesPerCluster;
        uint64_t usedBytes       = totalBytes - freeBytes;
        doc["total"] = String((double)totalBytes / (1024.0*1024.0)) + " MB";
        doc["used"]  = String((double)usedBytes  / (1024.0*1024.0)) + " MB";
        doc["free"]  = String((double)freeBytes  / (1024.0*1024.0)) + " MB";
        doc["pct"]   = (totalBytes > 0) ? (int)((double)usedBytes / totalBytes * 100) : 0;
        SD_GIVE();
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
    if (!SD_TAKE()) return false;
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

    File32 f;
    if (!f.open("/cfg/settings.json", O_WRONLY | O_CREAT | O_TRUNC)) { SD_GIVE(); return false; }
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    SD_GIVE();
    Serial.println("[CFG] Settings saved");
    return ok;
}

bool loadSettings(AttendanceSettings &s) {
    setDefaultSettings(s);   // always start with defaults
    if (!_sdOk) return false;
    if (!SD_TAKE()) return false;
    if (!sd.exists("/cfg/settings.json")) { SD_GIVE(); return false; }

    File32 f;
    if (!f.open("/cfg/settings.json", O_RDONLY)) { SD_GIVE(); return false; }
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); SD_GIVE(); return false; }
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
    SD_GIVE();
    Serial.println("[CFG] Settings loaded");
    return true;
}

} // namespace Bridge
