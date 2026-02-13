# FaceGuard Pro — ESP32-CAM Facial Attendance System v2.0

## Project Structure

```
project/
├── src/
│   ├── main.cpp           ← Entry point, WiFi, NTP, attendance loop
│   ├── app_httpd.cpp      ← HTTP server, all API endpoints
│   └── sd_card.cpp        ← SD card, time, attendance, settings
├── include/
│   ├── main_page.h        ← FaceGuard Pro admin portal HTML (PROGMEM)
│   ├── camera_index.h     ← Login page HTML (PROGMEM)
│   ├── camera_pins.h      ← Camera GPIO definitions (unchanged)
│   ├── sd_card.h          ← Bridge namespace declarations
│   └── global.h           ← Shared globals + AttendanceSettings struct
├── partitions/
│   └── huge_app.csv       ← Custom partition table (required!)
├── platformio.ini
└── sdkconfig.defaults
```

---

## ⚙️ Quick Setup

### 1. Set WiFi Credentials
In `src/main.cpp` (lines 17-18):
```cpp
const char *ssid     = "YourNetworkName";
const char *password = "YourPassword";
```

### 2. Change Admin Password
In `src/app_httpd.cpp` (lines 28-29):
```cpp
#define ADMIN_USER "admin"
#define ADMIN_PASS "1234"          // ← CHANGE THIS!
```

### 3. SD Card Setup
Format a MicroSD card as **FAT32**. The system auto-creates:
```
/db/users.txt         ← User database (JSON array)
/db/ FACE.BIN         ← Face encodings (binary)
/atd/log_YYYY-MM-DD.csv  ← Daily attendance logs
/cfg/settings.json    ← Saved settings
```

### 4. Partition Table
Create `partitions/huge_app.csv` with:
```
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xe000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x300000,
spiffs,   data, spiffs,  0x310000, 0xF0000,
```

### 5. Build & Flash
```bash
pio run --target upload
pio device monitor
```

---

## 🌐 Admin Portal Usage

1. Find the ESP32's IP address from the serial monitor:
   ```
   [READY] Admin portal:   http://192.168.x.x
   [READY] Camera stream:  http://192.168.x.x:81/stream
   ```
2. Open the IP in your browser → Login page
3. Login with `admin` / `1234`
4. Portal sections:
   - **Dashboard** — live stats, weekly chart, real-time check-in log
   - **Users** — enroll, edit, delete, train model
   - **Attendance** — filter/export logs, manual override
   - **Reports** — bar/pie/line charts, ranking
   - **Settings** — time thresholds, NTP, camera params, backup

---

## 📡 Full REST API Reference

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/stats` | Dashboard stats (total, present, absent, late, storage, uptime) |
| GET | `/api/status` | System status (camera, wifi, model, faceCount, IP) |
| GET | `/api/storage` | SD card storage info |
| GET | `/api/sync_ntp` | Force NTP re-sync |
| GET | `/api/users` | List all registered users (JSON array) |
| GET | `/api/delete_user?name=X` | Delete user + face encoding |
| GET | `/api/enroll_mode?active=1\|0` | Enable/disable face detection on stream |
| GET | `/api/enroll_capture?id=X&name=Y&dept=Z` | Trigger a face capture for enrollment |
| GET | `/api/logs?date=&dept=&status=&search=` | Attendance records with optional filters |
| GET | `/api/logs_range?days=N` | Per-day summary for charts (up to 90 days) |
| POST | `/api/manual_attendance` | Manual override (body: uid, name, date, time, status, notes) |
| GET | `/api/download_csv?date=YYYY-MM-DD` | Download attendance CSV for a date |
| GET | `/api/clear_logs?date=YYYY-MM-DD` | Delete a day's attendance log |
| GET | `/api/settings` | Get current settings (JSON) |
| POST | `/api/settings` | Save settings (form-encoded body) |

### POST Body Examples

**`/api/manual_attendance`**
```
uid=STU-001&name=John+Doe&date=2025-01-15&status=Excused&time=&notes=Medical+appointment
```

**`/api/settings`**
```
startTime=07:30&endTime=18:00&lateTime=08:10&absentTime=10:00&confidence=85&gmtOffsetSec=3600&ntpServer=pool.ntp.org&buzzerEnabled=0&autoMode=1
```

---

## 📁 SD Card File Formats

### `/db/users.txt` (JSON array)
```json
[
  {"id":"STU-001","name":"John Doe","dept":"Computer Science","role":"Student","regDate":"2025-01-15","faces":5},
  {"id":"STA-001","name":"Dr. Ade","dept":"Engineering","role":"Staff","regDate":"2025-01-01","faces":7}
]
```

### `/atd/log_YYYY-MM-DD.csv`
```
UID,Name,Department,Date,Time,Status,Confidence
STU-001,John Doe,Computer Science,2025-01-15,07:48,Present,92%
STU-002,Jane Ali,Engineering,2025-01-15,08:25,Late,89%
```

### `/cfg/settings.json`
```json
{
  "startTime": "07:30",
  "endTime":   "18:00",
  "lateTime":  "08:10",
  "absentTime":"10:00",
  "confidence": 85,
  "buzzerEnabled": false,
  "autoMode": true,
  "gmtOffsetSec": 3600,
  "ntpServer": "pool.ntp.org"
}
```

---

## ⚡ Attendance Status Logic

| Condition | Status |
|-----------|--------|
| Check-in before `lateTime` (default 08:10) | **Present** |
| Check-in between `lateTime` and `absentTime` (default 10:00) | **Late** |
| No check-in by `absentTime` | **Absent** (manual entry only) |
| Set via manual override | **Excused** |

---

## 🔋 Hardware Notes

- **ESP32-CAM (AI-Thinker)** — only supported board by default
- **MicroSD Card** — FAT32 formatted, Class 10 recommended  
- **Power** — Use a stable 5V/2A supply (USB power banks often cause resets)
- **Optional Buzzer** — Connect between GPIO13 and GND (active when recognition succeeds)
- **Flash LED** — GPIO4 (same as SD card DATA1 — avoid using both simultaneously)

---

## 🛠️ Troubleshooting

| Problem | Fix |
|---------|-----|
| Camera init failed | Check power supply (min 500mA); PSRAM detected? |
| SD Card Mount Failed | Reformat FAT32; check GPIO2 not conflicting |
| Faces not enrolling | Ensure `enroll_mode?active=1` is called first; good lighting |
| Stream not loading | Port 81 must be open; browser must support MJPEG |
| NTP not syncing | Check `gmtOffsetSec` setting; ensure internet access |
| OOM / stack overflow | Increase `cfg.stack_size` in `startCameraServer()` |
| HTML not loading | Clear browser cache; PROGMEM string ~68KB, takes a second |

---

## 📝 Enrollment Flow

1. Admin clicks **"+ Add User"** in portal
2. Enters ID, Name, Department
3. Clicks **"Start Camera"** → stream appears from ESP32-CAM
4. Clicks **"Enroll Face"** 5 times → ESP32 captures face vectors (MTMN → MobileNet)
5. After 5 confirmations, encoding saved to `/FACE.BIN` automatically
6. Click **"Save User"** → user added to `/db/users.txt`

> Each "Enroll Face" click calls `/api/enroll_capture` which sets `is_enrolling = 1`.
> The stream handler's `run_face_recognition()` then uses `enroll_face_with_name()`
> which requires 5 confirmations per enrollment (ENROLL_CONFIRM_TIMES = 5).
> So you should click "Enroll Face" at least 5 times to guarantee all 5 shots are captured.
