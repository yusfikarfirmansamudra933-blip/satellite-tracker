#pragma once
// Lokasi dan waktu UTC. Sumber waktu: GPS, cadangannya NTP (saat ada internet).
// Sumber lokasi: GPS, cadangannya lokasi tersimpan (config.h atau diisi dari web).

#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <time.h>
#include "config.h"

namespace gps {

TinyGPSPlus parser;
HardwareSerial &port = Serial2;

double lat = DEFAULT_LAT, lon = DEFAULT_LON, alt = DEFAULT_ALT;
bool hasFix = false;          // lokasi dari GPS (bukan cadangan)
const char *timeSource = "-";  // "GPS", "NTP", atau "-"

// Waktu disimpan sebagai pasangan (unix detik, millis saat itu) agar bisa diinterpolasi
double baseUnix = 0;
uint32_t baseMs = 0;
uint32_t lastGpsTimeMs = 0;

void begin() {
  port.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  configTime(0, 0, "pool.ntp.org", "time.google.com");  // NTP, hanya berhasil jika STA tersambung
}

bool timeValid() { return baseUnix > 0; }

// Waktu UTC sekarang dalam detik unix (pecahan detik ikut dihitung)
double now() {
  if (!timeValid()) return 0;
  return baseUnix + (millis() - baseMs) / 1000.0;
}

// Konversi tanggal UTC ke detik unix (tanpa bergantung pada zona waktu sistem)
double toUnix(int y, int mo, int d, int h, int mi, double s) {
  // Algoritma hari sejak 1970-01-01 (Howard Hinnant)
  y -= mo <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153L * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097 + doe - 719468;
  return days * 86400.0 + h * 3600 + mi * 60 + s;
}

void update() {
  while (port.available()) {
    char c = port.read();
    if (!parser.encode(c)) continue;

    // Waktu dari kalimat NMEA yang baru selesai
    if (parser.time.isUpdated() && parser.time.isValid() && parser.date.isValid() &&
        parser.date.year() >= 2024) {
      baseUnix = toUnix(parser.date.year(), parser.date.month(), parser.date.day(),
                        parser.time.hour(), parser.time.minute(),
                        parser.time.second() + parser.time.centisecond() / 100.0);
      baseMs = millis();
      lastGpsTimeMs = baseMs;
      timeSource = "GPS";
    }
    if (parser.location.isUpdated() && parser.location.isValid()) {
      lat = parser.location.lat();
      lon = parser.location.lng();
      if (parser.altitude.isValid()) alt = parser.altitude.meters();
      hasFix = true;
    }
  }

  // GPS tidak memberi waktu > 5 s: pakai NTP jika sistem sudah sinkron
  if (millis() - lastGpsTimeMs > 5000 || lastGpsTimeMs == 0) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    if (tv.tv_sec > 1700000000) {
      baseUnix = tv.tv_sec + tv.tv_usec / 1e6;
      baseMs = millis();
      timeSource = "NTP";
    }
  }
  if (hasFix && parser.location.age() > 10000) hasFix = false;  // fix hilang
}

int satellites() { return parser.satellites.isValid() ? parser.satellites.value() : 0; }
uint32_t charsReceived() { return parser.charsProcessed(); }

void setManualTime(double unix) {
  baseUnix = unix;
  baseMs = millis();
  timeSource = "manual";
}

void setManualLocation(double la, double lo, double al) {
  lat = la;
  lon = lo;
  alt = al;
}

}  // namespace gps
