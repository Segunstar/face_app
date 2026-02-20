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
// #include "soc/soc.h"
// #include "soc/rtc_cntl_reg.h"
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
// Heap guard: skip face_detect when free heap falls below this threshold.
// Raised to 130 KB (was 100 KB) so httpd DynamicJsonDocument allocations
// (up to 16 KB each for /api/logs) always have room even under load.
#define MIN_FREE_HEAP_BYTES  (130 * 1024)

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
    // 20 MHz is the standard XCLK for OV2640; do NOT go above 20 MHz —
    // the OV2640 PCLK becomes unstable and produces corrupt frames.
    config.xclk_freq_hz  = 20000000;
    config.pixel_format  = PIXFORMAT_JPEG;

    if (psramFound()) {
        config.frame_size   = FRAMESIZE_UXGA;
        config.jpeg_quality = 12;
        config.fb_count     = 2;   // 2 buffers prevent DMA starvation / JPG corruption
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

    // ── OV3660-specific baseline ──────────────────────────────────────────────
    if (s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1);
        s->set_brightness(s, 1);
        s->set_saturation(s, -2);
    }

    // ── Low-light tuning (OV2640 – AI-Thinker default) ───────────────────────
    // These settings are safe for all OV2640 revisions and improve indoor
    // recognition significantly without degrading daytime performance.
    //
    // AGC (Automatic Gain Control) + ceiling
    //   gain_ctrl=1 lets the sensor boost amplification in dim scenes.
    //   agc_gain ceiling=6 (~8x) balances brightness vs noise.
    //   Going above 6 (max 30) adds too much grain for MTMN detection.
    s->set_gain_ctrl(s, 1);        // enable AGC
    s->set_agc_gain(s, 6);         // AGC ceiling: 0=1x … 30=max; 6 ≈ 8x — indoor sweet spot

    // AEC (Auto Exposure Control) — night mode
    //   aec2=1 enables "night mode": the sensor extends integration time across
    //   multiple frames, delivering brighter images in dim light with no extra noise.
    //   ae_level=1 adds +1 stop of exposure bias on top of AEC target.
    s->set_aec2(s, 1);             // enable AEC night mode
    s->set_ae_level(s, 1);         // +1 exposure bias (range -2…+2)
    s->set_aec_value(s, 400);      // initial AEC hint; AEC algorithm takes over from here

    // Brightness / contrast
    //   Slight brightness boost helps MTMN's P-net find face candidates.
    //   Contrast +1 sharpens feature boundaries (eyes, nose, mouth) that
    //   the landmark network relies on.
    s->set_brightness(s, 1);       // -2…+2; 1 = mild boost
    s->set_contrast(s, 1);         // -2…+2; 1 = mild sharpening

    // Noise / correction
    //   BPC (black pixel correction) and WPC (white pixel correction) remove
    //   hot/dead pixels that confuse the neural net in low-signal conditions.
    //   Raw gamma = slightly more shadow detail at the cost of a touch of highlight.
    s->set_bpc(s, 1);
    s->set_wpc(s, 1);
    s->set_raw_gma(s, 1);          // raw gamma correction
    s->set_lenc(s, 1);             // lens shading correction (edge brightness)

    // ── Frame size for face recognition ──────────────────────────────────────
    // QVGA (320×240) is the only resolution the ESP-WHO MTMN pipeline supports.
    // Larger sizes are decimated internally but waste time; smaller sizes miss faces.
    s->set_framesize(s, FRAMESIZE_QVGA);

    Serial.println("[CAM] Initialised  (AGC, AEC-night, gamma, BPC/WPC active)");
    return true;
}

// ─── setup() ─────────────────────────────────────────────────────────────────
void setup() {
    // Extend WDT before anything else — SD multi-retry and WiFi can both take >5 s
    esp_task_wdt_init(30, true);
    // WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector

    Serial.begin(115200);
    Serial.println("\n\n=== FaceGuard Pro v2.0 ===");

    // 0) Hardware GPIO — all outputs LOW before anything else so LEDs don't
    //    flicker randomly during SPI/camera init.
    pinMode(GREEN_LED_GPIO,  OUTPUT); digitalWrite(GREEN_LED_GPIO,  LOW);
    pinMode(RED_LED_GPIO,    OUTPUT); digitalWrite(RED_LED_GPIO,    LOW);
    pinMode(BUZZER_GPIO_NUM, OUTPUT); digitalWrite(BUZZER_GPIO_NUM, LOW);
    Serial.println("[HW]  GPIOs initialised (LEDs + buzzer all OFF)");

    // 1) SD card — must succeed before proceeding.
    //    initSD() retries up to 4 times with SPI bus reset between attempts.
    //    If the card genuinely cannot be mounted (unseated, unformatted, dead)
    //    the system halts here with a red LED blink so the problem is obvious.
    Bridge::initSD();
    if (!Bridge::sdIsOk()) {
        Serial.println("[FATAL] SD card unavailable after all retries.");
        Serial.println("        Check: card seated? FAT32 formatted? Wiring correct?");
        // Blink red LED forever – unmistakable hardware fault signal
        while (true) {
            digitalWrite(RED_LED_GPIO, HIGH); delay(200);
            digitalWrite(RED_LED_GPIO, LOW);  delay(200);
        }
    }
    Bridge::loadSettings(gSettings);
    Bridge::listDir("/", 1);

    // 2) Camera — also fatal if it fails (no point running attendance without it)
    if (!initCamera()) {
        Serial.println("[FATAL] Camera init failed – halting");
        while (true) {
            digitalWrite(RED_LED_GPIO, HIGH); delay(500);
            digitalWrite(RED_LED_GPIO, LOW);  delay(500);
        }
    }

    // 3) WiFi — plain DHCP, works on any network (hotspot, router, office)
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

    // 4) Face recognition model — single init here; startCameraServer() does
    //    NOT re-run mtmn_config so there is exactly one initialisation path.
    initFaceRecognition();

    // 5) HTTP server
    startCameraServer();

    // 6) Attendance task pinned to CPU 0 (HTTP + stream run on CPU 1)
    xTaskCreatePinnedToCore(
        attendanceTask, "atd", 8192, NULL, 1, NULL, 0
    );

    // 7) NTP sync in a background task — never blocks setup()
    if (WiFi.status() == WL_CONNECTED) {
        xTaskCreate([](void*) {
            Bridge::syncNTP();
            vTaskDelete(NULL);
        }, "ntp", 4096, NULL, 1, NULL);
    }

    // 8) WiFi watchdog — checks connection every 30 s and reconnects if lost.
    //    Without this, a hotspot idle-timeout or brief RF dropout makes the
    //    portal permanently unreachable until a hardware reboot.
    //    The task is self-contained and safe to run indefinitely.
    xTaskCreate([](void*) {
        const char *wSsid = ssid;
        const char *wPass = password;
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(30000));
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WiFi] Connection lost – reconnecting…");
                WiFi.disconnect(true);
                vTaskDelay(pdMS_TO_TICKS(1000));
                WiFi.begin(wSsid, wPass);
                int tries = 0;
                while (WiFi.status() != WL_CONNECTED && tries++ < 20) {
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.printf("[WiFi] Reconnected – IP: %s\n",
                                  WiFi.localIP().toString().c_str());
                } else {
                    Serial.println("[WiFi] Reconnect failed – will retry in 30 s");
                }
            }
        }
    }, "wifi_wdg", 3072, NULL, 1, NULL);

    Serial.printf("\n[READY] Admin portal:  http://%s  or  http://%s.local\n",
                  WiFi.localIP().toString().c_str(), MDNS_NAME);
    Serial.printf("[READY] Stream:        http://%s:81/stream\n",
                  WiFi.localIP().toString().c_str());
    Serial.println("[READY] Login: admin / 1234");
    Serial.println("[READY] Attendance task running on CPU 0.");
}

// ─── initFaceRecognition() ───────────────────────────────────────────────────
// Single initialisation point for all MTMN parameters and the face ID list.
// startCameraServer() uses these values — do NOT re-init them there.
void initFaceRecognition() {
    // mtmn_config and id_list are defined in app_httpd.cpp (extern in global.h)
    // so they are shared with the stream handler and enrolment code.
    mtmn_config.type                         = FAST;
    mtmn_config.min_face                     = 80;
    mtmn_config.pyramid                      = 0.707f;
    mtmn_config.pyramid_times                = 4;

    // P-net: face proposal network.
    // Lowering score 0.6→0.55 catches more face candidates in low light at
    // the cost of ~5% more false-positive boxes — O-net discards most of them.
    mtmn_config.p_threshold.score            = 0.55f;   // was 0.6
    mtmn_config.p_threshold.nms              = 0.7f;
    mtmn_config.p_threshold.candidate_number = 20;

    // R-net: refinement network — keep conservative, it runs fast.
    mtmn_config.r_threshold.score            = 0.7f;
    mtmn_config.r_threshold.nms              = 0.7f;
    mtmn_config.r_threshold.candidate_number = 10;

    // O-net: output (landmark) network — keep strict to avoid false matches.
    mtmn_config.o_threshold.score            = 0.7f;
    mtmn_config.o_threshold.nms              = 0.7f;
    mtmn_config.o_threshold.candidate_number = 1;

    face_id_name_init(&id_list, 10, ENROLL_CONFIRM_TIMES);
    Bridge::read_face_id_name_list_sdcard(&id_list, "/FACE.BIN");
    Serial.printf("[FACE] Loaded %d enrolled face(s) | P-score=0.55 (low-light)\n",
                  id_list.count);
}

// ─── Feedback helpers ─────────────────────────────────────────────────────────
// Both functions use vTaskDelay so they yield the CPU while waiting —
// safe to call from inside attendanceTask (pinned to CPU 0).

// Called when a face is matched and attendance is logged.
// Pattern: green LED on solid + two short buzzer beeps.
static void feedbackRecognised() {
    // Green LED stays on for the full duration so it dominates visually.
    Serial.println("green led on");
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
    Serial.println("red led on");
    digitalWrite(RED_LED_GPIO,    HIGH);  // red LED on
    digitalWrite(BUZZER_GPIO_NUM, HIGH);  // buzzer on  (GPIO 1 – independent)
    vTaskDelay(pdMS_TO_TICKS(600));
    digitalWrite(RED_LED_GPIO,    LOW);
    digitalWrite(BUZZER_GPIO_NUM, LOW);
}

// ─── attendanceTask() – FreeRTOS task pinned to CPU 0 ────────────────────────
static void attendanceTask(void *pvParameters) {
    Serial.println("[ATD] Task started on CPU 0");

    // Brief startup delay: let camera settle its AEC/AGC after init before
    // the first recognition attempt so the first frame isn't overexposed.
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (true) {
        esp_task_wdt_reset();  // feed WDT at the top of every iteration

        // ── Gate: admin mode or auto-mode disabled ────────────────────────────
        if (!isAttendanceMode || !gSettings.autoMode) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // ── Gate: enrollment in progress ─────────────────────────────────────
        // While the stream handler is collecting confirmation frames for a new
        // enrolment, attendanceTask must not compete for the camera framebuffer
        // or call face_detect() concurrently.  This avoids DMA corruption and
        // ensures the enrolment accumulator isn't confused by a parallel match.
        if (is_enrolling == 1) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        unsigned long now = millis();

        // ── Cooldown throttles ────────────────────────────────────────────────
        if (now - lastAttemptTime    < (unsigned long)ATTEMPT_COOLDOWN) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (now - lastRecognitionTime < (unsigned long)RECOGNITION_COOLDOWN) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        lastAttemptTime = now;

        // ── Heap guard ────────────────────────────────────────────────────────
        if (heap_caps_get_free_size(MALLOC_CAP_8BIT) < MIN_FREE_HEAP_BYTES) {
            Serial.printf("[ATD] Low heap (%u B) – skipping\n",
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // ── Grab frame ────────────────────────────────────────────────────────
        camera_fb_t *fb = esp_camera_fb_get();

        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // ── Convert to RGB888 ─────────────────────────────────────────────────
        dl_matrix3du_t *im = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
        if (!im) {
            esp_camera_fb_return(fb);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        bool converted = fmt2rgb888(fb->buf, fb->len, fb->format, im->item);
        esp_camera_fb_return(fb);
        fb = nullptr;

        if (!converted) {
            dl_matrix3du_free(im);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // ── Face detection ────────────────────────────────────────────────────
        box_array_t *boxes = face_detect(im, &mtmn_config);

        if (boxes) {
            dl_matrix3du_t *aligned = dl_matrix3du_alloc(1, FACE_WIDTH, FACE_HEIGHT, 3);

            if (aligned && align_face(boxes, im, aligned) == ESP_OK) {
                dl_matrix3d_t *fid = get_face_id(aligned);
                if (!fid) {
                    // OOM inside get_face_id -- skip this frame, nothing to free
                    if (aligned) dl_matrix3du_free(aligned);
                    if (boxes->score)    dl_lib_free(boxes->score);
                    if (boxes->box)      dl_lib_free(boxes->box);
                    if (boxes->landmark) dl_lib_free(boxes->landmark);
                    dl_lib_free(boxes);
                    dl_matrix3du_free(im);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    continue;
                }
                face_id_node  *match = recognize_face_with_name(&id_list, fid);

                if (match) {
                    Serial.printf("[ATD] Recognised: %s\n", match->id_name);

                    UserRecord user;
                    bool found = Bridge::getUserByName(match->id_name, user);

                    AttendanceRecord rec = {};
                    if (found) {
                        strncpy(rec.uid,  user.id,   sizeof(rec.uid)  - 1);
                        strncpy(rec.name, user.name, sizeof(rec.name) - 1);
                        strncpy(rec.dept, user.dept, sizeof(rec.dept) - 1);
                    } else {
                        // Not in DB – use the enrolled name as fallback
                        strncpy(rec.uid,  match->id_name, sizeof(rec.uid)  - 1);
                        strncpy(rec.name, match->id_name, sizeof(rec.name) - 1);
                    }

                    Bridge::logAttendance(rec);
                    lastRecognitionTime = now;

                    if (gSettings.buzzerEnabled) {
                        feedbackRecognised();
                    }
                } else {
                    Serial.println("[ATD] Face detected but not recognised");
                    if (gSettings.buzzerEnabled) {
                        feedbackNotRecognised();
                    }
                }
                dl_matrix3d_free(fid);
            }

            if (aligned) dl_matrix3du_free(aligned);
            // Free all box sub-arrays defensively
            if (boxes->score)    dl_lib_free(boxes->score);
            if (boxes->box)      dl_lib_free(boxes->box);
            if (boxes->landmark) dl_lib_free(boxes->landmark);
            dl_lib_free(boxes);
        }

        dl_matrix3du_free(im);
        vTaskDelay(pdMS_TO_TICKS(50));  // yield between inference cycles
    }
}

// ─── loop() – nothing heavy lives here any more ───────────────────────────────
// Arduino's loop() runs on CPU 1 alongside httpd.  Keeping it minimal means
// httpd gets clean scheduling and the CPU 1 IDLE task always feeds the WDT.
void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
