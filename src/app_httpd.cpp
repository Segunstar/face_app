// app_httpd.cpp  –  FaceGuard Pro (ESP32-CAM)
// HTTP server with all API endpoints for the admin portal.

#define LOG_LOCAL_LEVEL ESP_LOG_INFO

#include "esp_http_server.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "sdkconfig.h"
#include "camera_index.h"   // login_html
#include "main_page.h"      // index_html  (FaceGuard Pro portal)
#include "esp_log.h"
#include "fd_forward.h"
#include "fr_forward.h"
#include "global.h"
#include "sd_card.h"

#include <sys/time.h>
#include <algorithm>
#include <WiFi.h>

static const char *TAG = "HTTPD";

#define ENABLE_SD_TEST 1
#if ENABLE_SD_TEST
  #include "fr_flash.h"
#endif

// ─── Admin credentials (change before production!) ───────────────────────────
#define ADMIN_USER "admin"
#define ADMIN_PASS "1234"
bool authenticated = false;

// ─── Face paths ──────────────────────────────────────────────────────────────
const char *myFilePath = "/FACE.BIN";

// ─── Face detection / recognition config (defined here, extern in global.h) ──
mtmn_config_t     mtmn_config       = {0};
face_id_name_list id_list            = {0};
int8_t            detection_enabled  = 0;
int8_t            recognition_enabled = 0;
int8_t            is_enrolling        = 0;

// ─── Enrolment context (replaces three loose String globals) ─────────────────
// Set by api_enroll_capture_handler; read by run_face_recognition.
EnrollContext enrollCtx = {};

// ─── MJPEG streaming constants ────────────────────────────────────────────────
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

#define ENROLL_CONFIRM_TIMES 5
#define FACE_ID_SAVE_NUMBER  7
#define FACE_COLOR_WHITE  0x00FFFFFF
#define FACE_COLOR_RED    0x000000FF
#define FACE_COLOR_GREEN  0x0000FF00
#define FACE_COLOR_YELLOW (FACE_COLOR_RED | FACE_COLOR_GREEN)

// ─── Helper: URL-decode a string ──────────────────────────────────────────────
static String urlDecode(const char *src) {
    String out;
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            a = tolower(a); b = tolower(b);
            int hi = (a >= 'a') ? a-'a'+10 : a-'0';
            int lo = (b >= 'a') ? b-'a'+10 : b-'0';
            out += (char)(hi * 16 + lo);
            src += 3;
        } else if (*src == '+') {
            out += ' '; src++;
        } else {
            out += *src++;
        }
    }
    return out;
}

// ─── Helper: parse POST body form field ───────────────────────────────────────
static String getFormField(const String &body, const String &key) {
    String search = key + "=";
    int start = body.indexOf(search);
    if (start < 0) return "";
    start += search.length();
    int end = body.indexOf('&', start);
    String raw = (end < 0) ? body.substring(start) : body.substring(start, end);
    raw.trim();
    return urlDecode(raw.c_str());
}

// ─── GFX helpers ──────────────────────────────────────────────────────────────
static void rgb_print(dl_matrix3du_t *im, uint32_t color, const char *str) {
    fb_data_t fb;
    fb.width = im->w; fb.height = im->h; fb.data = im->item;
    fb.bytes_per_pixel = 3; fb.format = FB_BGR888;
    fb_gfx_print(&fb, (fb.width - (strlen(str)*14))/2, 10, color, str);
}

static void draw_face_boxes(dl_matrix3du_t *im, box_array_t *boxes, int fid) {
    uint32_t color = FACE_COLOR_YELLOW;
    if (fid < 0) color = FACE_COLOR_RED;
    else if (fid > 0) color = FACE_COLOR_GREEN;
    fb_data_t fb;
    fb.width = im->w; fb.height = im->h; fb.data = im->item;
    fb.bytes_per_pixel = 3; fb.format = FB_BGR888;
    for (int i = 0; i < boxes->len; i++) {
        int x = (int)boxes->box[i].box_p[0], y = (int)boxes->box[i].box_p[1];
        int w = (int)boxes->box[i].box_p[2]-x+1, h = (int)boxes->box[i].box_p[3]-y+1;
        fb_gfx_drawFastHLine(&fb, x, y, w, color);
        fb_gfx_drawFastHLine(&fb, x, y+h-1, w, color);
        fb_gfx_drawFastVLine(&fb, x, y, h, color);
        fb_gfx_drawFastVLine(&fb, x+w-1, y, h, color);
    }
}

// ─── Face recognition runner ──────────────────────────────────────────────────
static int run_face_recognition(dl_matrix3du_t *im, box_array_t *net_boxes,
                                 const char *enrollName) {
    char cname[ENROLL_NAME_LEN];
    strncpy(cname, enrollName, ENROLL_NAME_LEN-1);
    cname[ENROLL_NAME_LEN-1] = '\0';

    dl_matrix3du_t *aligned = dl_matrix3du_alloc(1, FACE_WIDTH, FACE_HEIGHT, 3);
    if (!aligned) return 0;
    int matched = 0;

    if (align_face(net_boxes, im, aligned) == ESP_OK) {
        dl_matrix3d_t *face_id = get_face_id(aligned);

        if (is_enrolling == 1) {
            int8_t left = enroll_face_with_name(&id_list, face_id, cname);
            if (left == 0) {
                is_enrolling = 0;
                Bridge::write_face_id_name_list_sdcard(&id_list, myFilePath);
                Serial.printf("[ENROLL] Enrolled '%s' – saved to SD\n", cname);
            } else {
                Serial.printf("[ENROLL] %d captures left for '%s'\n", left, cname);
            }
        } else {
            face_id_node *node = recognize_face_with_name(&id_list, face_id);
            if (node) {
                matched = 1;
                rgb_print(im, FACE_COLOR_GREEN, "Recognised");

                // Look up the full user record from users.txt by name.
                // match->id_name is just the display name stored in FACE.BIN.
                // getUserByName fills in the proper uid and dept.
                UserRecord user;
                bool found = Bridge::getUserByName(node->id_name, user);

                AttendanceRecord rec = {};
                if (found) {
                    strncpy(rec.uid,  user.id,   sizeof(rec.uid)  - 1);
                    strncpy(rec.name, user.name, sizeof(rec.name) - 1);
                    strncpy(rec.dept, user.dept, sizeof(rec.dept) - 1);
                } else {
                    // User not in DB (enrolled but not registered) – use name as fallback
                    strncpy(rec.uid,  node->id_name, sizeof(rec.uid)  - 1);
                    strncpy(rec.name, node->id_name, sizeof(rec.name) - 1);
                }
                Bridge::logAttendance(rec);
            } else {
                rgb_print(im, FACE_COLOR_RED, "Unknown");
                matched = -1;
            }
        }
        dl_matrix3d_free(face_id);
    }
    dl_matrix3du_free(aligned);
    return matched;
}

// ══════════════════════════════════════════════════════════════════════════════
//  STREAM HANDLER (port 81)
// ══════════════════════════════════════════════════════════════════════════════
static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t    *fb           = NULL;
    struct timeval  ts;
    esp_err_t       res          = ESP_OK;
    size_t          jpg_len      = 0;
    uint8_t        *jpg_buf      = NULL;
    char            part_buf[128];
    dl_matrix3du_t *image_matrix = NULL;

    httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) { res = ESP_FAIL; break; }
        gettimeofday(&ts, NULL);

        if (!detection_enabled || fb->width > 400) {
            if (fb->format != PIXFORMAT_JPEG) {
                bool ok = frame2jpg(fb, 80, &jpg_buf, &jpg_len);
                esp_camera_fb_return(fb); fb = NULL;
                if (!ok) res = ESP_FAIL;
            } else { jpg_len = fb->len; jpg_buf = fb->buf; }
        } else {
            image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
            if (!image_matrix) { res = ESP_FAIL; }
            else {
                if (!fmt2rgb888(fb->buf, fb->len, fb->format, image_matrix->item)) {
                    res = ESP_FAIL;
                } else {
                    box_array_t *boxes = face_detect(image_matrix, &mtmn_config);
                    if (boxes) {
                        int fid = 0;
                        if (recognition_enabled)
                            // Pass enrollCtx.name into the recognition runner
                            fid = run_face_recognition(image_matrix, boxes,
                                                       enrollCtx.name);
                        draw_face_boxes(image_matrix, boxes, fid);
                        dl_lib_free(boxes->score); dl_lib_free(boxes->box);
                        if (boxes->landmark) dl_lib_free(boxes->landmark);
                        dl_lib_free(boxes);
                    }
                    if (!fmt2jpg(image_matrix->item, fb->width*fb->height*3,
                                 fb->width, fb->height, PIXFORMAT_RGB888, 90,
                                 &jpg_buf, &jpg_len)) {
                        ESP_LOGE(TAG, "fmt2jpg failed");
                    }
                }
                dl_matrix3du_free(image_matrix);
                esp_camera_fb_return(fb); fb = NULL;
            }
        }

        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        if (res == ESP_OK) {
            size_t hlen = snprintf(part_buf, 128, _STREAM_PART,
                                   jpg_len, (int)ts.tv_sec, (int)ts.tv_usec);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
        }
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, (const char*)jpg_buf, jpg_len);

        if (fb) esp_camera_fb_return(fb);
        else if (jpg_buf) free(jpg_buf);
        jpg_buf = NULL;

        if (res != ESP_OK) break;
    }
    return res;
}

// ══════════════════════════════════════════════════════════════════════════════
//  PAGE HANDLERS
// ══════════════════════════════════════════════════════════════════════════════

static esp_err_t send_progmem_html(httpd_req_t *req, const char *html) {
    httpd_resp_set_type(req, "text/html");
    const size_t CHUNK = 2048;
    size_t total = strlen(html);
    size_t sent  = 0;
    while (sent < total) {
        size_t to_send = std::min(CHUNK, total - sent);
        esp_err_t r = httpd_resp_send_chunk(req, html + sent, (ssize_t)to_send);
        if (r != ESP_OK) return r;
        sent += to_send;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t index_handler(httpd_req_t *req) {
    if (!authenticated) return send_progmem_html(req, login_html);
    return send_progmem_html(req, index_html);
}

static esp_err_t login_post_handler(httpd_req_t *req) {
    char buf[128] = {0};
    int ret = httpd_req_recv(req, buf,
                             std::min((size_t)req->content_len, sizeof(buf)-1));
    if (ret <= 0) return ESP_FAIL;

    if (strstr(buf, "user=" ADMIN_USER) && strstr(buf, "pass=" ADMIN_PASS)) {
        authenticated    = true;
        isAttendanceMode = false;
        Serial.println("[AUTH] Admin logged in");
    } else {
        authenticated = false;
        Serial.println("[AUTH] Login failed");
    }
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t logout_handler(httpd_req_t *req) {
    authenticated    = false;
    isAttendanceMode = true;
    Serial.println("[AUTH] Admin logged out – resuming attendance mode");
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

// ─── Inline CORS helper ───────────────────────────────────────────────────────
static void set_cors_headers(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin",  "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

static esp_err_t send_json(httpd_req_t *req, const String &json) {
    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), (ssize_t)json.length());
}

// ══════════════════════════════════════════════════════════════════════════════
//  DASHBOARD / STATUS API
// ══════════════════════════════════════════════════════════════════════════════

static esp_err_t api_stats_handler   (httpd_req_t *req) { return send_json(req, Bridge::getStatsJSON());   }
static esp_err_t api_status_handler  (httpd_req_t *req) { return send_json(req, Bridge::getStatusJSON());  }
static esp_err_t api_storage_handler (httpd_req_t *req) { return send_json(req, Bridge::getStorageJSON()); }

static esp_err_t api_sync_ntp_handler(httpd_req_t *req) {
    Bridge::syncNTP();
    set_cors_headers(req);
    return httpd_resp_send(req, "NTP synced OK", HTTPD_RESP_USE_STRLEN);
}

// ══════════════════════════════════════════════════════════════════════════════
//  USER API
// ══════════════════════════════════════════════════════════════════════════════

static esp_err_t api_users_handler(httpd_req_t *req) {
    return send_json(req, Bridge::getUsersJSON());
}

// Runtime face-list deletion helper
static int8_t delete_face_runtime(face_id_name_list *l, const char *name) {
    if (l->count == 0) return -1;
    face_id_node *p = l->head, *prev = NULL;
    while (p) {
        if (strcmp(p->id_name, name) == 0) {
            if (p->id_vec) dl_matrix3d_free(p->id_vec);
            if (!prev) { l->head = p->next; if (p == l->tail) l->tail = NULL; }
            else       { prev->next = p->next; if (p == l->tail) l->tail = prev; }
            dl_lib_free(p); l->count--;
            if (l->count == 0) { l->head = nullptr; l->tail = nullptr; }
            return 0;
        }
        prev = p; p = p->next;
    }
    return -1;
}

static esp_err_t api_delete_handler(httpd_req_t *req) {
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char name[64] = {0};
        if (httpd_query_key_value(buf, "name", name, sizeof(name)) == ESP_OK) {
            Bridge::deleteUserFromDB(name);
            if (delete_face_runtime(&id_list, name) == 0) {
                Bridge::write_face_id_name_list_sdcard(&id_list, myFilePath);
                Serial.printf("[DB] Deleted user '%s'\n", name);
            }
        }
    }
    set_cors_headers(req);
    return httpd_resp_send(req, "OK", 2);
}

// ══════════════════════════════════════════════════════════════════════════════
//  ENROLMENT API
// ══════════════════════════════════════════════════════════════════════════════

static esp_err_t api_enroll_mode_handler(httpd_req_t *req) {
    char buf[64];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char val[4];
        if (httpd_query_key_value(buf, "active", val, sizeof(val)) == ESP_OK) {
            if (atoi(val) == 1) {
                detection_enabled    = 1;
                recognition_enabled  = 1;
                Serial.println("[ENROLL] Enrol mode ON");
            } else {
                detection_enabled    = 0;
                recognition_enabled  = 0;
                is_enrolling         = 0;
                memset(&enrollCtx, 0, sizeof(enrollCtx));
                Serial.println("[ENROLL] Enrol mode OFF");
            }
        }
    }
    set_cors_headers(req);
    return httpd_resp_send(req, "OK", 2);
}

static esp_err_t api_enroll_capture_handler(httpd_req_t *req) {
    char buf[256];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char id[32]={0}, name[64]={0}, dept[64]={0};
        if (httpd_query_key_value(buf, "id",   id,   sizeof(id))   == ESP_OK &&
            httpd_query_key_value(buf, "name", name, sizeof(name)) == ESP_OK) {

            httpd_query_key_value(buf, "dept", dept, sizeof(dept));

            // Fill EnrollContext struct (replaces newUserId / newUserName / newUserDept)
            strncpy(enrollCtx.id,   urlDecode(id).c_str(),   sizeof(enrollCtx.id)   - 1);
            strncpy(enrollCtx.name, urlDecode(name).c_str(), sizeof(enrollCtx.name) - 1);
            strncpy(enrollCtx.dept, urlDecode(dept).c_str(), sizeof(enrollCtx.dept) - 1);

            // Save user to DB using UserRecord struct
            UserRecord user = UserRecord::make(enrollCtx.id, enrollCtx.name,
                                               enrollCtx.dept, "Student");
            Bridge::saveUserToDB(user);

            is_enrolling = 1;
            Serial.printf("[ENROLL] Capturing for '%s' (id=%s)\n",
                          enrollCtx.name, enrollCtx.id);
            set_cors_headers(req);
            return httpd_resp_send(req, "Capturing...", HTTPD_RESP_USE_STRLEN);
        }
    }
    return httpd_resp_send_500(req);
}

// ══════════════════════════════════════════════════════════════════════════════
//  ATTENDANCE API
// ══════════════════════════════════════════════════════════════════════════════

// GET /api/logs?date=YYYY-MM-DD&dept=X&status=X&search=X
static esp_err_t api_logs_handler(httpd_req_t *req) {
    char buf[256] = {0};
    String date="", dept="", status="", search="";
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char tmp[128];
        if (httpd_query_key_value(buf, "date",   tmp, sizeof(tmp)) == ESP_OK) date   = urlDecode(tmp);
        if (httpd_query_key_value(buf, "dept",   tmp, sizeof(tmp)) == ESP_OK) dept   = urlDecode(tmp);
        if (httpd_query_key_value(buf, "status", tmp, sizeof(tmp)) == ESP_OK) status = String(tmp);
        if (httpd_query_key_value(buf, "search", tmp, sizeof(tmp)) == ESP_OK) search = urlDecode(tmp);
    }
    return send_json(req, Bridge::getLogsJSON(date, dept, status, search));
}

// GET /api/logs_range?days=N
static esp_err_t api_logs_range_handler(httpd_req_t *req) {
    char buf[64] = {0};
    int days = 7;
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char tmp[8];
        if (httpd_query_key_value(buf, "days", tmp, sizeof(tmp)) == ESP_OK)
            days = atoi(tmp);
    }
    if (days < 1)  days = 1;
    if (days > 90) days = 90;
    return send_json(req, Bridge::getLogsRange(days));
}

// POST /api/manual_attendance  (body: uid=&name=&date=&status=&time=&notes=)
static esp_err_t api_manual_handler(httpd_req_t *req) {
    int len = req->content_len;
    if (len <= 0 || len > 512) return httpd_resp_send_500(req);
    char *buf = (char*)malloc(len+1);
    if (!buf) return httpd_resp_send_500(req);
    int ret = httpd_req_recv(req, buf, len);
    buf[ret > 0 ? ret : 0] = '\0';
    String body = String(buf);
    free(buf);

    // Build AttendanceRecord from form fields (replaces 6 loose String locals)
    AttendanceRecord rec = {};
    strncpy(rec.uid,    getFormField(body, "uid").c_str(),    sizeof(rec.uid)    - 1);
    strncpy(rec.name,   getFormField(body, "name").c_str(),   sizeof(rec.name)   - 1);
    strncpy(rec.date,   getFormField(body, "date").c_str(),   sizeof(rec.date)   - 1);
    strncpy(rec.status, getFormField(body, "status").c_str(), sizeof(rec.status) - 1);
    strncpy(rec.time,   getFormField(body, "time").c_str(),   sizeof(rec.time)   - 1);
    strncpy(rec.notes,  getFormField(body, "notes").c_str(),  sizeof(rec.notes)  - 1);

    if (rec.uid[0] == '\0') {
        set_cors_headers(req);
        return httpd_resp_send(req, "FAIL: uid required", HTTPD_RESP_USE_STRLEN);
    }
    bool ok = Bridge::manualAttendance(rec);
    set_cors_headers(req);
    return httpd_resp_send(req, ok ? "OK" : "FAIL: write error", HTTPD_RESP_USE_STRLEN);
}

// GET /api/download_csv?date=YYYY-MM-DD
static esp_err_t api_download_csv_handler(httpd_req_t *req) {
    char buf[64] = {0};
    String date = "";
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char tmp[32];
        if (httpd_query_key_value(buf, "date", tmp, sizeof(tmp)) == ESP_OK)
            date = String(tmp);
    }
    String csv = Bridge::downloadAttendanceCSV(date);
    set_cors_headers(req);
    httpd_resp_set_type(req, "text/csv");
    String fname = "attachment; filename=attendance_"
                 + (date.length() ? date : Bridge::getCurrentDateStr()) + ".csv";
    httpd_resp_set_hdr(req, "Content-Disposition", fname.c_str());
    return httpd_resp_send(req, csv.c_str(), (ssize_t)csv.length());
}

// GET /api/clear_logs?date=YYYY-MM-DD
static esp_err_t api_clear_logs_handler(httpd_req_t *req) {
    char buf[64] = {0};
    String date = "";
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char tmp[32];
        if (httpd_query_key_value(buf, "date", tmp, sizeof(tmp)) == ESP_OK)
            date = String(tmp);
    }
    bool ok = Bridge::clearAttendanceLogs(date);
    set_cors_headers(req);
    return httpd_resp_send(req, ok ? "OK" : "FAIL", HTTPD_RESP_USE_STRLEN);
}

// ══════════════════════════════════════════════════════════════════════════════
//  SETTINGS API
// ══════════════════════════════════════════════════════════════════════════════

// GET /api/settings
static esp_err_t api_settings_get_handler(httpd_req_t *req) {
    char j[512];
    snprintf(j, sizeof(j),
        "{\"startTime\":\"%s\",\"endTime\":\"%s\","
        "\"lateTime\":\"%s\",\"absentTime\":\"%s\","
        "\"confidence\":%d,\"buzzerEnabled\":%s,\"autoMode\":%s,"
        "\"gmtOffsetSec\":%ld,\"ntpServer\":\"%s\"}",
        gSettings.startTime, gSettings.endTime,
        gSettings.lateTime,  gSettings.absentTime,
        gSettings.confidence,
        gSettings.buzzerEnabled ? "true" : "false",
        gSettings.autoMode      ? "true" : "false",
        gSettings.gmtOffsetSec,
        gSettings.ntpServer);
    return send_json(req, String(j));
}

// POST /api/settings  (form-encoded body)
static esp_err_t api_settings_post_handler(httpd_req_t *req) {
    int len = req->content_len;
    if (len <= 0 || len > 512) return httpd_resp_send_500(req);
    char *buf = (char*)malloc(len+1);
    if (!buf) return httpd_resp_send_500(req);
    int ret = httpd_req_recv(req, buf, len);
    buf[ret > 0 ? ret : 0] = '\0';
    String body = String(buf);
    free(buf);

    String s;
    s = getFormField(body, "startTime");    if (s.length()) strncpy(gSettings.startTime,    s.c_str(), 5);
    s = getFormField(body, "endTime");      if (s.length()) strncpy(gSettings.endTime,      s.c_str(), 5);
    s = getFormField(body, "lateTime");     if (s.length()) strncpy(gSettings.lateTime,     s.c_str(), 5);
    s = getFormField(body, "absentTime");   if (s.length()) strncpy(gSettings.absentTime,   s.c_str(), 5);
    s = getFormField(body, "ntpServer");    if (s.length()) strncpy(gSettings.ntpServer,    s.c_str(), 63);
    s = getFormField(body, "gmtOffsetSec"); if (s.length()) gSettings.gmtOffsetSec = s.toInt();
    s = getFormField(body, "confidence");   if (s.length()) gSettings.confidence   = s.toInt();
    s = getFormField(body, "buzzerEnabled"); gSettings.buzzerEnabled = (s == "1");
    s = getFormField(body, "autoMode");      gSettings.autoMode      = (s == "1");

    Bridge::saveSettings(gSettings);
    Serial.println("[CFG] Settings updated via portal");
    set_cors_headers(req);
    return httpd_resp_send(req, "OK", 2);
}

// ══════════════════════════════════════════════════════════════════════════════
//  startCameraServer()
// ══════════════════════════════════════════════════════════════════════════════
void startCameraServer() {
    mtmn_config.type                         = FAST;
    mtmn_config.min_face                     = 80;
    mtmn_config.pyramid                      = 0.707f;
    mtmn_config.pyramid_times                = 4;
    mtmn_config.p_threshold.score            = 0.6f;
    mtmn_config.p_threshold.nms              = 0.7f;
    mtmn_config.p_threshold.candidate_number = 20;
    mtmn_config.r_threshold.score            = 0.7f;
    mtmn_config.r_threshold.nms              = 0.7f;
    mtmn_config.r_threshold.candidate_number = 10;
    mtmn_config.o_threshold.score            = 0.7f;
    mtmn_config.o_threshold.nms              = 0.7f;
    mtmn_config.o_threshold.candidate_number = 1;

    face_id_name_init(&id_list, FACE_ID_SAVE_NUMBER, ENROLL_CONFIRM_TIMES);

#if ENABLE_SD_TEST
    Bridge::read_face_id_name_list_sdcard(&id_list, myFilePath);
#endif

    httpd_config_t cfg  = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 24;
    cfg.stack_size       = 8192;

    httpd_uri_t uris[] = {
        {"/",                     HTTP_GET,  index_handler,              NULL},
        {"/login",                HTTP_POST, login_post_handler,         NULL},
        {"/logout",               HTTP_GET,  logout_handler,             NULL},
        // Dashboard
        {"/api/stats",            HTTP_GET,  api_stats_handler,          NULL},
        {"/api/status",           HTTP_GET,  api_status_handler,         NULL},
        {"/api/storage",          HTTP_GET,  api_storage_handler,        NULL},
        {"/api/sync_ntp",         HTTP_GET,  api_sync_ntp_handler,       NULL},
        // Users
        {"/api/users",            HTTP_GET,  api_users_handler,          NULL},
        {"/api/delete_user",      HTTP_GET,  api_delete_handler,         NULL},
        // Enrolment
        {"/api/enroll_mode",      HTTP_GET,  api_enroll_mode_handler,    NULL},
        {"/api/enroll_capture",   HTTP_GET,  api_enroll_capture_handler, NULL},
        // Attendance
        {"/api/logs",             HTTP_GET,  api_logs_handler,           NULL},
        {"/api/logs_range",       HTTP_GET,  api_logs_range_handler,     NULL},
        {"/api/manual_attendance",HTTP_POST, api_manual_handler,         NULL},
        {"/api/download_csv",     HTTP_GET,  api_download_csv_handler,   NULL},
        {"/api/clear_logs",       HTTP_GET,  api_clear_logs_handler,     NULL},
        // Settings
        {"/api/settings",         HTTP_GET,  api_settings_get_handler,   NULL},
        {"/api/settings",         HTTP_POST, api_settings_post_handler,  NULL},
    };

    if (httpd_start(&camera_httpd, &cfg) == ESP_OK) {
        for (auto &u : uris)
            httpd_register_uri_handler(camera_httpd, &u);
        Serial.println("[HTTP] Main server started on port 80");
    } else {
        Serial.println("[HTTP] Failed to start main server!");
    }

    // Stream server on port 81
    cfg.server_port += 1;
    cfg.ctrl_port   += 1;
    httpd_uri_t stream_uri = {"/stream", HTTP_GET, stream_handler, NULL};
    if (httpd_start(&stream_httpd, &cfg) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
        Serial.println("[HTTP] Stream server started on port 81");
    }
}
