#define LOG_LOCAL_LEVEL ESP_LOG_INFO

#include "esp_http_server.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "sdkconfig.h"
#include "camera_index.h"
#include "main_page.h"
#include "esp_log.h"
#include "fd_forward.h"
#include "fr_forward.h"
#include "global.h"
// Added these for the timestamp and min function fixes
#include <sys/time.h>
#include <algorithm>

#define ENABLE_SD_TEST 1

#if ENABLE_SD_TEST
#include "fr_flash.h"
#include "sd_card.h"
#endif

static const char* TAG = "HTTP code";
const char* myFilePath = "/face.bin";
const char* DB = "/db";
const char* pathToDB = "/db/users.txt";

#define CONFIG_ESP_FACE_DETECT_ENABLED 1
#define CONFIG_ESP_FACE_RECOGNITION_ENABLED 1

#define FACE_COLOR_WHITE 0x00FFFFFF
#define FACE_COLOR_BLACK 0x00000000
#define FACE_COLOR_RED 0x000000FF
#define FACE_COLOR_GREEN 0x0000FF00
#define FACE_COLOR_BLUE 0x00FF0000
#define FACE_COLOR_YELLOW (FACE_COLOR_RED | FACE_COLOR_GREEN)
#define FACE_COLOR_CYAN (FACE_BLUE | FACE_COLOR_GREEN)
#define FACE_COLOR_PURPLE (FACE_COLOR_BLUE | FACE_COLOR_RED)

#define ADMIN_USER "admin"
#define ADMIN_PASS "1234"
bool authenticated = false;

typedef struct {
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

mtmn_config_t mtmn_config = {0};

#if CONFIG_ESP_FACE_RECOGNITION_ENABLED
// static int8_t is_enrolling = 0;
int8_t recognition_enabled = 0;
int8_t is_enrolling = 0;
face_id_name_list id_list = {0};
// static face_id_list id_list = {0};
#endif

#define ENROLL_CONFIRM_TIMES 5
#define FACE_ID_SAVE_NUMBER 7
int8_t detection_enabled = 0;

String newUserId;
 String newUserName;
 String newUserDept;

static void rgb_print(dl_matrix3du_t *image_matrix, uint32_t color, const char *str) {
    fb_data_t fb;
    fb.width = image_matrix->w;
    fb.height = image_matrix->h;
    fb.data = image_matrix->item;
    fb.bytes_per_pixel = 3;
    fb.format = FB_BGR888;
    fb_gfx_print(&fb, (fb.width - (strlen(str) * 14)) / 2, 10, color, str);
}

static int rgb_printf(dl_matrix3du_t *image_matrix, uint32_t color, const char *format, ...) {
    char loc_buf[64];
    char *temp = loc_buf;
    int len;
    va_list arg;
    va_list copy;
    va_start(arg, format);
    va_copy(copy, arg);
    len = vsnprintf(loc_buf, sizeof(loc_buf), format, arg);
    va_end(copy);
    if (len >= sizeof(loc_buf)) {
        temp = (char *)malloc(len + 1);
        if (temp == NULL) return 0;
    }
    vsnprintf(temp, len + 1, format, arg);
    va_end(arg);
    rgb_print(image_matrix, color, temp);
    if (len > 64) free(temp);
    return len;
}

static void draw_face_boxes(dl_matrix3du_t *image_matrix, box_array_t *boxes, int face_id) {
    int x, y, w, h, i;
    uint32_t color = FACE_COLOR_YELLOW;
    if (face_id < 0) color = FACE_COLOR_RED;
    else if (face_id > 0) color = FACE_COLOR_GREEN;
    
    fb_data_t fb;
    fb.width = image_matrix->w;
    fb.height = image_matrix->h;
    fb.data = image_matrix->item;
    fb.bytes_per_pixel = 3;
    fb.format = FB_BGR888;
    
    for (i = 0; i < boxes->len; i++) {
        x = (int)boxes->box[i].box_p[0];
        y = (int)boxes->box[i].box_p[1];
        w = (int)boxes->box[i].box_p[2] - x + 1;
        h = (int)boxes->box[i].box_p[3] - y + 1;
        fb_gfx_drawFastHLine(&fb, x, y, w, color);
        fb_gfx_drawFastHLine(&fb, x, y + h - 1, w, color);
        fb_gfx_drawFastVLine(&fb, x, y, h, color);
        fb_gfx_drawFastVLine(&fb, x + w - 1, y, h, color);
    }
}

static int run_face_recognition(dl_matrix3du_t *image_matrix, box_array_t *net_boxes, String name) {
    dl_matrix3du_t *aligned_face = NULL;
    dl_matrix3d_t *aligned_face_id = NULL;
    face_id_node *matched_id_node = NULL;
    char* c_str = new char[name.length()+1];
    strcpy(c_str, name.c_str());
    int matched_id = 0;
    // ESP_LOGI(TAG, "name is set to %s in recog_func", name.c_str());
    aligned_face = dl_matrix3du_alloc(1, FACE_WIDTH, FACE_HEIGHT, 3);
    if (!aligned_face) return matched_id;

    if (align_face(net_boxes, image_matrix, aligned_face) == ESP_OK) {
        aligned_face_id = get_face_id(aligned_face);
        if (is_enrolling == 1) {
            // int8_t left_sample_face = enroll_face(&id_list, aligned_face);
            int8_t left_sample_face = enroll_face_with_name(&id_list, aligned_face_id, c_str);
            if (left_sample_face == 0) {
                is_enrolling = 0;
                #if ENABLE_SD_TEST
                // Bridge::write_face_id_list_sdcard(&id_list, myFilePath);
                Bridge::write_face_id_name_list_sdcard(&id_list, myFilePath);
                #endif
            }
        } else {
            // matched_id = recognize_face(&id_list, aligned_face);
            matched_id_node = recognize_face_with_name(&id_list, aligned_face_id);
            if(matched_id_node) {
                // ESP_LOGI(TAG, "name is set to %s in handler", matched_id_node->id_name);
                matched_id=1;
                rgb_printf(image_matrix, FACE_COLOR_GREEN, "Hello Subject");
            
            } else {
                rgb_printf(image_matrix, FACE_COLOR_RED, "Intruder Alert!");
                matched_id = -1;
                
            }
        }
    }
    dl_matrix3du_free(aligned_face);
    dl_matrix3d_free(aligned_face_id);
    return matched_id;

}

static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    struct timeval _timestamp;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[128];

#if CONFIG_ESP_FACE_DETECT_ENABLED
    dl_matrix3du_t *image_matrix = NULL;
    // dl_matrix3d_t *image_matrix_id = NULL;
    bool detected = false;
    int face_id = 0;
#endif

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
        } else {
            // FIX: Use system time instead of fb->timestamp
            gettimeofday(&_timestamp, NULL);

#if CONFIG_ESP_FACE_DETECT_ENABLED
            if (!detection_enabled || fb->width > 400) {
#endif
                if (fb->format != PIXFORMAT_JPEG) {
                    bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                    esp_camera_fb_return(fb);
                    fb = NULL;
                    if (!jpeg_converted) res = ESP_FAIL;
                } else {
                    _jpg_buf_len = fb->len;
                    _jpg_buf = fb->buf;
                }
#if CONFIG_ESP_FACE_DETECT_ENABLED
            } else {
                image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
                // image_matrix_id = dl_matrix3d_alloc(1, fb->width, fb->height, 3);
                if (!image_matrix) res = ESP_FAIL;
                else {
                    if (!fmt2rgb888(fb->buf, fb->len, fb->format, image_matrix->item)) res = ESP_FAIL;
                    else {
                        box_array_t *net_boxes = face_detect(image_matrix, &mtmn_config);
                        if (net_boxes) {
                            detected = true;
                            if (recognition_enabled) face_id = run_face_recognition(image_matrix, net_boxes, newUserName);
                            // ESP_LOGI(TAG, "before draw boxes");
                            draw_face_boxes(image_matrix, net_boxes, face_id);
                            // ESP_LOGI(TAG, "after draw boxes");
                            dl_lib_free(net_boxes->score);
                            dl_lib_free(net_boxes->box);
                            if (net_boxes->landmark) dl_lib_free(net_boxes->landmark);
                            // ESP_LOGI(TAG, "after net boxes");
                            dl_lib_free(net_boxes);
                        }
                        if (!fmt2jpg(image_matrix->item, fb->width * fb->height * 3, fb->width, fb->height, PIXFORMAT_RGB888, 90, &_jpg_buf, &_jpg_buf_len)) {
                            ESP_LOGE(TAG, "fmt2jpg failed");
                        }
                        esp_camera_fb_return(fb);
                        fb = NULL;
                    }
                    dl_matrix3du_free(image_matrix);
                }
            }
#endif
        }

        if (res == ESP_OK) res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        if (res == ESP_OK) {
            size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, (int)_timestamp.tv_sec, (int)_timestamp.tv_usec);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);

        if (fb) esp_camera_fb_return(fb);
        else if (_jpg_buf) free(_jpg_buf);

        if (res != ESP_OK) break;
    }
    return res;
}



static esp_err_t login_post_handler(httpd_req_t *req) {
    char buf[100];
    // FIX: Added std::min and cast to size_t
    int ret = httpd_req_recv(req, buf, std::min((size_t)req->content_len, sizeof(buf)));
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    if (strstr(buf, "user=" ADMIN_USER) && strstr(buf, "pass=" ADMIN_PASS)) {
        authenticated = true;
        isAttendanceMode=false;
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
    } else {
        // httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid Credentials");
        // printf("Login Failed\n");
        authenticated = false;
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
    }
    return ESP_OK;
}


int8_t delete_face_runtime(face_id_name_list *l, const char *name) {
    if (l->count == 0) return -1;

    face_id_node *p = l->head;
    face_id_node *prev = NULL;

    while (p != NULL) {
        if (strcmp(p->id_name, name) == 0) {
            // Free the face ID vector
            if (p->id_vec != NULL) {
                dl_matrix3d_free(p->id_vec);
            }

            // Update list pointers
            if (prev == NULL) {
                // Deleting head node
                l->head = p->next;
                if (p == l->tail) {
                    l->tail = NULL;
                }
            } else {
                // Deleting middle or tail node
                prev->next = p->next;
                if (p == l->tail) {
                    l->tail = prev;
                }
            }

            // Free the node (use dl_lib_free instead of free for consistency)
            dl_lib_free(p);
            l->count--;
            
            // CRITICAL: If list is now empty, ensure head is NULL
            if (l->count == 0) {
                l->head = NULL;
                l->tail = NULL;
            }
            
            Serial.printf("Deleted '%s'. Call write_face_id_name_list_sdcard() to save.\n", name);
            return 0;
        }
        prev = p;
        p = p->next;
    }
    
    Serial.printf("Face '%s' not found.\n", name);
    return -1;
}

static esp_err_t index_handler(httpd_req_t *req) {
    if (!authenticated) {
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_send(req, login_html, strlen(login_html));
    }
    // If authenticated, show the AI control page from camera_index.h
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html, strlen(index_html));

    // isAdminActive = true; // Flag system that Admin is active
    // lastAdminActivity = millis();
    // httpd_resp_set_type(req, "text/html");
    // return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
}

// --- API: Get Users List ---
static esp_err_t api_users_handler(httpd_req_t *req) {
    // isAdminActive = true;
    // lastAdminActivity = millis();
    String json = Bridge::getUsersJSON();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), json.length());
}

// --- API: Delete User ---
static esp_err_t api_delete_handler(httpd_req_t *req) {
    // isAdminActive = true;
    char buf[100];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char name[64];
        if (httpd_query_key_value(buf, "name", name, sizeof(name)) == ESP_OK) {
            Bridge::deleteUserFromDB(String(name));
            int8_t res = delete_face_runtime(&id_list,name);
            if (res >= 0) {
    // Deletion successful - now save to persistent storage
    Bridge::write_face_id_name_list_sdcard( &id_list, myFilePath);
    Serial.println("Face deleted and changes saved to storage");
} else {
    Serial.println("Face not found - no changes to save");
}
        }
    }
    return httpd_resp_send(req, "OK", 2);
}

// --- API: Logout (Resume Attendance Mode) ---
static esp_err_t logout_handler(httpd_req_t *req) {
    // isAdminActive = false; // Important: Resumes loop logic
        isAttendanceMode=true;
    authenticated=false;
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

//second addition to the code

// --- HANDLER 1: Enable/Disable Detection View (Step 1 & 3) ---
// URI: /api/enroll_mode?active=1 (Open Modal) or active=0 (Close Modal)
static esp_err_t api_enroll_mode_handler(httpd_req_t *req) {
    // if (!isAdminActive) return httpd_resp_send_403(req);

    char buf[100];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char val[5];
        if (httpd_query_key_value(buf, "active", val, sizeof(val)) == ESP_OK) {
            int active = atoi(val);
            if (active == 1) {
                detection_enabled= 1;  // Enable Detection loop
                recognition_enabled = 1;
                Serial.println("Enrollment View: ON");
            } else {
                // isEnrollmentView = false; // Disable Detection loop
                detection_enabled= 0;  // Disable Detection loop
                recognition_enabled = 0;
                Serial.println("Enrollment View: OFF");
            }
        }
    }
    return httpd_resp_send(req, "OK", 2);
}

// --- HANDLER 2: Trigger Capture (Step 2) ---
// URI: /api/enroll_capture?id=123&name=John&dept=CS
static esp_err_t api_enroll_capture_handler(httpd_req_t *req) {
    // if (!isAdminActive) return httpd_resp_send_403(req);

    char buf[256]; // Increased buffer size for names
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char id[32], name[64], dept[64];
        
        // Parse params
        if (httpd_query_key_value(buf, "id", id, sizeof(id)) == ESP_OK &&
            httpd_query_key_value(buf, "name", name, sizeof(name)) == ESP_OK) {          
            httpd_query_key_value(buf, "dept", dept, sizeof(dept)); // Optional
            newUserName = String(name);
            Bridge::saveUserToDB(String(id), String(name), String(dept));
            is_enrolling = 1; 
            return httpd_resp_send(req, "Capturing...", 12);
        }
    }
    return httpd_resp_send_500(req);
}

// --- API: Get Attendance Logs ---
static esp_err_t api_logs_handler(httpd_req_t *req) {
    String json = Bridge::getAttendanceLogs();
    if(json)Serial.println(json);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), json.length());
}

// --- API: Download CSV ---
static esp_err_t api_download_csv_handler(httpd_req_t *req) {
    String csv = Bridge::downloadAttendanceCSV();
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=attendance.csv");
    return httpd_resp_send(req, csv.c_str(), csv.length());
}

// --- API: Clear Logs ---
static esp_err_t api_clear_logs_handler(httpd_req_t *req) {
    bool success = Bridge::clearAttendanceLogs();
    return httpd_resp_send(req, success ? "OK" : "FAIL", success ? 2 : 4);
}


void startCameraServer(){
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16; // Increased from 12 to 16 for new handlers
    config.stack_size = 8192; 

    // FIX: Added .user_ctx = NULL to clear missing initializer warnings
    httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL };
    // httpd_uri_t cmd_uri = { .uri = "/control", .method = HTTP_GET, .handler = cmd_handler, .user_ctx = NULL };
    httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };
    httpd_uri_t login_uri = { .uri = "/login", .method = HTTP_POST, .handler = login_post_handler, .user_ctx = NULL };
    httpd_uri_t logout_uri = { .uri = "/logout", .method = HTTP_GET, .handler = logout_handler, .user_ctx = NULL };
    httpd_uri_t api_users = { .uri = "/api/users", .method = HTTP_GET, .handler = api_users_handler, .user_ctx = NULL };
    httpd_uri_t api_delete = { .uri = "/api/delete_user", .method = HTTP_GET, .handler = api_delete_handler, .user_ctx = NULL };

    // Enrollment handlers
    httpd_uri_t enroll_mode_uri = { .uri = "/api/enroll_mode", .method = HTTP_GET,.handler = api_enroll_mode_handler,.user_ctx = NULL };
    httpd_uri_t enroll_capture_uri = { .uri = "/api/enroll_capture", .method = HTTP_GET, .handler = api_enroll_capture_handler, .user_ctx = NULL };
    
    // Attendance handlers
    httpd_uri_t api_logs = { .uri = "/api/logs", .method = HTTP_GET, .handler = api_logs_handler, .user_ctx = NULL };
    httpd_uri_t api_download = { .uri = "/api/download_csv", .method = HTTP_GET, .handler = api_download_csv_handler, .user_ctx = NULL };
    httpd_uri_t api_clear = { .uri = "/api/clear_logs", .method = HTTP_GET, .handler = api_clear_logs_handler, .user_ctx = NULL };
    // httpd_uri_t api_attendance_mode = { .uri = "/api/attendance_mode", .method = HTTP_GET, .handler = api_attendance_mode_handler, .user_ctx = NULL };

    // ra_filter_init(&ra_filter, 20);
    
    mtmn_config.type = FAST;
    mtmn_config.min_face = 80;
    mtmn_config.pyramid = 0.707;
    mtmn_config.pyramid_times = 4;
    mtmn_config.p_threshold.score = 0.6;
    mtmn_config.p_threshold.nms = 0.7;
    mtmn_config.p_threshold.candidate_number = 20;
    mtmn_config.r_threshold.score = 0.7;
    mtmn_config.r_threshold.nms = 0.7;
    mtmn_config.r_threshold.candidate_number = 10;
    mtmn_config.o_threshold.score = 0.7;
    mtmn_config.o_threshold.nms = 0.7;
    mtmn_config.o_threshold.candidate_number = 1;

    face_id_name_init(&id_list, FACE_ID_SAVE_NUMBER, ENROLL_CONFIRM_TIMES);
    #if ENABLE_SD_TEST
    Bridge::initSD();
    Bridge::listDir(SD_MMC, "/", 0);
    Bridge::read_face_id_name_list_sdcard(&id_list, myFilePath);
    #endif

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &login_uri);
        httpd_register_uri_handler(camera_httpd, &logout_uri);
        // httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &api_delete);
        httpd_register_uri_handler(camera_httpd, &api_users);
        httpd_register_uri_handler(camera_httpd, &enroll_mode_uri);
        httpd_register_uri_handler(camera_httpd, &enroll_capture_uri);
        httpd_register_uri_handler(camera_httpd, &api_logs);
        httpd_register_uri_handler(camera_httpd, &api_download);
        httpd_register_uri_handler(camera_httpd, &api_clear);
        // httpd_register_uri_handler(camera_httpd, &api_attendance_mode);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}