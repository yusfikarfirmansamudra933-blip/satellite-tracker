#pragma once
// Perhitungan posisi satelit (SGP4/SDP4) dan prediksi lintasan.

#include <Arduino.h>
#include <Sgp4.h>
#include "config.h"
#include "tle.h"

namespace tracker {

struct Pass {
  double start, peak, stop;  // unix UTC
  float maxEl, azStart, azStop;
  int sight;                 // 0 = siang, 1 = dalam bayangan bumi, 2 = terlihat mata (terkena matahari, langit gelap)
};

struct PathPoint { float az, el; };
const int PATH_POINTS = 40;

Sgp4 sat;
bool ready = false;
bool geo = false;  // satelit "diam" (geostasioner / orbit sangat tinggi): tidak ada lintasan

// Posisi sekarang
double az = 0, el = 0, distKm = 0, subLat = 0, subLon = 0, altKm = 0;
double azRate = 0, elRate = 0;  // derajat/detik

Pass passes[PASS_COUNT];
int passCount = 0;
PathPoint path[PATH_POINTS];  // jalur lintasan berikutnya / yang sedang berlangsung
int pathCount = 0;
double predictedAt = 0;
double predictLat = 999, predictLon = 999;

double toJd(double unix) { return unix / 86400.0 + 2440587.5; }
double toUnix(double jd) { return (jd - 2440587.5) * 86400.0; }

bool begin(const tle::Entry &e, double lat, double lon, double alt) {
  // twoline2rv mengubah isi string, jadi pakai salinan
  char n[25], l1[130], l2[130];
  strlcpy(n, e.name, sizeof(n));
  strlcpy(l1, e.line1, sizeof(l1));
  strlcpy(l2, e.line2, sizeof(l2));
  sat.site(lat, lon, alt);
  // init() mengembalikan false jika TLE sama persis dengan yang sudah dimuat; itu bukan error
  bool same = strcmp(sat.line1, l1) == 0 && strcmp(sat.line2, l2) == 0;
  ready = (same || sat.init(n, l1, l2)) && sat.satrec.error == 0;
  geo = ready && sat.revpday < 1.5;
  passCount = pathCount = 0;
  predictedAt = 0;
  return ready;
}

void setSite(double lat, double lon, double alt) { sat.site(lat, lon, alt); }

void positionAt(double unix, double &a, double &e) {
  sat.findsat(toJd(unix));
  a = sat.satAz;
  e = sat.satEl;
}

// Diff sudut azimuth dalam rentang -180..180
double azDiff(double a, double b) {
  double d = fmod(a - b, 360.0);
  if (d > 180) d -= 360;
  if (d < -180) d += 360;
  return d;
}

void update(double unix) {
  if (!ready || unix <= 0) return;
  double a2, e2;
  positionAt(unix + 0.5, a2, e2);
  sat.findsat(toJd(unix));
  az = sat.satAz;
  el = sat.satEl;
  distKm = sat.satDist;
  subLat = sat.satLat;
  subLon = sat.satLon;
  altKm = sat.satAlt;
  azRate = azDiff(a2, az) / 0.5;
  elRate = (e2 - el) / 0.5;
}

void samplePath(const Pass &p) {
  pathCount = 0;
  for (int i = 0; i < PATH_POINTS; i++) {
    double t = p.start + (p.stop - p.start) * i / (PATH_POINTS - 1);
    double a, e;
    positionAt(t, a, e);
    path[pathCount++] = {(float)a, (float)max(e, 0.0)};
  }
}

// Prediksi lintasan mulai dari sedikit sebelum "sekarang" agar lintasan yang sedang berjalan ikut terhitung
void predict(double unix, double lat, double lon) {
  passCount = pathCount = 0;
  predictedAt = unix;
  predictLat = lat;
  predictLon = lon;
  if (!ready || geo || unix <= 0) return;

  sat.initpredpoint(toJd(unix - 1200), 0.0);
  passinfo info;
  int guard = 0;
  while (passCount < PASS_COUNT && guard++ < PASS_COUNT + 5) {
    if (!sat.nextpass(&info, 20)) break;
    Pass p = {toUnix(info.jdstart), toUnix(info.jdmax), toUnix(info.jdstop), (float)info.maxelevation,
              (float)info.azstart, (float)info.azstop, (int)info.sight};
    if (p.stop < unix) continue;  // sudah lewat
    passes[passCount++] = p;
  }
  if (passCount > 0) samplePath(passes[0]);
}

// Perlu prediksi ulang? (lintasan pertama selesai, lokasi pindah, atau belum pernah)
bool needsPredict(double unix, double lat, double lon) {
  if (!ready || geo || unix <= 0) return false;
  if (predictedAt == 0) return true;
  if (fabs(lat - predictLat) > 0.05 || fabs(lon - predictLon) > 0.05) return true;
  if (passCount == 0) return unix - predictedAt > 600;  // coba lagi tiap 10 menit
  return unix > passes[0].stop + 5;
}

bool inPass(double unix) { return passCount > 0 && unix >= passes[0].start && unix <= passes[0].stop; }

// Pilih sudut pan awal (az + k*360) untuk lintasan berikutnya sehingga seluruh jalurnya
// muat dalam batas pan, dan sedekat mungkin dengan posisi pan sekarang.
double choosePassStartPan(double currentPan) {
  if (pathCount < 2) return azDiff(passes[0].azStart, 0);
  // Jalur azimuth dibuat menerus (tanpa loncatan 360)
  double minU = 0, maxU = 0, u = 0;
  for (int i = 1; i < pathCount; i++) {
    u += azDiff(path[i].az, path[i - 1].az);
    minU = min(minU, u);
    maxU = max(maxU, u);
  }
  double start = path[0].az, best = azDiff(start, 0);
  double bestCost = 1e9;
  for (int k = -2; k <= 2; k++) {
    double s = start + 360.0 * k;
    double lo = s + minU, hi = s + maxU;
    double over = max(0.0, PAN_MIN_DEG + 5 - lo) + max(0.0, hi - (PAN_MAX_DEG - 5));
    double cost = over * 1000 + fabs(s - currentPan);
    if (cost < bestCost) { bestCost = cost; best = s; }
  }
  return best;
}

}  // namespace tracker
