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
#define ENABLE_SD_TEST 1
#include "FS.h"
#include "SD_MMC.h"
#include "global.h"

const char* ssid = "itel RS4";
const char* password = "Asdf1234";

// Attendance mode variables
bool isAttendanceMode = true;  // Start in attendance mode
unsigned long lastRecognitionTime = 0;
const unsigned long RECOGNITION_COOLDOWN = 5000; // 5 seconds between recognition attempts

// Face recognition objects
// mtmn_config_t mtmn_config = {0};
// face_id_name_list id_list = {0};

#if ENABLE_SD_TEST
void initMicroSDCard() {
    if(!SD_MMC.begin()){
        Serial.println("Card Mount Failed");
        return;
    }
    uint8_t cardType = SD_MMC.cardType();

    if(cardType == CARD_NONE){
        Serial.println("No SD_MMC card attached");
        return;
    }

    Serial.print("SD_MMC Card Type: ");
    if(cardType == CARD_MMC){
        Serial.println("MMC");
    } else if(cardType == CARD_SD){
        Serial.println("SDSC");
    } else if(cardType == CARD_SDHC){
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("SD_MMC Card Size: %lluMB\n", cardSize);
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
    Serial.printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if (!root) {
        Serial.println("Failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels) {
                listDir(fs, file.path(), levels - 1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}
#endif

void startCameraServer();
void initFaceRecognition();
void attendanceLoop();

void setup() {
    Serial.begin(115200);
    
#if ENABLE_SD_TEST
    initMicroSDCard();
    Bridge::initSD();
    listDir(SD_MMC, "/", 0);
#endif

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    if(psramFound()){
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x", err);
        return;
    }

    sensor_t * s = esp_camera_sensor_get();
    if (s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1);
        s->set_brightness(s, 1);
        s->set_saturation(s, -2);
    }
    s->set_framesize(s, FRAMESIZE_QVGA); // Required for face recognition

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    // Initialize face recognition
    initFaceRecognition();

    startCameraServer();
    Serial.print("\nCamera Ready! Use: http://");
    Serial.println(WiFi.localIP());
    Serial.println("Attendance mode active. System ready to log attendance.");
}

void initFaceRecognition() {
    // Initialize MTMN
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

    // Initialize face list
    face_id_name_init(&id_list, 0, 5);
    
    // Load enrolled faces from SD card
    Bridge::read_face_id_name_list_sdcard(&id_list, "/FACE.BIN");
    
    Serial.printf("Loaded %d enrolled faces\n", id_list.count);
}

void attendanceLoop() {
    // Only run attendance if not in admin/web mode
    if (!isAttendanceMode) {
        return;
    }
    // Serial.println("in attendance mode");
    // Check if enough time has passed since last recognition
    unsigned long currentTime = millis();
    if (currentTime - lastRecognitionTime < RECOGNITION_COOLDOWN) {
        return;
    }

    // Capture frame
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return;
    }

    // Convert to RGB888
    dl_matrix3du_t *image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
    if (!image_matrix) {
        esp_camera_fb_return(fb);
        Serial.println("Matrix allocation failed");
        return;
    }

    if (!fmt2rgb888(fb->buf, fb->len, fb->format, image_matrix->item)) {
        dl_matrix3du_free(image_matrix);
        esp_camera_fb_return(fb);
        Serial.println("Format conversion failed");
        return;
    }

    esp_camera_fb_return(fb);

    // Detect faces
    box_array_t *net_boxes = face_detect(image_matrix, &mtmn_config);
    
    if (net_boxes) {
        // Face detected
        Serial.println("Face detected!");
        
        // Align face
        dl_matrix3du_t *aligned_face = aligned_face_alloc();
        if (align_face(net_boxes, image_matrix, aligned_face) == ESP_OK) {
            // Get face ID
            dl_matrix3d_t *face_id = get_face_id(aligned_face);
            
            // Recognize face
            face_id_node *recognized = recognize_face_with_name(&id_list, face_id);
            
            if (recognized != NULL) {
                Serial.printf("✓ Recognized: %s\n", recognized->id_name);
                
                // Log attendance
                Bridge::logAttendance(String(recognized->id_name), String(recognized->id_name));
                
                // Update cooldown timer
                lastRecognitionTime = currentTime;
                
                // Optional: Trigger buzzer/LED feedback here
                // digitalWrite(LED_GPIO_NUM, HIGH);
                // delay(500);
                // digitalWrite(LED_GPIO_NUM, LOW);
            } else {
                Serial.println("✗ Face not recognized");
            }
            
            dl_matrix3d_free(face_id);
        }
        
        dl_matrix3du_free(aligned_face);
        dl_lib_free(net_boxes->score);
        dl_lib_free(net_boxes->box);
        dl_lib_free(net_boxes->landmark);
        dl_lib_free(net_boxes);
    }
    
    dl_matrix3du_free(image_matrix);
}

void loop() { 
    attendanceLoop();
    delay(100); // Small delay to prevent overwhelming the system
}
