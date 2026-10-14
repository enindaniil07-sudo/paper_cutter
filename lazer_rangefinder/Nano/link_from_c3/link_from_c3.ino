/*
  Nano принимает миллиметры, которые C3 выводит на TX.

  Nano в USB компьютера.
  C3 питается от Nano, свой USB-кабель не нужен.
    Nano 5V  -> C3 5V
    Nano GND -> C3 GND
    C3   TX  -> Nano D8
  D9 не подключать: на нём 5 В, для входа C3 это много.

  S3 со датчиком — отдельным кабелем USB.
  По радио S3 шлёт замер на C3, C3 печатает число на TX, 115200.
  Nano повторяет его в USB: "345 мм", "нет замера" или "пакетов нет".
*/

#include <AltSoftSerial.h>

static constexpr uint32_t kBaud = 115200;
static constexpr uint32_t kSilenceMs = 1000;

AltSoftSerial link;

char line[16];
uint8_t filled = 0;
uint32_t lastByteMs = 0;
bool silenceNoted = false;

void show(bool on) {
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
}

void takeLine() {
  line[filled] = 0;
  bool digits = filled > 0;
  for (uint8_t i = 0; i < filled; i++) {
    if (line[i] < '0' || line[i] > '9') digits = false;
  }
  if (digits) {
    silenceNoted = false;
    show(true);
    Serial.print(line);
    Serial.println(F(" мм"));
  } else if (strcmp(line, "NA") == 0) {
    silenceNoted = false;
    show(false);
    Serial.println(F("нет замера"));
  }
  filled = 0;
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  show(false);
  Serial.begin(kBaud);
  link.begin(kBaud);
  Serial.println(F("Nano слушает C3 на D8"));
}

void loop() {
  while (link.available() > 0) {
    char c = (char)link.read();
    lastByteMs = millis();
    if (c == '\r') continue;
    if (c == '\n') {
      takeLine();
      continue;
    }
    if (filled < sizeof(line) - 1) line[filled++] = c;
    else filled = 0;
  }

  if (lastByteMs != 0 && millis() - lastByteMs >= kSilenceMs && !silenceNoted) {
    silenceNoted = true;
    filled = 0;
    show(false);
    Serial.println(F("пакетов нет"));
  }
}
