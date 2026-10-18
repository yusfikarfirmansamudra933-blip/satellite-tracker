#pragma once
// Driver 2 motor 28BYJ-48 lewat 2x ULN2003 (langsung ke pin ESP32, atau lewat 74HC595).
// Motor digerakkan dengan kecepatan (derajat/detik) oleh timer 2000 Hz,
// dengan percepatan bertahap dan batas sudut.

#include <Arduino.h>
#include "esp_timer.h"
#include "config.h"

namespace motors {

const float STEPS_PER_DEG = STEPS_PER_REV / 360.0f;
const float TICK_S = TICK_US * 1e-6f;

// Urutan koil untuk IN1..IN4 (bit0 = IN1)
#if WAVE_DRIVE
const uint8_t SEQ[4] = {0b0001, 0b0010, 0b0100, 0b1000};  // 1 koil aktif
const int SEQ_MASK = 3;
#else
const uint8_t SEQ[8] = {0b0001, 0b0011, 0b0010, 0b0110, 0b0100, 0b1100, 0b1000, 0b1001};  // half-step
const int SEQ_MASK = 7;
#endif

struct Axis {
  volatile int32_t pos;   // posisi, langkah
  volatile float target;  // kecepatan yang diminta, langkah/detik
  float speed;            // kecepatan saat ini
  float frac;             // sisa langkah pecahan
  int32_t minStep, maxStep;
};

Axis axes[2];  // 0 = pan, 1 = tilt
volatile bool setPosPending[2] = {false, false};
volatile int32_t setPosValue[2] = {0, 0};
volatile bool releaseRequest = true;  // koil mati sampai ada perintah gerak pertama
volatile bool inverted[2] = {false, false};  // balik arah putar tanpa mengubah koordinat sudut
uint32_t lastMoveMs = 0;
uint8_t lastBits = 0xFF;
esp_timer_handle_t timer;

#if !USE_74HC595
const uint8_t PAN_COILS[4] = PAN_PINS;
const uint8_t TILT_COILS[4] = TILT_PINS;
// bit 0..3 = IN1..IN4 PAN, bit 4..7 = IN1..IN4 TILT
uint8_t coilPin(int bit) { return bit < 4 ? PAN_COILS[bit] : TILT_COILS[bit - 4]; }
#endif

void writeBits(uint8_t bits) {
  if (bits == lastBits) return;
#if USE_74HC595
  digitalWrite(SR_LATCH, LOW);
  shiftOut(SR_DATA, SR_CLOCK, MSBFIRST, bits);
  digitalWrite(SR_LATCH, HIGH);
#else
  uint8_t changed = bits ^ lastBits;
  for (int i = 0; i < 8; i++)
    if (changed & (1 << i)) digitalWrite(coilPin(i), (bits >> i) & 1);
#endif
  lastBits = bits;
}

void tick(void *) {
  for (int i = 0; i < 2; i++) {
    if (setPosPending[i]) {
      axes[i].pos = setPosValue[i];
      axes[i].frac = 0;
      setPosPending[i] = false;
    }
  }

  // Perintah gerak baru membatalkan release; selama release motor langsung berhenti.
  if (releaseRequest && (axes[0].target != 0 || axes[1].target != 0)) releaseRequest = false;
  if (releaseRequest) {
    for (Axis &a : axes) a.speed = 0;
  }

  const float maxDelta = ACCEL_DPS2 * STEPS_PER_DEG * TICK_S;
  bool moving = false;
  for (Axis &a : axes) {
    a.speed += constrain(a.target - a.speed, -maxDelta, maxDelta);
    if ((a.speed > 0 && a.pos >= a.maxStep) || (a.speed < 0 && a.pos <= a.minStep)) a.speed = 0;

    a.frac += a.speed * TICK_S;  // maksimal ~0.35 langkah per tick
    if (a.frac >= 1.0f)       { a.pos = a.pos + 1; a.frac -= 1.0f; }
    else if (a.frac <= -1.0f) { a.pos = a.pos - 1; a.frac += 1.0f; }
    if (a.speed != 0 || a.target != 0) moving = true;
  }

  uint32_t now = millis();
  if (moving) lastMoveMs = now;
  bool hold = !releaseRequest && (moving || RELEASE_AFTER_MS == 0 || now - lastMoveMs < RELEASE_AFTER_MS);
  uint8_t bits = 0;
  if (hold) {
    int32_t p0 = inverted[0] ? -axes[0].pos : axes[0].pos;
    int32_t p1 = inverted[1] ? -axes[1].pos : axes[1].pos;
    bits = SEQ[p0 & SEQ_MASK] | (SEQ[p1 & SEQ_MASK] << 4);
  }
  writeBits(bits);
}

void begin() {
#if USE_74HC595
  pinMode(SR_DATA, OUTPUT);
  pinMode(SR_CLOCK, OUTPUT);
  pinMode(SR_LATCH, OUTPUT);
#else
  for (int i = 0; i < 8; i++) {
    uint8_t p = coilPin(i);
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
#endif
  writeBits(0);

  axes[0].minStep = lroundf(PAN_MIN_DEG * STEPS_PER_DEG);
  axes[0].maxStep = lroundf(PAN_MAX_DEG * STEPS_PER_DEG);
  axes[1].minStep = lroundf(TILT_MIN_DEG * STEPS_PER_DEG);
  axes[1].maxStep = lroundf(TILT_MAX_DEG * STEPS_PER_DEG);

  esp_timer_create_args_t args = {};
  args.callback = tick;
  args.name = "motors";
  esp_timer_create(&args, &timer);
  esp_timer_start_periodic(timer, TICK_US);
}

// Kecepatan dalam derajat/detik (positif = azimuth/elevasi bertambah).
void setVelocity(float panDps, float tiltDps) {
  axes[0].target = constrain(panDps, -MAX_SPEED_DPS, MAX_SPEED_DPS) * STEPS_PER_DEG;
  axes[1].target = constrain(tiltDps, -MAX_SPEED_DPS, MAX_SPEED_DPS) * STEPS_PER_DEG;
}

float panDeg()  { return axes[0].pos / STEPS_PER_DEG; }
float tiltDeg() { return axes[1].pos / STEPS_PER_DEG; }

// Tetapkan posisi sekarang sebagai sudut tertentu (misalnya 0 = utara / horizon)
void setPosition(int axis, float deg) {
  setPosValue[axis] = lroundf(deg * STEPS_PER_DEG);
  setPosPending[axis] = true;
}

void setDirection(bool invPan, bool invTilt) {
  inverted[0] = invPan;
  inverted[1] = invTilt;
}

void release() {
  setVelocity(0, 0);
  releaseRequest = true;
}

}  // namespace motors
