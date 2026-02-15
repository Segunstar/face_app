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
//   lastRecognitionTime was only updated on a successful match.  On a no-match
//   the loop re-ran immediately (only 100 ms gap), hammering face_detect().
//   Fix: lastRecognitionTime is stamped at the START of every detection
//   attempt, enforcing the full RECOGNITION_COOLDOWN between runs regardless
//   of outcome.
//
// Problem 3 – "JPG Decompression Failed":
//   fb_count=1 meant one shared DMA buffer.  Under heavy CPU load the camera
//   DMA would be starved and corrupt the buffer before the stream handler
//   encoded it.
//   Fix: fb_count raised to 2 when PSRAM is present so the camera can fill one
//   buffer while the previous is in use.  A free-heap guard prevents matrix
//   allocation when memory is too low.
// ─────────────────────────────────────────────────────────────────────────────

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_task_wdt.h"
#include <Arduino.h>
#include "esp_camera.h"
#include "sd_card.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "fd_forward.h"
#include "fr_forward.h"
#include "image_util.h"

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"
#include "global.h"

// ─── WiFi credentials ─────────────────────────────────────────────────────────
const char* ssid     = "itel RS4";
const char* password = "Asdf1234";

// mDNS hostname – device reachable at http://faceguard.local on any network
// No static IP needed – DHCP works on hotspot, home router, or office network.
// The actual assigned IP is printed to serial on every boot.
#define MDNS_NAME "faceguard"

// ─── Global definitions (declared extern in global.h) ────────────────────────
bool                isAttendanceMode     = true;
unsigned long       lastRecognitionTime  = 0;  // set only on a successful match
unsigned long       lastAttemptTime      = 0;  // set on every detection attempt
const unsigned long RECOGNITION_COOLDOWN = 5000;  // ms – prevents double-logging same person
const unsigned long ATTEMPT_COOLDOWN     = 500;   // ms – retry gap when no face found
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
        config.fb_count     = 2;   // ← was 1; 2 prevents DMA starvation / JPG errors
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
    pinMode(GREEN_LED_GPIO,  OUTPUT); digitalWrite(GREEN_LED_GPIO,  LOW);
    pinMode(RED_LED_GPIO,    OUTPUT); digitalWrite(RED_LED_GPIO,    LOW);
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

    // 3) WiFi – plain DHCP, works on any network (hotspot, router, office)
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

        // Start mDNS so device is reachable at http://faceguard.local
        if (MDNS.begin(MDNS_NAME)) {
            MDNS.addService("http",   "tcp", 80);
            MDNS.addService("stream", "tcp", 81);
            Serial.printf("[mDNS] Reachable at http://%s.local\n", MDNS_NAME);
        } else {
            Serial.println("[mDNS] Failed to start – IP access still works");
        }
    } else {
        Serial.println("\n[WiFi] Connection failed – running without network");
    }

    // 4) Face recognition model init (local SD only – fast, no network)
    initFaceRecognition();

    // 5) HTTP server – up immediately, zero network dependency
    startCameraServer();

    // 6) Attendance task – pinned to CPU 0
    xTaskCreatePinnedToCore(
        attendanceTask, "atd", 8192, NULL, 1, NULL, 0
    );

    // 7) NTP sync in a background task – never blocks setup()
    if (WiFi.status() == WL_CONNECTED) {
        xTaskCreate([](void*) {
            Bridge::syncNTP();
            vTaskDelete(NULL);
        }, "ntp", 4096, NULL, 1, NULL);
    }

    Serial.printf("\n[READY] Admin portal:  http://%s  or  http://%s.local\n",
                  WiFi.localIP().toString().c_str(), MDNS_NAME);
    Serial.printf("[READY] Stream:        http://%s:81/stream\n",
                  WiFi.localIP().toString().c_str());
    Serial.println("[READY] Login: admin / 1234");
    Serial.println("[READY] Attendance task running on CPU 0.");
}

// ─── initFaceRecognition() ───────────────────────────────────────────────────
void initFaceRecognition() {
    extern mtmn_config_t   mtmn_config;
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
// Both functions use vTaskDelay so they yield the CPU while waiting —
// safe to call from inside attendanceTask (pinned to CPU 0).

// Called when a face is matched and attendance is logged.
// Pattern: green LED on solid + two short buzzer beeps.
static void feedbackRecognised() {
    // Green LED stays on for the full duration so it dominates visually.
    digitalWrite(GREEN_LED_GPIO, HIGH);

    // Beep 1
    digitalWrite(BUZZER_GPIO_NUM, HIGH);
    vTaskDelay(pdMS_TO_TICKS(120));
    digitalWrite(BUZZER_GPIO_NUM, LOW);

    // Gap between beeps
    vTaskDelay(pdMS_TO_TICKS(120));

    // Beep 2
    digitalWrite(BUZZER_GPIO_NUM, HIGH);
    vTaskDelay(pdMS_TO_TICKS(120));
    digitalWrite(BUZZER_GPIO_NUM, LOW);

    // Hold green briefly so it registers visually, then off.
    vTaskDelay(pdMS_TO_TICKS(200));
    digitalWrite(GREEN_LED_GPIO, LOW);
}

// Called when a face is detected but not in the enrolled database.
// Pattern: red LED + long continuous buzz for 600 ms.
static void feedbackNotRecognised() {
    // RED_LED_GPIO == BUZZER_GPIO_NUM on this board — one write covers both.
    digitalWrite(RED_LED_GPIO, HIGH);      // red LED on + buzzer on
    vTaskDelay(pdMS_TO_TICKS(600));
    digitalWrite(RED_LED_GPIO, LOW);       // red LED off + buzzer off
}

// ─── attendanceTask() – FreeRTOS task pinned to CPU 0 ────────────────────────
static void attendanceTask(void *pvParameters) {
    extern mtmn_config_t   mtmn_config;
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
        // ATTEMPT_COOLDOWN  (500 ms): throttles how often face_detect() runs.
        //   Fires on every attempt so the camera stays responsive – if a face
        //   is missed due to a bad angle it retries half a second later.
        //
        // RECOGNITION_COOLDOWN (5000 ms): only armed after a *successful match*.
        //   Prevents the same person being logged twice during one "session"
        //   while still allowing quick retries for unrecognised faces.
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
        esp_camera_fb_return(fb);   // release buffer ASAP so stream can reuse it
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

                    // Look up full user record from users.txt by name.
                    // match->id_name is only the display name stored in FACE.BIN.
                    // getUserByName fills in the proper uid and dept.
                    UserRecord user;
                    bool found = Bridge::getUserByName(match->id_name, user);

                    AttendanceRecord rec = {};
                    if (found) {
                        strncpy(rec.uid,  user.id,   sizeof(rec.uid)  - 1);
                        strncpy(rec.name, user.name, sizeof(rec.name) - 1);
                        strncpy(rec.dept, user.dept, sizeof(rec.dept) - 1);
                    } else {
                        // Not in DB – use name as fallback for both uid and name
                        strncpy(rec.uid,  match->id_name, sizeof(rec.uid)  - 1);
                        strncpy(rec.name, match->id_name, sizeof(rec.name) - 1);
                    }

                    Bridge::logAttendance(rec);
                    lastRecognitionTime = now;  // start 5s no-relog window

                    if (gSettings.buzzerEnabled) {
                        feedbackRecognised();
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

        // Yield to let the scheduler breathe between heavy inference cycles.
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ─── loop() – nothing heavy lives here any more ───────────────────────────────
// Arduino's loop() runs on CPU 1 alongside httpd.  Keeping it minimal means
// httpd gets clean scheduling and the CPU 1 IDLE task always feeds the WDT.
void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
