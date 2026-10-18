# 🛰️ Pelacak Satelit (ESP32 + GPS + 2 Stepper)

🌐 **Halaman proyek:** https://yusfikarfirmansamudra933-blip.github.io/satellite-tracker/  
🔭 **Pelacak online (tanpa alat):** https://yusfikarfirmansamudra933-blip.github.io/satellite-tracker/tracker.html

Mount pan-tilt yang mengarahkan penunjuk (panah/sedotan) tepat ke satelit di langit secara
real-time: ISS, Tiangong, Hubble, satelit cuaca, satelit radio amatir, sampai satelit geostasioner.

```
CelesTrak (internet) ──TLE──► ESP32 ◄── GPS (lokasi + waktu UTC)
                               │  hitung posisi satelit (SGP4/SDP4)
                               ▼
                   ├──► ULN2003 ─► 28BYJ-48 PAN  (azimuth, 0° = utara)
                   └──► ULN2003 ─► 28BYJ-48 TILT (elevasi, 0° = horizon)
```

- **PAN** memutar penunjuk ke arah mata angin (azimuth).
- **TILT** mengangkat penunjuk dari horizon (0°) sampai tegak ke atas (90°).
- Internet hanya dibutuhkan sesekali untuk mengunduh data orbit (TLE). Setelah itu alat bisa
  dibawa ke lapangan tanpa internet, karena waktu dan lokasi diambil dari GPS.

Hasil perhitungan sudah diuji di ESP32 dan dibandingkan dengan library astronomi Skyfield:
selisih posisi **< 0,05°**, dan prediksi lintasan cocok sampai menitnya.

## Komponen

- ESP32 DevKit
- 2× ULN2003 + 2× 28BYJ-48, disambung **langsung** ke ESP32 (tanpa IC tambahan)
- Modul GPS (NEO-6M atau sejenis, output NMEA 9600 baud)
- Penunjuk: panah, sedotan, atau tabung kecil sebagai "teropong bidik". **Jangan pakai laser**,
  karena berbahaya bagi pilot pesawat.

Kabel kamera OV7670 dan IC 74HC595 tidak dipakai lagi, cabut semua.

## Wiring

### GPS → ESP32

| Pin GPS | Sambung ke |
|---|---|
| VCC | 3.3V atau 5V (lihat tulisan di modul; NEO-6M umumnya bisa keduanya) |
| GND | GND |
| TX | **GPIO 16** (RX2) |
| RX | **GPIO 17** (TX2) |

TX GPS ke RX ESP32, dan sebaliknya. Antena keramik GPS harus **menghadap ke langit**. Di dalam
ruangan, GPS biasanya tidak dapat sinyal.

### ESP32 → ULN2003 (langsung)

| ESP32 | Driver **PAN** | | ESP32 | Driver **TILT** |
|---|---|---|---|---|
| GPIO 13 | IN1 | | GPIO 25 | IN1 |
| GPIO 14 | IN2 | | GPIO 33 | IN2 |
| GPIO 27 | IN3 | | GPIO 32 | IN3 |
| GPIO 26 | IN4 | | GPIO 23 | IN4 |

- `+` dan `−` kedua ULN2003 → **5V** dan GND power supply.
- **GND power supply wajib disambung ke GND ESP32.**
- Pasang kapasitor 100–470µF di antara 5V dan GND, dekat driver motor.

Masih punya rangkaian 74HC595? Ubah `USE_74HC595` menjadi `1` di [src/config.h](src/config.h)
(DS = GPIO 13, SHCP = GPIO 14, STCP = GPIO 4).

### Mekanik

- PAN bisa berputar **±270°** (1,5 putaran) supaya lintasan satelit apa pun bisa diikuti tanpa
  putar balik. **Beri kabel motor TILT cukup kendur** untuk 1,5 putaran.
- TILT bergerak dari −5° sampai 95°.
- Pasang penunjuk sejajar dengan arah "depan" mount.

## Upload (VS Code + PlatformIO)

1. Di VS Code: `File → Open Folder…`, pilih folder `satellite tracker`.
2. Klik **✓** (Build). Library Sgp4 dan TinyGPSPlus terunduh otomatis.
3. Klik **→** (Upload).
4. Klik **🔌** (Serial Monitor). Harus muncul `READY`.

## Pemakaian pertama

### 1. Sambungkan ke internet (untuk data orbit)
1. Di HP, sambungkan ke WiFi **`SatTracker`** (password `satelit123`), lalu buka `http://192.168.4.1`.
2. Di bagian **WiFi internet**, isi nama dan password WiFi rumah atau hotspot HP lain, lalu tekan **Simpan**.
3. ESP32 tersambung ke internet, mengunduh TLE, dan menyetel jam. Status ada di layar.

Tanpa internet, TLE bisa ditempel lewat Serial Monitor (lihat **Perintah Serial**).

### 2. Kalibrasi arah (wajib)
1. Letakkan mount di permukaan **datar**.
2. Buka kompas di HP. Pakai **utara sebenarnya** (true north) jika ada pilihannya.
3. Tekan **STOP**, lalu pakai tombol ◀ ▶ ▲ ▼ sampai penunjuk **lurus ke utara dan datar**.
4. Tekan **Set utara**, lalu **Set datar**.
5. Tes: masukkan GOTO azimuth `90`, elevasi `0`. Penunjuk harus mengarah ke **timur**.
   - Kalau malah ke barat, centang **Balik arah pan**, lalu ulangi kalibrasi.
   - GOTO elevasi `45` harus mengangkat penunjuk **ke atas**. Kalau ke bawah, centang **Balik arah tilt**.

Posisi mount disimpan otomatis saat berhenti, jadi tidak perlu kalibrasi ulang setelah ESP32
dimatikan, asalkan mount tidak diputar dengan tangan.

### 3. Lacak satelit
1. Pilih satelit dari daftar, atau ketik NORAD ID lalu tekan **Pakai**.
2. Tekan **TRACK**.
3. **Pan selalu mengikuti arah satelit**, meskipun satelit sedang di bawah horizon (di sisi lain
   bumi). Kalau satelit bergerak ke utara, penunjuk ikut berputar ke utara. Tilt tetap di 0° (datar)
   selama satelit di bawah horizon, lalu ikut naik saat satelit muncul.
4. Agar kabel tidak terpelintir, pan bisa **berputar balik 360°** secara otomatis saat mendekati
   batas ±270°. Ini hanya terjadi saat satelit di bawah horizon, tidak pernah di tengah lintasan.
   2 menit sebelum satelit muncul, posisi pan disiapkan supaya seluruh lintasan muat.

## Tampilan web

- **Peta langit**: tengah = tepat di atas kepala, lingkaran luar = horizon, U/T/S/B = arah mata angin.
  - Garis biru = jalur lintasan berikutnya (titik biru = tempat muncul).
  - Titik kuning = posisi satelit sekarang (jika di atas horizon).
  - Tanda + merah = arah penunjuk mount.
- **Tabel lintasan**: 5 lintasan berikutnya. Baris kuning **👁 terlihat** berarti satelit terkena
  sinar matahari sementara langit di tempatmu sudah gelap, jadi **bisa dilihat dengan mata**.
  Ini waktu terbaik untuk menguji alat dengan ISS.

## Satelit bawaan

| Kategori | Satelit (NORAD ID) |
|---|---|
| Terlihat mata | ISS (25544), Tiangong (48274), Hubble (20580) |
| Cuaca | Meteor-M2 3 (57166), Meteor-M2 4 (59051), NOAA 15/18/19 (25338, 28654, 33591) |
| Radio amatir | SO-50 (27607) |
| Geostasioner (diam) | Himawari-9 (41836), Merah Putih/Telkom-4 (43587) |

Satelit lain bisa dicari NORAD ID-nya di celestrak.org atau n2yo.com. Satelit geostasioner bagus
untuk menguji kalibrasi: posisinya tetap, jadi bisa dicek dengan aplikasi satelit di HP.

## Perintah Serial (115200 baud)

| Perintah | Fungsi |
|---|---|
| `T` | mode TRACK |
| `G <az> <el>` | arahkan ke azimuth/elevasi, misalnya `G 90 0` |
| `S` | berhenti |
| `N` / `D` | posisi sekarang = utara / datar |
| `P` | status: waktu, lokasi, posisi satelit, posisi mount |
| `L` | daftar lintasan berikutnya (jam UTC; WIB = UTC+7) |
| `U` | unduh ulang TLE |
| `I <norad>` | ganti satelit, misalnya `I 48274` |
| `Z <unix>` | atur waktu UTC manual (jika tidak ada GPS dan internet) |
| tempel 2 baris TLE | baris `1 ...` lalu `2 ...` dari celestrak.org, tanpa internet di ESP32 |

## Pengaturan ([src/config.h](src/config.h))

| Setelan | Default | Keterangan |
|---|---|---|
| `AP_SSID` / `AP_PASS` | SatTracker / satelit123 | WiFi yang dipancarkan ESP32 |
| `STA_SSID` / `STA_PASS` | kosong | WiFi internet (bisa juga diisi dari web) |
| `DEFAULT_LAT/LON/ALT` | Jakarta | Lokasi jika GPS belum fix (bisa diubah dari web) |
| `PAN_MIN/MAX_DEG` | ±270° | Batas putaran pan |
| `TRACK_KP` | 2.0 | Kekuatan koreksi posisi saat melacak |
| `PASS_PREPARE_SEC` | 120 | Detik sebelum satelit muncul, posisi pan disiapkan untuk seluruh lintasan |
| `UNWIND_MARGIN_DEG` | 20° | Putar balik 360° jika pan sedekat ini ke batas (hanya saat di bawah horizon) |
| `TLE_MAX_AGE_HOURS` | 48 | TLE diunduh ulang jika lebih tua dari ini |

## Keterbatasan

- **Lintasan tepat di atas kepala** (elevasi > ~80°): azimuth berubah sangat cepat di dekat zenith,
  melebihi kecepatan maksimum 28BYJ-48 (±60°/s). Penunjuk tertinggal beberapa detik lalu menyusul.
- **Backlash gearbox** 28BYJ-48 sekitar 1–3°: cukup untuk penunjuk visual, kurang untuk antena
  yang sangat terarah.
- **Motor hampir selalu bergerak pelan** karena terus mengikuti satelit, jadi 28BYJ-48 akan terasa
  hangat. Ini normal. Posisi mount disimpan tiap 60 detik agar kalibrasi tidak hilang saat listrik mati.
- **Akurasi TLE turun** jika tidak diperbarui lebih dari 1–2 minggu (terutama untuk ISS).
- Satelit NOAA sudah tidak beroperasi penuh, tapi posisinya tetap bisa dilacak.

## Troubleshooting

| Masalah | Solusi |
|---|---|
| Web: `GPS TIDAK ADA DATA` | Cek TX GPS → GPIO 16, RX GPS → GPIO 17, VCC dan GND |
| Web: `GPS mencari sinyal...` | Normal di dalam ruangan. Bawa ke luar, tunggu 1–5 menit (pertama kali bisa lebih lama) |
| `Gagal unduh TLE` | Cek nama/password WiFi internet; pastikan WiFi 2.4 GHz (ESP32 tidak mendukung 5 GHz) |
| Penunjuk ke arah terbalik | Centang **Balik arah pan/tilt**, lalu kalibrasi ulang |
| Arah meleset beberapa derajat | Kalibrasi ulang di permukaan datar, jauh dari benda logam (kompas HP terganggu oleh logam dan motor) |
| Tabel lintasan kosong | Belum ada waktu yang valid (tunggu GPS atau sambungkan internet) atau TLE belum ada |
