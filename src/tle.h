#pragma once
// Data orbit (TLE): diunduh dari CelesTrak lewat HTTPS, disimpan di NVS agar tetap ada tanpa internet.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>

namespace tle {

struct Entry {
  uint32_t norad = 0;
  char name[25] = "";
  char line1[80] = "";
  char line2[80] = "";
  double fetchedUnix = 0;  // waktu unduh (0 = tidak tahu)
  bool valid = false;
};

Entry current;
String lastError = "";

String key(uint32_t norad) { return "t" + String(norad); }

bool load(uint32_t norad) {
  Preferences p;
  p.begin("tle", true);
  Entry e;
  bool ok = p.getBytesLength(key(norad).c_str()) == sizeof(Entry) &&
            p.getBytes(key(norad).c_str(), &e, sizeof(Entry)) == sizeof(Entry) && e.valid;
  p.end();
  if (ok) current = e;
  return ok;
}

void save(const Entry &e) {
  Preferences p;
  p.begin("tle", false);
  p.putBytes(key(e.norad).c_str(), &e, sizeof(Entry));
  p.end();
}

// Unduh TLE satu satelit. nowUnix dipakai untuk mencatat umur data.
bool fetch(uint32_t norad, double nowUnix) {
  if (WiFi.status() != WL_CONNECTED) {
    lastError = "WiFi internet belum tersambung";
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();  // tanpa verifikasi sertifikat (data publik, hanya dibaca)
  HTTPClient http;
  String url = "https://celestrak.org/NORAD/elements/gp.php?CATNR=" + String(norad) + "&FORMAT=tle";
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    lastError = "Gagal membuka koneksi";
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    lastError = "HTTP " + String(code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  // Format: nama \n baris 1 \n baris 2
  body.replace("\r", "");
  int a = body.indexOf('\n');
  int b = body.indexOf('\n', a + 1);
  if (a < 0 || b < 0) {
    lastError = "Satelit tidak ditemukan";
    return false;
  }
  String name = body.substring(0, a);
  String l1 = body.substring(a + 1, b);
  String l2 = body.substring(b + 1);
  int c = l2.indexOf('\n');
  if (c >= 0) l2 = l2.substring(0, c);
  name.trim();
  l1.trim();
  l2.trim();
  if (!l1.startsWith("1 ") || !l2.startsWith("2 ") || l1.length() < 69 || l2.length() < 69) {
    lastError = "Format TLE tidak valid";
    return false;
  }

  Entry e;
  e.norad = norad;
  strlcpy(e.name, name.c_str(), sizeof(e.name));
  strlcpy(e.line1, l1.c_str(), sizeof(e.line1));
  strlcpy(e.line2, l2.c_str(), sizeof(e.line2));
  e.fetchedUnix = nowUnix;
  e.valid = true;
  save(e);
  current = e;
  lastError = "";
  return true;
}

// TLE yang ditempel manual (lewat Serial), misalnya saat tidak ada internet
bool setManual(const char *l1, const char *l2, double nowUnix) {
  if (strncmp(l1, "1 ", 2) != 0 || strncmp(l2, "2 ", 2) != 0 || strlen(l1) < 69 || strlen(l2) < 69) {
    lastError = "Format TLE tidak valid";
    return false;
  }
  Entry e;
  e.norad = atol(l1 + 2);
  snprintf(e.name, sizeof(e.name), "SAT %lu", (unsigned long)e.norad);
  strlcpy(e.line1, l1, sizeof(e.line1));
  strlcpy(e.line2, l2, sizeof(e.line2));
  e.fetchedUnix = nowUnix;
  e.valid = true;
  save(e);
  current = e;
  return true;
}

// Umur epoch TLE dalam hari (dari baris 1 kolom 19-32: YYDDD.DDDDDDDD)
double epochAgeDays(double nowUnix) {
  if (!current.valid || nowUnix <= 0) return -1;
  int yy = atoi(String(current.line1).substring(18, 20).c_str());
  double doy = atof(String(current.line1).substring(20, 32).c_str());
  int year = yy < 57 ? 2000 + yy : 1900 + yy;
  // 1 Januari tahun itu dalam unix
  long y = year - 1;
  long days = 365L * (y - 1969) + (y - 1968) / 4 - (y - 1900) / 100 + (y - 1600) / 400;
  double epochUnix = days * 86400.0 + (doy - 1) * 86400.0;
  return (nowUnix - epochUnix) / 86400.0;
}

}  // namespace tle
