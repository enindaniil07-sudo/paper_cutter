/*
  Black Pill F411CE — PC13 blink frequency set over USB serial (CDC, 115200).

  Commands (line ends with \\n or \\r\\n, ASCII):
    HZ <number>   set blink frequency in Hz (e.g. HZ 2.5)
    ?             print current Hz

  Replies: OK / ERR lines. LED is active-low on PC13.
*/

#include <stdlib.h>

static constexpr uint8_t LED_PIN = PC13;

static float g_hz = 1.0f;
static uint32_t g_halfMs = 500;

static void recomputeHalfMs() {
  if (g_hz < 0.05f) {
    g_hz = 0.05f;
  }
  if (g_hz > 40.0f) {
    g_hz = 40.0f;
  }
  float periodMs = 1000.0f / g_hz;
  uint32_t full = (uint32_t)(periodMs + 0.5f);
  if (full < 2U) {
    full = 2U;
  }
  g_halfMs = full / 2U;
  if (g_halfMs < 1U) {
    g_halfMs = 1U;
  }
}

static char s_line[48];
static uint8_t s_lineLen = 0;

static void parseLine(char *line) {
  while (*line == ' ' || *line == '\t') {
    line++;
  }
  if (*line == 0) {
    return;
  }

  if (*line == '?') {
    Serial.print(F("HZ "));
    Serial.println(g_hz, 2);
    return;
  }

  char c0 = (char)(line[0] | 32);
  char c1 = (char)(line[1] | 32);
  if (c0 == 'h' && c1 == 'z') {
    char *p = line + 2;
    while (*p && ((*p < '0' || *p > '9') && *p != '.' && *p != '-' && *p != '+')) {
      p++;
    }
    float v = (float)atof(p);
    if (v > 0.0f) {
      g_hz = v;
      recomputeHalfMs();
      Serial.print(F("OK "));
      Serial.print(g_hz, 2);
      Serial.println(F(" Hz"));
    } else {
      Serial.println(F("ERR bad Hz"));
    }
    return;
  }

  Serial.println(F("ERR use: HZ <Hz> or ?"));
}

static void pollSerial() {
  while (Serial.available() > 0) {
    int r = Serial.read();
    if (r < 0) {
      break;
    }
    char c = (char)r;
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (s_lineLen >= sizeof(s_line)) {
        s_lineLen = (uint8_t)(sizeof(s_line) - 1U);
      }
      s_line[s_lineLen] = 0;
      s_lineLen = 0;
      parseLine(s_line);
      continue;
    }
    if (s_lineLen < sizeof(s_line) - 1U) {
      s_line[s_lineLen++] = c;
    } else {
      s_lineLen = 0;
      Serial.println(F("ERR line too long"));
    }
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 2500) {
  }
  recomputeHalfMs();
  Serial.println(F("READY HZ <float> | ?"));
  Serial.flush();
}

void loop() {
  pollSerial();

  static uint32_t tFlip = 0;
  static bool ledOn = false;
  uint32_t now = millis();
  if ((uint32_t)(now - tFlip) >= g_halfMs) {
    tFlip = now;
    ledOn = !ledOn;
    digitalWrite(LED_PIN, ledOn ? LOW : HIGH);
  }
}
