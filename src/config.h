#pragma once
// Semua pengaturan hardware ada di sini.

// ---------------- WiFi ----------------
// Access Point selalu aktif: sambungkan HP ke WiFi ini, buka http://192.168.4.1
#define AP_SSID "SatTracker"
#define AP_PASS "satelit123"  // minimal 8 karakter

// WiFi rumah / hotspot HP untuk mengunduh data orbit (TLE) dan jam internet.
// Boleh dikosongkan; bisa juga diisi lewat halaman web.
#define STA_SSID ""
#define STA_PASS ""

// ---------------- GPS (NEO-6M atau sejenis, NMEA 9600 baud) ----------------
#define GPS_RX_PIN 16  // ke pin TX modul GPS
#define GPS_TX_PIN 17  // ke pin RX modul GPS
#define GPS_BAUD   9600

// Lokasi cadangan jika GPS belum fix (bisa diubah dari web). Default: Jakarta.
#define DEFAULT_LAT -6.2000
#define DEFAULT_LON 106.8166
#define DEFAULT_ALT 10.0

// ---------------- 74HC595 -> 2x ULN2003 ----------------
// Q0..Q3 -> IN1..IN4 driver PAN, Q4..Q7 -> IN1..IN4 driver TILT
#define SR_DATA  13  // pin 14 (DS)
#define SR_CLOCK 14  // pin 11 (SHCP)
#define SR_LATCH 4   // pin 12 (STCP)

// ---------------- Motor 28BYJ-48 ----------------
#define STEPS_PER_REV    4096.0f  // half-step
#define MAX_SPEED_DPS    60.0f    // derajat/detik; di atas ini 28BYJ-48 mulai selip
#define ACCEL_DPS2       120.0f   // percepatan, derajat/detik^2
#define TICK_US          500      // periode timer motor (2000 Hz)
#define RELEASE_AFTER_MS 0        // 0 = koil selalu ditahan (posisi tidak bergeser saat menunggu satelit)

// Batas gerak dari posisi nol. PAN 0 = utara (setelah "Set utara"), TILT 0 = horizon.
#define PAN_MIN_DEG  -270.0f  // 1,5 putaran total: lintasan apa pun selalu muat tanpa putar balik (beri kabel kendur)
#define PAN_MAX_DEG   270.0f
#define TILT_MIN_DEG  -5.0f
#define TILT_MAX_DEG  95.0f

// ---------------- Pelacakan ----------------
#define TRACK_KP          2.0f   // koreksi posisi, derajat/detik per derajat error
#define PASS_PREPARE_SEC  120    // sekian detik sebelum satelit muncul, posisi pan disiapkan untuk seluruh lintasan
#define UNWIND_MARGIN_DEG 20.0f  // putar balik 360° jika pan sedekat ini ke batas (hanya saat satelit di bawah horizon)
#define PASS_COUNT        5      // jumlah lintasan yang diprediksi
#define TLE_MAX_AGE_HOURS 48     // unduh ulang TLE jika lebih tua dari ini (saat ada internet)
