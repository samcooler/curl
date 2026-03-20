#pragma once
#include <Arduino.h>

// Forward declarations — implemented in main.cpp
extern void setSolenoid(int index, bool state);
extern void setModulator(bool state);
extern const int NUM_SOLENOIDS;
extern bool solenoidStates[];

// --- Program parameter and struct definitions ---

#define MAX_PARAMS 4

struct ProgramParam {
  const char* name;
  float value;
  float defaultValue;
  float min;
  float max;
  float step;
};

struct Program {
  const char* name;
  void (*init)();
  void (*update)(unsigned long now);
  void (*stop)();
  ProgramParam params[MAX_PARAMS];
  int numParams;
};

// Forward-declare the array so program code can read params
extern Program programs[];
extern const int NUM_PROGRAMS;

// ============================================================
// Program 1: Random Fire
// ============================================================
namespace RandomFire {
  unsigned long nextToggle[12];
  bool channelOn[12];
  unsigned long modNextToggle;
  bool modOn;

  void init() {
    for (int i = 0; i < 12; i++) {
      channelOn[i] = false;
      nextToggle[i] = millis() + random(100, 500);
    }
    modNextToggle = millis() + random(200, 800);
    modOn = false;
  }

  void update(unsigned long now) {
    float density = programs[0].params[0].value;
    float minInt  = programs[0].params[1].value;
    float maxInt  = programs[0].params[2].value;
    float modPct  = programs[0].params[3].value;

    for (int i = 0; i < 12; i++) {
      if (now >= nextToggle[i]) {
        if (channelOn[i]) {
          setSolenoid(i, false);
          channelOn[i] = false;
          nextToggle[i] = now + random((int)minInt, (int)maxInt);
        } else {
          if (random(100) < (int)density) {
            setSolenoid(i, true);
            channelOn[i] = true;
            int onMax = max((int)minInt + 1, (int)(maxInt / 2));
            nextToggle[i] = now + random((int)minInt, onMax);
          } else {
            nextToggle[i] = now + random((int)minInt, (int)maxInt);
          }
        }
      }
    }

    // Modulator on its own timer, same speed range as channels
    if (now >= modNextToggle) {
      if (modOn) {
        setModulator(false);
        modOn = false;
        modNextToggle = now + random((int)minInt, (int)maxInt);
      } else {
        if (random(100) < (int)modPct) {
          setModulator(true);
          modOn = true;
          int onMax = max((int)minInt + 1, (int)(maxInt / 2));
          modNextToggle = now + random((int)minInt, onMax);
        } else {
          modNextToggle = now + random((int)minInt, (int)maxInt);
        }
      }
    }
  }

  void stop() {
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
    setModulator(false);
  }
}

// ============================================================
// Program 2: Chase
// ============================================================
namespace Chase {
  int headPos;
  int direction; // 1 or -1
  unsigned long lastStep;

  void init() {
    headPos = 0;
    direction = 1;
    lastStep = millis();
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
  }

  void update(unsigned long now) {
    float speed  = programs[1].params[0].value;
    int width    = (int)programs[1].params[1].value;
    int dirMode  = (int)programs[1].params[2].value;
    bool mod     = programs[1].params[3].value > 0.5;

    if (now - lastStep >= (unsigned long)speed) {
      lastStep = now;

      // Advance head
      headPos += direction;
      if (dirMode == 0) { // left to right
        direction = 1;
        if (headPos >= 12) headPos = 0;
      } else if (dirMode == 1) { // right to left
        direction = -1;
        if (headPos < 0) headPos = 11;
      } else { // bounce
        if (headPos >= 12) { headPos = 10; direction = -1; }
        if (headPos < 0) { headPos = 1; direction = 1; }
      }

      // Set channels
      for (int i = 0; i < 12; i++) {
        bool on = false;
        for (int w = 0; w < width; w++) {
          int pos = headPos - w;
          if (pos < 0) pos += 12;
          if (i == pos) { on = true; break; }
        }
        setSolenoid(i, on);
      }
      setModulator(mod);
    }
  }

  void stop() {
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
    setModulator(false);
  }
}

// ============================================================
// Program 3: Wave
// ============================================================
namespace Wave {
  unsigned long startTime;

  void init() {
    startTime = millis();
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
  }

  void update(unsigned long now) {
    float speed = programs[2].params[0].value;
    float width = programs[2].params[1].value;
    bool mod    = programs[2].params[2].value > 0.5;

    float phase = (float)(now - startTime) / speed * TWO_PI;

    for (int i = 0; i < 12; i++) {
      float channelPhase = phase - (float)i / width * TWO_PI;
      bool on = sin(channelPhase) > 0.0;
      setSolenoid(i, on);
    }
    setModulator(mod);
  }

  void stop() {
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
    setModulator(false);
  }
}

// ============================================================
// Program 4: All Pulse
// ============================================================
namespace AllPulse {
  unsigned long lastToggle;
  bool isOn;

  void init() {
    lastToggle = millis();
    isOn = false;
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
  }

  void update(unsigned long now) {
    float onTime  = programs[3].params[0].value;
    float offTime = programs[3].params[1].value;
    bool modPulse = programs[3].params[2].value > 0.5;

    unsigned long interval = isOn ? (unsigned long)onTime : (unsigned long)offTime;
    if (now - lastToggle >= interval) {
      lastToggle = now;
      isOn = !isOn;
      for (int i = 0; i < 12; i++) setSolenoid(i, isOn);
      setModulator(isOn && modPulse);
    }
  }

  void stop() {
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
    setModulator(false);
  }
}

// ============================================================
// Program 5: Pairs
// ============================================================
namespace Pairs {
  int currentPair;
  unsigned long lastStep;

  void init() {
    currentPair = 0;
    lastStep = millis();
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
  }

  void update(unsigned long now) {
    float speed = programs[4].params[0].value;
    int mode    = (int)programs[4].params[1].value;
    bool mod    = programs[4].params[2].value > 0.5;

    if (now - lastStep >= (unsigned long)speed) {
      lastStep = now;

      // Turn off all
      for (int i = 0; i < 12; i++) setSolenoid(i, false);

      int pair;
      if (mode == 0) { // inward
        pair = currentPair;
      } else if (mode == 1) { // outward
        pair = 5 - currentPair;
      } else { // alternate
        pair = currentPair;
      }

      // Fire the pair: pair 0 = channels 0+11, pair 1 = 1+10, etc.
      if (pair >= 0 && pair < 6) {
        setSolenoid(pair, true);
        setSolenoid(11 - pair, true);
      }

      currentPair++;
      if (currentPair >= 6) currentPair = 0;
      setModulator(mod);
    }
  }

  void stop() {
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
    setModulator(false);
  }
}

// ============================================================
// Program 6: Burst
// ============================================================
namespace Burst {
  int burstIndex;
  bool inPause;
  unsigned long lastStep;

  void init() {
    burstIndex = 0;
    inPause = false;
    lastStep = millis();
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
  }

  void update(unsigned long now) {
    float fireRate   = programs[5].params[0].value;
    float pauseTime  = programs[5].params[1].value;
    int numChannels  = (int)programs[5].params[2].value;
    bool mod         = programs[5].params[3].value > 0.5;

    if (inPause) {
      if (now - lastStep >= (unsigned long)pauseTime) {
        inPause = false;
        burstIndex = 0;
        lastStep = now;
      }
    } else {
      if (now - lastStep >= (unsigned long)fireRate) {
        lastStep = now;

        // Turn off previous
        if (burstIndex > 0) setSolenoid(burstIndex - 1, false);

        if (burstIndex < numChannels && burstIndex < 12) {
          setSolenoid(burstIndex, true);
          burstIndex++;
        } else {
          // End of burst — all off, enter pause
          for (int i = 0; i < 12; i++) setSolenoid(i, false);
          inPause = true;
          lastStep = now;
        }
      }
    }
    setModulator(mod && !inPause);
  }

  void stop() {
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
    setModulator(false);
  }
}

// ============================================================
// Program 7: Rainfall
// ============================================================
namespace Rainfall {
  unsigned long dropEnd[12];

  void init() {
    for (int i = 0; i < 12; i++) {
      dropEnd[i] = 0;
      setSolenoid(i, false);
    }
  }

  void update(unsigned long now) {
    float density  = programs[6].params[0].value;
    float duration = programs[6].params[1].value;
    bool mod       = programs[6].params[2].value > 0.5;

    for (int i = 0; i < 12; i++) {
      if (now >= dropEnd[i]) {
        // Drop finished or not active
        if (solenoidStates[i]) {
          setSolenoid(i, false);
        }
        // Chance of new drop — check every ~20ms worth of calls
        if (random(1000) < (int)(density * 10.0 / 12.0)) {
          setSolenoid(i, true);
          dropEnd[i] = now + (unsigned long)duration;
        }
      }
    }
    setModulator(mod);
  }

  void stop() {
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
    setModulator(false);
  }
}

// ============================================================
// Programs array
// ============================================================

Program programs[] = {
  {
    "Random Fire",
    RandomFire::init, RandomFire::update, RandomFire::stop,
    {
      {"Density %",      30, 30, 0, 100, 5},
      {"Min Interval",  300, 300, 250, 2000, 50},
      {"Max Interval", 1000, 1000, 500, 5000, 100},
      {"Modulator %",    50, 50, 0, 100, 5},
    },
    4
  },
  {
    "Chase",
    Chase::init, Chase::update, Chase::stop,
    {
      {"Speed ms",   250, 250, 250, 2000, 25},
      {"Width",        2, 2, 1, 6, 1},
      {"Direction",    0, 0, 0, 2, 1},
      {"Modulator",    1, 1, 0, 1, 1},
    },
    4
  },
  {
    "Wave",
    Wave::init, Wave::update, Wave::stop,
    {
      {"Speed ms", 1000, 1000, 250, 5000, 100},
      {"Width",       6, 6, 1, 12, 1},
      {"Modulator",   1, 1, 0, 1, 1},
      {"", 0, 0, 0, 0, 0},
    },
    3
  },
  {
    "All Pulse",
    AllPulse::init, AllPulse::update, AllPulse::stop,
    {
      {"On Time ms",  500, 500, 250, 5000, 50},
      {"Off Time ms", 500, 500, 250, 5000, 50},
      {"Mod Pulse",     1, 1, 0, 1, 1},
      {"", 0, 0, 0, 0, 0},
    },
    3
  },
  {
    "Pairs",
    Pairs::init, Pairs::update, Pairs::stop,
    {
      {"Speed ms", 300, 300, 250, 3000, 50},
      {"Mode",       0, 0, 0, 2, 1},
      {"Modulator",  1, 1, 0, 1, 1},
      {"", 0, 0, 0, 0, 0},
    },
    3
  },
  {
    "Burst",
    Burst::init, Burst::update, Burst::stop,
    {
      {"Fire Rate ms", 250, 250, 250, 500, 10},
      {"Pause ms",    3000, 3000, 500, 10000, 250},
      {"Channels",      12, 12, 1, 12, 1},
      {"Modulator",      1, 1, 0, 1, 1},
    },
    4
  },
  {
    "Rainfall",
    Rainfall::init, Rainfall::update, Rainfall::stop,
    {
      {"Density %",      15, 15, 1, 100, 5},
      {"Drop ms",       300, 300, 250, 1000, 25},
      {"Modulator",       0, 0, 0, 1, 1},
      {"", 0, 0, 0, 0, 0},
    },
    3
  },
};

const int NUM_PROGRAMS = sizeof(programs) / sizeof(programs[0]);
