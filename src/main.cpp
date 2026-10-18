/*
 * Pelacak Satelit - ESP32 + GPS + 2 motor stepper 28BYJ-48 (pan-tilt)
 *
 * ESP32 menghitung posisi satelit dengan SGP4 dari data orbit (TLE) CelesTrak,
 * lalu PAN (azimuth, 0 = utara) dan TILT (elevasi, 0 = horizon) mengarahkan
 * penunjuk tepat ke satelit itu. GPS memberi lokasi dan waktu UTC.
 *
 * Kontrol: sambungkan HP ke WiFi "SatTracker", buka http://192.168.4.1
 *
 * Perintah Serial Monitor (115200 baud):
 *   T              mode TRACK (ikuti satelit)
 *   G <az> <el>    arahkan ke azimuth/elevasi (derajat), untuk tes
 *   S              berhenti (MANUAL)
 *   N              posisi sekarang = utara (pan 0)
 *   D              posisi sekarang = datar (tilt 0)
 *   P              status: waktu, lokasi, posisi satelit, posisi mount
 *   L              daftar lintasan berikutnya
 *   U              unduh ulang TLE
 *   I <norad>      ganti satelit (NORAD ID)
 *   Z <unix>       atur waktu UTC manual (jika tidak ada GPS/internet)
 *   1 ... / 2 ...  tempel 2 baris TLE langsung (tanpa internet)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "config.h"
#include "motors.h"
#include "gps.h"
#include "tle.h"
#include "tracker.h"
#include "web_page.h"

// ---------------- Pengaturan tersimpan ----------------
struct Settings {
  uint32_t version;
  uint32_t norad;
  bool invPan, invTilt;
  double lat, lon, alt;  // lokasi cadangan saat GPS belum fix
  char staSsid[33];
  char staPass[65];
};
const uint32_t SETTINGS_VERSION = 1;
Settings cfg;
Preferences prefs;

void defaultSettings() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.version = SETTINGS_VERSION;
  cfg.norad = 25544;  // ISS
  cfg.lat = DEFAULT_LAT;
  cfg.lon = DEFAULT_LON;
  cfg.alt = DEFAULT_ALT;
  strlcpy(cfg.staSsid, STA_SSID, sizeof(cfg.staSsid));
  strlcpy(cfg.staPass, STA_PASS, sizeof(cfg.staPass));
}

void loadSettings() {
  defaultSettings();
  prefs.begin("sat", false);
  Settings s;
  if (prefs.getBytesLength("cfg") == sizeof(Settings) &&
      prefs.getBytes("cfg", &s, sizeof(Settings)) == sizeof(Settings) && s.version == SETTINGS_VERSION) {
    cfg = s;
  }
}

void saveSettings() { prefs.putBytes("cfg", &cfg, sizeof(Settings)); }

// ---------------- Status ----------------
enum Mode { TRACK, MANUAL, GOTO };
const char *MODE_NAMES[] = {"TRACK", "MANUAL", "GOTO"};
Mode mode = TRACK;

WebServer server(80);

float targetPan = 0, targetTilt = 0;  // sudut tujuan mount (derajat)
double panRef = 0;                    // azimuth menerus (bisa < 0 atau > 360) untuk menghindari batas kabel
double passStartRef = -1;             // waktu mulai lintasan yang cabang pan-nya sudah dipilih
double passBranch = 0;                // sudut pan awal lintasan berikutnya (cabang yang dipilih)
float manualPan = 0, manualTilt = 0;
uint32_t manualUntil = 0;
String status = "";                   // pesan untuk web
uint32_t lastTleTry = 0;
double siteLat = 999, siteLon = 999;

// ---------------- Mount ----------------
float mountPan() { return motors::panDeg(); }
float mountTilt() { return motors::tiltDeg(); }

void stopMount() { motors::setVelocity(0, 0); }

// Kecepatan = laju target (feed-forward) + koreksi posisi
void driveTo(float pan, float tilt, float panRate, float tiltRate) {
  targetPan = constrain(pan, PAN_MIN_DEG, PAN_MAX_DEG);
  targetTilt = constrain(tilt, TILT_MIN_DEG, TILT_MAX_DEG);
  float vp = panRate + TRACK_KP * (targetPan - mountPan());
  float vt = tiltRate + TRACK_KP * (targetTilt - mountTilt());
  motors::setVelocity(vp, vt);
}

// Azimuth -> sudut pan terdekat dari referensi (menghindari putaran 360 yang tidak perlu)
double nearestPan(double az, double ref) { return az + 360.0 * round((ref - az) / 360.0); }

void setMode(Mode m) {
  mode = m;
  manualUntil = 0;
  panRef = mountPan();  // lanjutkan dari posisi fisik sekarang
  passStartRef = -1;
  stopMount();
}

void startGoto(float az, float el) {
  setMode(GOTO);
  double p = nearestPan(az, mountPan());
  if (p > PAN_MAX_DEG) p -= 360;
  if (p < PAN_MIN_DEG) p += 360;
  targetPan = p;
  targetTilt = constrain(el, TILT_MIN_DEG, TILT_MAX_DEG);
}

// ---------------- Satelit ----------------
void initSatellite() {
  passStartRef = -1;
  if (tle::current.valid && tle::current.norad == cfg.norad) {
    tracker::begin(tle::current, gps::lat, gps::lon, gps::alt);
    siteLat = gps::lat;
    siteLon = gps::lon;
  } else {
    tracker::ready = false;
  }
}

bool downloadTle() {
  stopMount();  // unduhan memblokir beberapa detik
  lastTleTry = millis();
  status = "Mengunduh TLE " + String(cfg.norad) + "...";
  bool ok = tle::fetch(cfg.norad, gps::now());
  status = ok ? "TLE diperbarui: " + String(tle::current.name) : "Gagal unduh TLE: " + tle::lastError;
  Serial.println(status);
  if (ok) initSatellite();
  return ok;
}

void selectSatellite(uint32_t norad) {
  cfg.norad = norad;
  saveSettings();
  stopMount();
  if (tle::load(norad)) {
    status = "Memakai TLE tersimpan: " + String(tle::current.name);
    initSatellite();
  } else {
    tracker::ready = false;
    tle::current.valid = false;
    if (!downloadTle()) status = "TLE untuk " + String(norad) + " belum ada. Sambungkan WiFi internet.";
  }
}

// Unduh TLE otomatis jika belum ada / terlalu tua, maksimal sekali tiap 10 menit
void maintainTle() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (lastTleTry != 0 && millis() - lastTleTry < 600000) return;
  double now = gps::now();
  bool stale = !tle::current.valid || tle::current.norad != cfg.norad ||
               (now > 0 && (tle::current.fetchedUnix == 0 || now - tle::current.fetchedUnix > TLE_MAX_AGE_HOURS * 3600.0));
  if (!stale) return;
  if (mode == TRACK && tracker::inPass(now)) return;  // jangan ganggu lintasan yang sedang diikuti
  downloadTle();
}

void updateTrack(double now) {
  if (!tracker::ready || now <= 0) { stopMount(); return; }

  if (tracker::geo) {
    // Satelit diam di langit: tunjuk saja (jika di atas horizon)
    if (tracker::el > 0) driveTo(nearestPan(tracker::az, mountPan()), tracker::el, 0, 0);
    else stopMount();
    return;
  }

  bool passing = tracker::inPass(now) && tracker::el > -1;
  double pan = nearestPan(tracker::az, panRef);

  if (!passing) {
    const tracker::Pass *p = tracker::passCount > 0 ? &tracker::passes[0] : nullptr;
    if (p && p->start - now < PASS_PREPARE_SEC) {
      // Menjelang lintasan: pakai cabang pan yang memuat seluruh jalur lintasan
      if (passStartRef != p->start) {
        passBranch = tracker::choosePassStartPan(mountPan());
        passStartRef = p->start;
      }
      pan = nearestPan(tracker::az, passBranch);
    } else if (pan > PAN_MAX_DEG - UNWIND_MARGIN_DEG) {
      pan -= 360;  // satelit masih di bawah horizon: putar balik agar kabel tidak terpelintir
    } else if (pan < PAN_MIN_DEG + UNWIND_MARGIN_DEG) {
      pan += 360;
    }
  }

  // Pan selalu mengikuti azimuth satelit; tilt mengikuti elevasi (di bawah horizon tetap 0°)
  panRef = pan;
  driveTo(panRef, max(tracker::el, 0.0), tracker::azRate, tracker::el > 0 ? tracker::elRate : 0);
}

// ---------------- Simpan posisi mount ----------------
// Motor ditahan terus, jadi posisi disimpan saat diam agar tetap benar setelah ESP32 dimatikan.
void persistPosition() {
  static float savedPan = 1e9, savedTilt = 1e9;
  static uint32_t stillSince = 0, savedMs = 0;
  static float lastPan = 0, lastTilt = 0;
  float p = mountPan(), t = mountTilt();
  if (p != lastPan || t != lastTilt) {
    lastPan = p;
    lastTilt = t;
    stillSince = millis();
  }
  bool changed = fabs(p - savedPan) > 0.05 || fabs(t - savedTilt) > 0.05;
  // Simpan saat diam 3 detik, atau tiap 60 detik saat terus bergerak mengikuti satelit
  if (changed && (millis() - stillSince > 3000 || millis() - savedMs > 60000)) {
    prefs.putFloat("pan", p);
    prefs.putFloat("tilt", t);
    savedPan = p;
    savedTilt = t;
    savedMs = millis();
  }
}

// ---------------- Web ----------------
String num(double v, int d) { return isfinite(v) ? String(v, d) : "null"; }

void handleStatus() {
  double now = gps::now();
  String j;
  j.reserve(3000);
  j += "{\"now\":" + num(now, 1);
  j += ",\"timeSrc\":\"" + String(gps::timeSource) + "\"";
  j += ",\"gpsFix\":" + String(gps::hasFix ? "true" : "false");
  j += ",\"gpsSats\":" + String(gps::satellites());
  j += ",\"gpsChars\":" + String(gps::charsReceived());
  j += ",\"lat\":" + num(gps::lat, 5) + ",\"lon\":" + num(gps::lon, 5) + ",\"alt\":" + num(gps::alt, 0);
  j += ",\"wifi\":\"" + String(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "") + "\"";
  j += ",\"ssid\":\"" + String(cfg.staSsid) + "\"";
  j += ",\"mode\":\"" + String(MODE_NAMES[mode]) + "\"";
  j += ",\"msg\":\"" + status + "\"";
  j += ",\"norad\":" + String(cfg.norad);
  j += ",\"ready\":" + String(tracker::ready ? "true" : "false");
  j += ",\"geo\":" + String(tracker::geo ? "true" : "false");
  j += ",\"name\":\"" + String(tle::current.valid ? tle::current.name : "") + "\"";
  j += ",\"tleAge\":" + num(tle::epochAgeDays(now), 1);
  j += ",\"az\":" + num(tracker::az, 2) + ",\"el\":" + num(tracker::el, 2);
  j += ",\"dist\":" + num(tracker::distKm, 0) + ",\"satAlt\":" + num(tracker::altKm, 0);
  j += ",\"subLat\":" + num(tracker::subLat, 2) + ",\"subLon\":" + num(tracker::subLon, 2);
  j += ",\"pan\":" + num(mountPan(), 2) + ",\"tilt\":" + num(mountTilt(), 2);
  j += ",\"tPan\":" + num(targetPan, 2) + ",\"tTilt\":" + num(targetTilt, 2);
  j += ",\"invPan\":" + String(cfg.invPan ? "true" : "false");
  j += ",\"invTilt\":" + String(cfg.invTilt ? "true" : "false");
  j += ",\"passes\":[";
  for (int i = 0; i < tracker::passCount; i++) {
    const tracker::Pass &p = tracker::passes[i];
    if (i) j += ",";
    j += "[" + num(p.start, 0) + "," + num(p.peak, 0) + "," + num(p.stop, 0) + "," + num(p.maxEl, 1) + "," +
         num(p.azStart, 0) + "," + num(p.azStop, 0) + "," + String(p.sight) + "]";
  }
  j += "],\"path\":[";
  for (int i = 0; i < tracker::pathCount; i++) {
    if (i) j += ",";
    j += "[" + num(tracker::path[i].az, 1) + "," + num(tracker::path[i].el, 1) + "]";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

void handleCmd() {
  String c = server.arg("c");
  if (c == "track") setMode(TRACK);
  else if (c == "stop") setMode(MANUAL);
  else if (c == "park") startGoto(0, 0);
  else if (c == "north") { setMode(MANUAL); motors::setPosition(0, 0); passStartRef = -1; status = "Utara ditetapkan"; }
  else if (c == "level") { setMode(MANUAL); motors::setPosition(1, 0); status = "Datar ditetapkan"; }
  else if (c == "tle") downloadTle();
  server.send(200, "text/plain", "OK");
}

void handleGoto() {
  startGoto(server.arg("az").toFloat(), server.arg("el").toFloat());
  server.send(200, "text/plain", "OK");
}

void handleMove() {
  if (mode != MANUAL) setMode(MANUAL);
  manualPan = server.arg("pan").toFloat() * 20;
  manualTilt = server.arg("tilt").toFloat() * 20;
  manualUntil = millis() + 400;
  server.send(200, "text/plain", "OK");
}

void handleSat() {
  uint32_t id = server.arg("id").toInt();
  if (id > 0) selectSatellite(id);
  server.send(200, "text/plain", "OK");
}

void handleSet() {
  String k = server.arg("k");
  bool v = server.arg("v").toInt() != 0;
  if (k == "invPan") cfg.invPan = v;
  else if (k == "invTilt") cfg.invTilt = v;
  else { server.send(400, "text/plain", "unknown key"); return; }
  motors::setDirection(cfg.invPan, cfg.invTilt);
  saveSettings();
  server.send(200, "text/plain", "OK");
}

void handleLoc() {
  cfg.lat = server.arg("lat").toDouble();
  cfg.lon = server.arg("lon").toDouble();
  cfg.alt = server.arg("alt").toDouble();
  saveSettings();
  if (!gps::hasFix) gps::setManualLocation(cfg.lat, cfg.lon, cfg.alt);
  status = "Lokasi cadangan disimpan";
  server.send(200, "text/plain", "OK");
}

void handleWifi() {
  strlcpy(cfg.staSsid, server.arg("s").c_str(), sizeof(cfg.staSsid));
  strlcpy(cfg.staPass, server.arg("p").c_str(), sizeof(cfg.staPass));
  saveSettings();
  server.send(200, "text/plain", "OK");
  WiFi.disconnect();
  if (cfg.staSsid[0]) WiFi.begin(cfg.staSsid, cfg.staPass);
  status = "Menyambung ke WiFi " + String(cfg.staSsid);
}

// ---------------- Serial ----------------
void printPasses() {
  if (tracker::geo) { Serial.println("Satelit geostasioner: tidak ada lintasan, selalu di posisi yang sama."); return; }
  if (tracker::passCount == 0) { Serial.println("Belum ada prediksi lintasan."); return; }
  for (int i = 0; i < tracker::passCount; i++) {
    const tracker::Pass &p = tracker::passes[i];
    time_t s = (time_t)p.start;
    struct tm t;
    gmtime_r(&s, &t);
    Serial.printf("%d. %04d-%02d-%02d %02d:%02d UTC  durasi %3.0f s  el maks %4.1f  az %3.0f -> %3.0f  %s\n", i + 1,
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, p.stop - p.start, p.maxEl,
                  p.azStart, p.azStop, p.sight == 2 ? "TERLIHAT MATA" : (p.sight == 1 ? "gelap" : "siang"));
  }
}

void handleSerialLine(char *line) {
  float a = 0, b = 0;
  unsigned long id = 0;
  double d = 0;
  static char tleLine1[80] = "";

  // TLE ditempel: baris "1 ..." lalu "2 ..."
  if (line[0] == '1' && line[1] == ' ') {
    strlcpy(tleLine1, line, sizeof(tleLine1));
    return;
  }
  if (line[0] == '2' && line[1] == ' ') {
    if (tle::setManual(tleLine1, line, gps::now())) {
      cfg.norad = tle::current.norad;
      saveSettings();
      initSatellite();
      Serial.printf("OK TLE %lu dipakai\n", (unsigned long)cfg.norad);
    } else {
      Serial.println("ERR " + tle::lastError);
    }
    return;
  }

  switch (toupper(line[0])) {
    case 'T': setMode(TRACK); break;
    case 'G':
      if (sscanf(line + 1, "%f %f", &a, &b) == 2) startGoto(a, b);
      break;
    case 'S': setMode(MANUAL); break;
    case 'N': setMode(MANUAL); motors::setPosition(0, 0); passStartRef = -1; break;
    case 'D': setMode(MANUAL); motors::setPosition(1, 0); break;
    case 'U': downloadTle(); break;
    case 'I':
      if (sscanf(line + 1, "%lu", &id) == 1) selectSatellite(id);
      break;
    case 'Z':
      if (sscanf(line + 1, "%lf", &d) == 1) gps::setManualTime(d);
      break;
    case 'L': printPasses(); return;
    case 'P':
      tracker::update(gps::now());
      Serial.printf("Waktu %.1f (%s)  GPS fix=%d sat=%d chars=%lu  lokasi %.5f %.5f %.0fm\n", gps::now(),
                    gps::timeSource, gps::hasFix, gps::satellites(), (unsigned long)gps::charsReceived(), gps::lat,
                    gps::lon, gps::alt);
      Serial.printf("%s (%lu) siap=%d geo=%d  az %.2f el %.2f  jarak %.0f km  umur TLE %.1f hari\n",
                    tle::current.name, (unsigned long)cfg.norad, tracker::ready, tracker::geo, tracker::az,
                    tracker::el, tracker::distKm, tle::epochAgeDays(gps::now()));
      Serial.printf("Mode %s  mount pan %.2f tilt %.2f  target %.2f %.2f\n", MODE_NAMES[mode], mountPan(),
                    mountTilt(), targetPan, targetTilt);
      return;
    default: Serial.printf("ERR %s\n", line); return;
  }
  Serial.println("OK");
}

void readSerial() {
  static char buf[160];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (len) {
        buf[len] = 0;
        handleSerialLine(buf);
        len = 0;
      }
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

// ---------------- Program utama ----------------
void setup() {
  Serial.begin(115200);
  loadSettings();

  motors::begin();
  motors::setDirection(cfg.invPan, cfg.invTilt);
  motors::setPosition(0, prefs.getFloat("pan", 0));
  motors::setPosition(1, prefs.getFloat("tilt", 0));

  gps::begin();
  gps::setManualLocation(cfg.lat, cfg.lon, cfg.alt);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  if (cfg.staSsid[0]) WiFi.begin(cfg.staSsid, cfg.staPass);
  Serial.printf("WiFi \"%s\" aktif, buka http://%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  server.on("/", [] { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/status", handleStatus);
  server.on("/cmd", handleCmd);
  server.on("/goto", handleGoto);
  server.on("/move", handleMove);
  server.on("/sat", handleSat);
  server.on("/set", handleSet);
  server.on("/loc", handleLoc);
  server.on("/wifi", handleWifi);
  server.begin();

  if (tle::load(cfg.norad)) {
    initSatellite();
    status = "Memakai TLE tersimpan: " + String(tle::current.name);
  } else {
    status = "Belum ada TLE. Isi WiFi internet di halaman web.";
  }
  Serial.println(status);
  Serial.println("READY");
}

void loop() {
  server.handleClient();
  readSerial();
  gps::update();
  if (!gps::hasFix) gps::setManualLocation(cfg.lat, cfg.lon, cfg.alt);
  maintainTle();

  double now = gps::now();

  // Lokasi berubah: perbarui posisi pengamat
  if (tracker::ready && (fabs(gps::lat - siteLat) > 1e-4 || fabs(gps::lon - siteLon) > 1e-4)) {
    tracker::setSite(gps::lat, gps::lon, gps::alt);
    siteLat = gps::lat;
    siteLon = gps::lon;
  }
  if (tracker::needsPredict(now, gps::lat, gps::lon)) {
    stopMount();  // prediksi memblokir sebentar
    uint32_t t0 = millis();
    tracker::predict(now, gps::lat, gps::lon);
    passStartRef = -1;
    Serial.printf("Prediksi %d lintasan: %lu ms\n", tracker::passCount, (unsigned long)(millis() - t0));
  }

  static uint32_t lastCtrl = 0;
  if (millis() - lastCtrl >= 50) {  // 20 Hz
    lastCtrl = millis();
    tracker::update(now);

    switch (mode) {
      case TRACK:
        updateTrack(now);
        break;
      case MANUAL:
        if (millis() < manualUntil) motors::setVelocity(manualPan, manualTilt);
        else stopMount();
        break;
      case GOTO:
        if (fabs(targetPan - mountPan()) < 0.1f && fabs(targetTilt - mountTilt()) < 0.1f) {
          setMode(MANUAL);
        } else {
          driveTo(targetPan, targetTilt, 0, 0);
        }
        break;
    }
    persistPosition();
  }
}
