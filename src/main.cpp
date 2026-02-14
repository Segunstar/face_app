// main.cpp  –  FaceGuard Pro (ESP32-CAM)
// Entry point: camera init, WiFi, NTP, face recognition attendance loop.
//
// ── WDT / stability fix overview ─────────────────────────────────────────────
// Problem 1 – WDT on CPU 1 IDLE:
//   attendanceLoop() previously ran inside Arduino loop() on CPU 1, the same
//   core as httpd.  face_detect() blocks for 300–800 ms, starving httpd and
//   the CPU 1 IDLE task past the 5-second WDT window.
//   Fix: attendanceTask() is now pinned to CPU 0 via xTaskCreatePinnedToCore,
//   completely off the HTTP core.
//
// Problem 2 – "Face not recognised" spam / CPU thrash:
//   lastAttemptTime stamped on every attempt (500 ms gap).
//   lastRecognitionTime stamped only on successful match (5000 ms gap).
//
// Problem 3 – "JPG Decompression Failed":
//   fb_count raised to 2 when PSRAM present – prevents DMA starvation.
//   Heap guard prevents matrix allocation when memory is too low.
// ─────────────────────────────────────────────────────────────────────────────

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_task_wdt.h"
#include <Arduino.h>
#include "esp_camera.h"
#include "sd_card.h"
#include <WiFi.h>
#include "fd_forward.h"
#include "fr_forward.h"
#include "image_util.h"

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"
#include "global.h"

// ─── WiFi credentials ─────────────────────────────────────────────────────────
const char* ssid     = "itel RS4";
const char* password = "Asdf1234";

// ─── Global definitions (declared extern in global.h) ────────────────────────
bool                isAttendanceMode     = true;
unsigned long       lastRecognitionTime  = 0;  // set only on a successful match
unsigned long       lastAttemptTime      = 0;  // set on every detection attempt
const unsigned long RECOGNITION_COOLDOWN = 5000; // ms – prevents double-logging
const unsigned long ATTEMPT_COOLDOWN     = 500;  // ms – retry gap when no face found
bool                ntpSynced            = false;
AttendanceSettings  gSettings;

// ─── Minimum free heap before attempting matrix allocation ────────────────────
// face_detect() + aligned matrix need ~80 KB; guard at 100 KB to be safe.
#define MIN_FREE_HEAP_BYTES  (100 * 1024)

// ─── Forward declarations ────────────────────────────────────────────────────
void startCameraServer();
void initFaceRecognition();
static void attendanceTask(void *pvParameters);

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
        config.fb_count     = 2;   // 2 prevents DMA starvation / JPG errors
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
    s->set_framesize(s, FRAMESIZE_QVGA);  // required for face recognition
    Serial.println("[CAM] Initialised OK");
    return true;
}

// ─── setup() ─────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== FaceGuard Pro v2.0 ===");

    // 0) Feedback hardware – initialise before anything else so LEDs are
    //    deterministically OFF from the first moment of power-on.
    pinMode(GREEN_LED_GPIO, OUTPUT); digitalWrite(GREEN_LED_GPIO, LOW);
    pinMode(RED_LED_GPIO,   OUTPUT); digitalWrite(RED_LED_GPIO,   LOW);
    // BUZZER_GPIO_NUM == RED_LED_GPIO on this board, so the above covers it.
    Serial.println("[HW]  LEDs + buzzer GPIO initialised");

    // 1) SD card + settings
    Bridge::initSD();
    Bridge::loadSettings(gSettings);
    Bridge::listDir("/", 1);

    // 2) Camera
    if (!initCamera()) {
        Serial.println("[FATAL] Camera init failed – halting");
        while (true) delay(1000);
    }

    // 3) WiFi
    strncpy(gSettings.ssid, ssid, sizeof(gSettings.ssid) - 1);
    WiFi.begin(ssid, password);
    Serial.print("[WiFi] Connecting");
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 40) {
        delay(500); Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected – IP: %s\n",
                      WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] Connection failed – running without network");
    }

    // 4) NTP sync
    if (WiFi.status() == WL_CONNECTED) {
        Bridge::syncNTP();
    }

    // 5) Face recognition model init
    initFaceRecognition();

    // 6) HTTP server (runs its own tasks internally, stays on CPU 1)
    startCameraServer();

    // 7) Attendance task – pinned to CPU 0, away from httpd on CPU 1.
    //    Stack 8 KB is enough for the face pipeline; priority 1 keeps it below
    //    the idle priority so it never blocks the WDT feed.
    xTaskCreatePinnedToCore(
        attendanceTask,  // function
        "atd",           // name (shown in WDT logs)
        8192,            // stack bytes
        NULL,            // parameter
        1,               // priority
        NULL,            // handle (not needed)
        0                // CPU core 0
    );

    Serial.printf("\n[READY] Admin portal:  http://%s\n",
                  WiFi.localIP().toString().c_str());
    Serial.printf("[READY] Stream:        http://%s:81/stream\n",
                  WiFi.localIP().toString().c_str());
    Serial.println("[READY] Login: admin / 1234");
    Serial.println("[READY] Attendance task running on CPU 0.");
}

// ─── initFaceRecognition() ───────────────────────────────────────────────────
void initFaceRecognition() {
    extern mtmn_config_t    mtmn_config;
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
    Serial.printf("[FACE] Loaded %d enrolled face(s)\n", id_list.count);
}

// ─── Feedback helpers ─────────────────────────────────────────────────────────
// Both use vTaskDelay so they yield CPU while waiting —
// safe to call from inside attendanceTask (pinned to CPU 0).

// Called when a face is matched and attendance is logged.
// Pattern: green LED solid + two short buzzer beeps.
static void feedbackRecognised() {
    digitalWrite(GREEN_LED_GPIO, HIGH);  // green on for full duration

    // Beep 1
    digitalWrite(BUZZER_GPIO_NUM, HIGH);
    vTaskDelay(pdMS_TO_TICKS(120));
    digitalWrite(BUZZER_GPIO_NUM, LOW);

    vTaskDelay(pdMS_TO_TICKS(120));      // gap between beeps

    // Beep 2
    digitalWrite(BUZZER_GPIO_NUM, HIGH);
    vTaskDelay(pdMS_TO_TICKS(120));
    digitalWrite(BUZZER_GPIO_NUM, LOW);

    vTaskDelay(pdMS_TO_TICKS(200));      // hold green so it registers visually
    digitalWrite(GREEN_LED_GPIO, LOW);
}

// Called when a face is detected but not in the enrolled database.
// Pattern: red LED + long continuous buzz for 600 ms.
static void feedbackNotRecognised() {
    // RED_LED_GPIO == BUZZER_GPIO_NUM — one write covers both.
    digitalWrite(RED_LED_GPIO, HIGH);   // red LED on + buzzer on
    vTaskDelay(pdMS_TO_TICKS(600));
    digitalWrite(RED_LED_GPIO, LOW);    // red LED off + buzzer off
}

// ─── attendanceTask() – FreeRTOS task pinned to CPU 0 ────────────────────────
static void attendanceTask(void *pvParameters) {
    extern mtmn_config_t    mtmn_config;
    extern face_id_name_list id_list;

    Serial.println("[ATD] Task started on CPU 0");

    while (true) {
        // ── Gate checks ──────────────────────────────────────────────────────
        if (!isAttendanceMode || !gSettings.autoMode) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        unsigned long now = millis();

        // ── Two independent cooldowns ─────────────────────────────────────────
        // ATTEMPT_COOLDOWN (500 ms): throttles face_detect() frequency.
        //   Retries quickly on missed/unrecognised faces.
        //
        // RECOGNITION_COOLDOWN (5000 ms): only armed after a successful match.
        //   Prevents the same person being logged twice in one session.
        if (now - lastAttemptTime < ATTEMPT_COOLDOWN) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (now - lastRecognitionTime < RECOGNITION_COOLDOWN) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Stamp the attempt regardless of outcome (throttles CPU, stops spam)
        lastAttemptTime = now;

        // ── Heap guard ───────────────────────────────────────────────────────
        if (heap_caps_get_free_size(MALLOC_CAP_8BIT) < MIN_FREE_HEAP_BYTES) {
            Serial.printf("[ATD] Low heap (%lu B) – skipping frame\n",
                          (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT));
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // ── Grab frame ───────────────────────────────────────────────────────
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // ── Convert to RGB888 ────────────────────────────────────────────────
        dl_matrix3du_t *im = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
        if (!im) {
            esp_camera_fb_return(fb);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        bool converted = fmt2rgb888(fb->buf, fb->len, fb->format, im->item);
        esp_camera_fb_return(fb);  // release ASAP so stream can reuse it
        fb = nullptr;

        if (!converted) {
            dl_matrix3du_free(im);
            continue;
        }

        // ── Face detection ───────────────────────────────────────────────────
        box_array_t *boxes = face_detect(im, &mtmn_config);

        if (boxes) {
            dl_matrix3du_t *aligned = dl_matrix3du_alloc(1, FACE_WIDTH, FACE_HEIGHT, 3);

            if (aligned && align_face(boxes, im, aligned) == ESP_OK) {
                dl_matrix3d_t *fid   = get_face_id(aligned);
                face_id_node  *match = recognize_face_with_name(&id_list, fid);

                if (match) {
                    Serial.printf("[ATD] Recognised: %s\n", match->id_name);
                    AttendanceRecord rec = AttendanceRecord::fromFace(
                        match->id_name, match->id_name, "");
                    Bridge::logAttendance(rec);
                    lastRecognitionTime = now; // start the 5s no-relog window

                    if (gSettings.buzzerEnabled) {
                        feedbackRecognised();  // green flash + two short beeps
                    }
                } else {
                    Serial.println("[ATD] Face detected but not recognised");
                    if (gSettings.buzzerEnabled) {
                        feedbackNotRecognised();  // red flash + long buzz
                    }
                }
                dl_matrix3d_free(fid);
            }

            if (aligned) dl_matrix3du_free(aligned);
            dl_lib_free(boxes->score);
            dl_lib_free(boxes->box);
            if (boxes->landmark) dl_lib_free(boxes->landmark);
            dl_lib_free(boxes);
        }

        dl_matrix3du_free(im);

        // Yield to let the scheduler breathe between inference cycles.
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ─── loop() – nothing heavy lives here any more ───────────────────────────────
// Arduino's loop() runs on CPU 1 alongside httpd. Keeping it minimal means
// httpd gets clean scheduling and the CPU 1 IDLE task always feeds the WDT.
void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}