#pragma once

#include <Arduino.h>
#include "config.h"

enum class FsmState : uint8_t { Idle = 0, Keypad, Run, Stopped, Error, Settings };

enum class FsmError : uint8_t {
  None = 0,
  Reverse,
  NoEncoder,
  NoTarget,
  SpeedJump,
  ChannelFault,
  BrakeIneffective,
};

enum class FsmEvent : uint8_t {
  Start = 0,
  Stop,
  Reset,
  KbOpen,
  KbDigit,
  KbDel,
  KbOk,
  KbCancel,
  TargetDone,
  ReverseDetect,
  EncLoss,
  SpeedJumpDetect,
  ChannelFaultDetect,
  BrakeIneffectiveDetect,
  ErrAck,
  SettingsOpen,
  SettingsBack,
};

struct FsmEventData {
  FsmEvent type;
  uint8_t digit;
};

struct PlantData {
  uint32_t targetM;      // ЗАДАНО, целые метры
  uint32_t travelM;      // пройдено, ×0.01 м (125 = 1.25 м)
  uint16_t speedCms;     // ×0.01 м/с
  uint16_t rpm;
  uint16_t progressPct;  // 0..100
  uint32_t kbBuf;
  bool kbFresh;
  uint32_t brakeM;         // расстояние торможения, м
  uint16_t brakeOnMs;      // длительность ШИМ «1» (тормоз)
  uint16_t brakeOffMs;     // длительность ШИМ «0» (отпуск)
  uint8_t encInvert;       // 0=норма, 1=инверсия направления энкодера
};

void fsmBegin();
FsmState fsmState();
const PlantData& fsmPlant();
void fsmDispatch(const FsmEventData& ev);
void fsmMotionTick(uint32_t nowMs);
void fsmPushTarget();
void fsmPushPlant();
void fsmOnDwinVp(uint16_t vp, uint32_t value);
void fsmPollButtons(uint32_t nowMs);
uint16_t fsmLedPeriodMs();
bool fsmSpeedLive();
