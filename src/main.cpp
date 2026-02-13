// main.cpp  –  FaceGuard Pro (ESP32-CAM)
// Entry point: camera init, WiFi, NTP, face recognition attendance loop.

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Arduino.h>
#include "esp_camera.h"
#include "sd_card.h"
#include <WiFi.h>
#include "fd_forward.h"
#include "fr_forward.h"
#include "image_util.h"

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"
#include "FS.h"
#include "SD_MMC.h"
#include "global.h"

// ─── WiFi credentials ─────────────────────────────────────────────────────────
// UPDATE THESE to match your network before flashing!
const char *ssid     = "YourSSID";
const char *password = "YourPassword";

// ─── Global definitions (declared extern in global.h) ────────────────────────
bool                 isAttendanceMode    = true;
unsigned long        lastRecognitionTime = 0;
const unsigned long  RECOGNITION_COOLDOWN = 5000; // ms between recognitions
bool                 ntpSynced          = false;
AttendanceSettings   gSettings;

// ─── Forward declarations ────────────────────────────────────────────────────
void startCameraServer();
void initFaceRecognition();
void attendanceLoop();

// ─── Camera initialisation ────────────────────────────────────────────────────
static bool initCamera() {
    camera_config_t config;
    config.ledc_channel  = LEDC_CHANNEL_0;
    config.ledc_timer    = LEDC_TIMER_0;
    config.pin_d0        = Y2_GPIO_NUM;
    config.pin_d1        = Y3_GPIO_NUM;
    config.pin_d2        = Y4_GPIO_NUM;
    config.pin_d3        = Y5_GPIO_NUM;
    config.pin_d4        = Y6_GPIO_NUM;
    config.pin_d5        = Y7_GPIO_NUM;
    config.pin_d6        = Y8_GPIO_NUM;
    config.pin_d7        = Y9_GPIO_NUM;
    config.pin_xclk      = XCLK_GPIO_NUM;
    config.pin_pclk      = PCLK_GPIO_NUM;
    config.pin_vsync     = VSYNC_GPIO_NUM;
    config.pin_href      = HREF_GPIO_NUM;
    config.pin_sscb_sda  = SIOD_GPIO_NUM;
    config.pin_sscb_scl  = SIOC_GPIO_NUM;
    config.pin_pwdn      = PWDN_GPIO_NUM;
    config.pin_reset     = RESET_GPIO_NUM;
    config.xclk_freq_hz  = 20000000;
    config.pixel_format  = PIXFORMAT_JPEG;

    if (psramFound()) {
        config.frame_size   = FRAMESIZE_UXGA;
        config.jpeg_quality = 12;
        config.fb_count     = 1;
    } else {
        config.frame_size   = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count     = 1;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[CAM] Init failed: 0x%x\n", err);
        return false;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1);
        s->set_brightness(s, 1);
        s->set_saturation(s, -2);
    }
    s->set_framesize(s, FRAMESIZE_QVGA); // Required for face recognition
    Serial.println("[CAM] Initialised OK");
    return true;
}

// ─── setup() ─────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== FaceGuard Pro v2.0 ===");

    // 1) SD card + settings
    Bridge::initSD();
    Bridge::loadSettings(gSettings);   // fills gSettings (defaults if no file)
    Bridge::listDir(SD_MMC, "/", 1);

    // 2) Camera
    if (!initCamera()) {
        Serial.println("[FATAL] Camera init failed – halting");
        while (true) delay(1000);
    }

    // 3) WiFi
    strncpy(gSettings.ssid, ssid, sizeof(gSettings.ssid)-1);
    WiFi.begin(ssid, password);
    Serial.print("[WiFi] Connecting");
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 40) {
        delay(500); Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected – IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] Connection failed – running without network");
    }

    // 4) NTP time sync (requires WiFi)
    if (WiFi.status() == WL_CONNECTED) {
        Bridge::syncNTP();
    }

    // 5) Face recognition init
    initFaceRecognition();

    // 6) HTTP server
    startCameraServer();

    Serial.printf("\n[READY] Admin portal: http://%s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[READY] Camera stream: http://%s:81/stream\n", WiFi.localIP().toString().c_str());
    Serial.printf("[READY] Login: %s / %s\n", "admin", "1234");
    Serial.println("[READY] Attendance mode active.");
}

// ─── initFaceRecognition() – called from setup & can be recalled later ────────
void initFaceRecognition() {
    // extern objects defined in app_httpd.cpp
    extern mtmn_config_t mtmn_config;
    extern face_id_name_list id_list;

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

    face_id_name_init(&id_list, 7, 5);
    Bridge::read_face_id_name_list_sdcard(&id_list, "/FACE.BIN");
    Serial.printf("[FACE] Loaded %d enrolled faces\n", id_list.count);
}

// ─── attendanceLoop() – runs in loop(), skipped when admin is active ──────────
void attendanceLoop() {
    if (!isAttendanceMode) return;
    if (!gSettings.autoMode) return;

    unsigned long now = millis();
    if (now - lastRecognitionTime < RECOGNITION_COOLDOWN) return;

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return;

    dl_matrix3du_t *im = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
    if (!im) { esp_camera_fb_return(fb); return; }

    if (!fmt2rgb888(fb->buf, fb->len, fb->format, im->item)) {
        dl_matrix3du_free(im); esp_camera_fb_return(fb); return;
    }
    esp_camera_fb_return(fb);

    extern mtmn_config_t mtmn_config;
    box_array_t *boxes = face_detect(im, &mtmn_config);

    if (boxes) {
        extern face_id_name_list id_list;
        dl_matrix3du_t *aligned = dl_matrix3du_alloc(1, FACE_WIDTH, FACE_HEIGHT, 3);
        if (aligned && align_face(boxes, im, aligned) == ESP_OK) {
            dl_matrix3d_t *fid = get_face_id(aligned);
            face_id_node  *match = recognize_face_with_name(&id_list, fid);
            if (match) {
                Serial.printf("[ATD] Recognised: %s\n", match->id_name);
                Bridge::logAttendance(String(match->id_name),
                                      String(match->id_name), "");
                lastRecognitionTime = now;

                // Optional buzzer feedback
                if (gSettings.buzzerEnabled) {
                    digitalWrite(BUZZER_GPIO_NUM, HIGH); delay(200);
                    digitalWrite(BUZZER_GPIO_NUM, LOW);
                }
            } else {
                Serial.println("[ATD] Face not recognised");
            }
            dl_matrix3d_free(fid);
        }
        if (aligned) dl_matrix3du_free(aligned);
        dl_lib_free(boxes->score); dl_lib_free(boxes->box);
        if (boxes->landmark) dl_lib_free(boxes->landmark);
        dl_lib_free(boxes);
    }
    dl_matrix3du_free(im);
}

// ─── loop() ───────────────────────────────────────────────────────────────────
void loop() {
    attendanceLoop();
    delay(100);
}
