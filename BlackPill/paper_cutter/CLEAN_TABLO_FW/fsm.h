#pragma once

#include <Arduino.h>
#include "config.h"

enum class FsmState : uint8_t { Idle = 0, Keypad, Run, Stopped, ReverseError };

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
  ErrAck,
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
