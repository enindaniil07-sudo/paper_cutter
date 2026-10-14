/*
  Black Pill F411CE — blink PC13 + text on the *UART* that goes to the USB-serial chip
  (CH340/CP2102), NOT native USB CDC on PA11/PA12.

  Use this sketch if Device Manager never shows a "USB Serial Device" / VCP for the MCU,
  but you *do* get a COM port when the board is plugged in (bridge chip).

  Build FQBN: GenF4 + BLACKPILL_F411CE + usb=none (see build_upload.ps1 -Usb uart).
*/

// USART1: RX = PA10, TX = PA9 on typical WeAct-style F411 Black Pills
HardwareSerial LogSerial(PA10, PA9);

void setup() {
  pinMode(PC13, OUTPUT);
  LogSerial.begin(115200);
  delay(100);
  LogSerial.println(F("BlackPill F411CE UART (PA9/PA10) — hello"));
}

void loop() {
  digitalWrite(PC13, LOW);
  delay(400);
  digitalWrite(PC13, HIGH);
  delay(400);
  LogSerial.println(millis());
}
