// EmberCast MCU sensor station (STM32U585 on the Arduino UNO Q).
// Simulation only. Not for operational use.
//
// Uses only the Arduino_RouterBridge calls shown in the library README:
// Bridge.begin(), Bridge.provide_safe(), Bridge.notify().
// NOT verified on hardware. Do not flash without user approval (CLAUDE.md).
//
// - Emulates a weather station with a deterministic PRNG (xorshift32),
//   one frame every 250 ms, sent with Bridge.notify("ec_sensor", ...).
// - Optional knob: a potentiometer on A0 (3.3 V MCU domain ONLY; never the
//   1.8 V MPU pins) sets the wind direction override. Set USE_KNOB to 1.
// - Provides "ec_status" (state, top_risk) and "ec_ping" (echo, for Bridge
//   latency measurement).
// - Watchdog: no ec_status for WATCHDOG_MS -> FAULT.
// - Display: LED_BUILTIN only (the LED matrix API is not verified):
//   OK = slow blink, STALE/DEGRADED = fast blink, FAULT = on.

#include <Arduino_RouterBridge.h>

#define USE_KNOB 0
static const unsigned long PERIOD_MS = 250;
static const unsigned long WATCHDOG_MS = 3000;

static const int ST_OK = 0, ST_STALE = 1, ST_DEGRADED = 2, ST_FAULT = 3;

static volatile int g_state = ST_FAULT;
static volatile int g_top_risk = 255;
static volatile unsigned long g_last_status_ms = 0;
static unsigned long g_seq = 0;
static uint32_t g_rng = 0x12345678u;

static uint32_t xorshift32() {
  uint32_t x = g_rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  g_rng = x;
  return x;
}
static float u01() { return (xorshift32() >> 8) * (1.0f / 16777216.0f); }

void ec_status(int state, int top_risk) {
  g_state = state;
  g_top_risk = top_risk;
  g_last_status_ms = millis();
}

int ec_ping(int x) { return x; }

static float g_ws = 6.0f, g_wd = 0.0f, g_h = 0.3f, g_t = 25.0f;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  analogReadResolution(10);  // the knob scale below assumes 10 bits (HARDWARE.md Q9)
  Bridge.begin();
  Bridge.provide_safe("ec_status", ec_status);
  Bridge.provide_safe("ec_ping", ec_ping);
}

void loop() {
  static unsigned long next_ms = 0;
  const unsigned long now = millis();
  if ((long)(now - next_ms) >= 0) {
    next_ms = now + PERIOD_MS;
    // Bounded random walk (deterministic for a fixed start value).
    g_ws += (u01() - 0.5f) * 0.4f;
    if (g_ws < 0.5f) g_ws = 0.5f;
    if (g_ws > 18.0f) g_ws = 18.0f;
    g_wd += (u01() - 0.5f) * 0.05f;
    g_h += (u01() - 0.5f) * 0.01f;
    if (g_h < 0.05f) g_h = 0.05f;
    if (g_h > 0.9f) g_h = 0.9f;
    g_t += (u01() - 0.5f) * 0.1f;
    uint32_t flags = 0;
    float wd = g_wd;
#if USE_KNOB
    wd = analogRead(A0) * (6.2831853f / 1023.0f);  // 10-bit ADC, set in setup()
    flags |= 0x80000000u;                            // bit 31: knob override frame
#endif
    Bridge.notify("ec_sensor", (int)g_seq, g_ws, wd, g_h, g_t, (int)flags);
    ++g_seq;
  }
  // Watchdog: Linux silent -> FAULT.
  int st = g_state;
  if (millis() - g_last_status_ms > WATCHDOG_MS) st = ST_FAULT;
  bool led;
  if (st == ST_FAULT) led = true;
  else if (st == ST_OK) led = (now / 500) % 2;
  else led = (now / 100) % 2;
  digitalWrite(LED_BUILTIN, led ? HIGH : LOW);
}
