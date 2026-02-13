#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>

// Global State Flags
extern bool isAttendanceMode;          // True = Attendance Mode, False = Web Admin Mode
extern unsigned long lastRecognitionTime; // Cooldown for recognition
extern const int ADMIN_TIMEOUT_MS;

extern mtmn_config_t mtmn_config;
extern face_id_name_list id_list;

// Face recognition control flags (used by web server)
extern int8_t detection_enabled;
extern int8_t recognition_enabled;
extern int8_t is_enrolling;

// Shared Hardware Pins
#define LED_GPIO_NUM 4
#define FLASH_GPIO_NUM 4

#endif
