#pragma once
#include <Arduino.h>

// Forward declarations — implemented in main.cpp
extern void setSolenoid(int index, bool state);
extern void setModulator(bool state);
extern const int NUM_SOLENOIDS;
extern bool solenoidStates[];

// --- Program parameter and struct definitions ---

#define MAX_PARAMS 6

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

// Helper: sinusoidal modulation factor 0..1, period in seconds. Returns 1.0 if period<=0.
inline float sineFactor(unsigned long now, unsigned long startTime, float periodSec) {
  if (periodSec <= 0) return 1.0f;
  float t = (float)(now - startTime) / 1000.0f;
  return 0.5f + 0.5f * sin(TWO_PI * t / periodSec);
}

// ============================================================
// Program 1: Random Fire
// ============================================================
namespace RandomFire {
  unsigned long nextToggle[12];
  bool channelOn[12];
  unsigned long modNextToggle;
  bool modOn;
  unsigned long startTime;

  void init() {
    startTime = millis();
    for (int i = 0; i < 12; i++) {
      channelOn[i] = false;
      nextToggle[i] = millis() + random(250, 500);
    }
    modNextToggle = millis() + random(250, 800);
    modOn = false;
  }

  void update(unsigned long now) {
    float baseDensity = programs[0].params[0].value;
    float minInt      = programs[0].params[1].value;
    float maxInt      = programs[0].params[2].value;
    float modPct      = programs[0].params[3].value;
    float cycleSec    = programs[0].params[4].value;

    // Sinusoidal density variation
    float density = baseDensity * sineFactor(now, startTime, cycleSec);

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

    // Modulator on its own timer
    if (now >= modNextToggle) {
      float modDensity = modPct * sineFactor(now, startTime, cycleSec);
      if (modOn) {
        setModulator(false);
        modOn = false;
        modNextToggle = now + random((int)minInt, (int)maxInt);
      } else {
        if (random(100) < (int)modDensity) {
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
// Program 2: Chase (modulator fires on alternating half-steps)
// ============================================================
namespace Chase {
  int headPos;
  int direction;
  unsigned long lastStep;
  bool modSubStep; // false=normal, true=modulator boost

  void init() {
    headPos = 0;
    direction = 1;
    lastStep = millis();
    modSubStep = false;
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
    setModulator(false);
  }

  void update(unsigned long now) {
    float speed  = programs[1].params[0].value;
    int width    = (int)programs[1].params[1].value;
    int dirMode  = (int)programs[1].params[2].value;
    bool modEnabled = programs[1].params[3].value > 0.5;

    if (now - lastStep >= (unsigned long)speed) {
      lastStep = now;

      if (modSubStep) {
        // Second half-step: same position, modulator ON
        setModulator(modEnabled);
        modSubStep = false;
      } else {
        // First half-step: advance position, modulator OFF
        setModulator(false);

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

        for (int i = 0; i < 12; i++) {
          bool on = false;
          for (int w = 0; w < width; w++) {
            int pos = headPos - w;
            if (pos < 0) pos += 12;
            if (i == pos) { on = true; break; }
          }
          setSolenoid(i, on);
        }
        modSubStep = true;
      }
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
// Program 4: All Pulse (separate random clocks for channels & modulator)
// ============================================================
namespace AllPulse {
  unsigned long chanNextToggle;
  bool chanOn;
  unsigned long modNextToggle;
  bool modOn;

  void init() {
    unsigned long now = millis();
    chanNextToggle = now + 250;
    chanOn = false;
    modNextToggle = now + 250;
    modOn = false;
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
    setModulator(false);
  }

  void update(unsigned long now) {
    float chanOnMs  = programs[3].params[0].value;
    float chanOffMs = programs[3].params[1].value;
    float modOnMs   = programs[3].params[2].value;
    float modOffMs  = programs[3].params[3].value;

    // Channel clock with ±20% randomness
    if (now >= chanNextToggle) {
      chanOn = !chanOn;
      for (int i = 0; i < 12; i++) setSolenoid(i, chanOn);
      float base = chanOn ? chanOnMs : chanOffMs;
      int jitter = (int)(base * 0.2);
      if (jitter < 1) jitter = 1;
      chanNextToggle = now + (unsigned long)base + random(-jitter, jitter + 1);
    }

    // Modulator clock with ±20% randomness (independent)
    if (now >= modNextToggle) {
      modOn = !modOn;
      setModulator(modOn);
      float base = modOn ? modOnMs : modOffMs;
      int jitter = (int)(base * 0.2);
      if (jitter < 1) jitter = 1;
      modNextToggle = now + (unsigned long)base + random(-jitter, jitter + 1);
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
      for (int i = 0; i < 12; i++) setSolenoid(i, false);

      int pair;
      if (mode == 0) pair = currentPair;
      else if (mode == 1) pair = 5 - currentPair;
      else pair = currentPair;

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
        if (burstIndex > 0) setSolenoid(burstIndex - 1, false);
        if (burstIndex < numChannels && burstIndex < 12) {
          setSolenoid(burstIndex, true);
          burstIndex++;
        } else {
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
        if (solenoidStates[i]) setSolenoid(i, false);
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
// Program 8: Sparkle — brief random flashes, activity follows sine
// ============================================================
namespace Sparkle {
  unsigned long flashEnd[12];
  unsigned long nextCheck[12];
  unsigned long startTime;

  void init() {
    startTime = millis();
    for (int i = 0; i < 12; i++) {
      flashEnd[i] = 0;
      nextCheck[i] = millis() + random(0, 500);
      setSolenoid(i, false);
    }
    setModulator(false);
  }

  void update(unsigned long now) {
    int maxFlashes = (int)programs[7].params[0].value;
    float flashMs  = programs[7].params[1].value;
    float gapMs    = programs[7].params[2].value;
    float cycleSec = programs[7].params[3].value;
    bool mod       = programs[7].params[4].value > 0.5;

    float sf = sineFactor(now, startTime, cycleSec);
    int targetActive = max(1, (int)(maxFlashes * sf));

    // Count currently active
    int active = 0;
    for (int i = 0; i < 12; i++) {
      if (solenoidStates[i]) active++;
    }

    for (int i = 0; i < 12; i++) {
      // Turn off finished flashes
      if (solenoidStates[i] && now >= flashEnd[i]) {
        setSolenoid(i, false);
        nextCheck[i] = now + (unsigned long)gapMs;
        active--;
      }
      // Start new flashes if under target
      if (!solenoidStates[i] && active < targetActive && now >= nextCheck[i]) {
        if (random(12) < targetActive) {
          setSolenoid(i, true);
          flashEnd[i] = now + (unsigned long)flashMs;
          active++;
        } else {
          nextCheck[i] = now + random(250, (int)gapMs);
        }
      }
    }
    setModulator(mod && active > 0);
  }

  void stop() {
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
    setModulator(false);
  }
}

// ============================================================
// Program 9: Stack — build up channels, hold, drain, sine speed
// ============================================================
namespace Stack {
  int level;       // how many channels are on (0..12)
  bool filling;    // true=adding, false=removing
  bool holding;
  unsigned long lastStep;
  unsigned long holdStart;
  unsigned long startTime;

  void init() {
    startTime = millis();
    level = 0;
    filling = true;
    holding = false;
    lastStep = millis();
    holdStart = 0;
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
    setModulator(false);
  }

  void update(unsigned long now) {
    float stepMs   = programs[8].params[0].value;
    float holdMs   = programs[8].params[1].value;
    int dirMode    = (int)programs[8].params[2].value;
    float cycleSec = programs[8].params[3].value;
    bool mod       = programs[8].params[4].value > 0.5;

    // Sine modulates step speed: faster at peak, slower at trough
    float sf = sineFactor(now, startTime, cycleSec);
    float effectiveStep = stepMs * (0.3 + 1.4 * (1.0 - sf)); // range 0.3x..1.7x

    if (holding) {
      if (now - holdStart >= (unsigned long)holdMs) {
        holding = false;
        filling = !filling;
        lastStep = now;
      }
    } else if (now - lastStep >= (unsigned long)effectiveStep) {
      lastStep = now;

      if (filling) {
        if (level < 12) {
          int ch = (dirMode == 1) ? (11 - level) : level;
          setSolenoid(ch, true);
          level++;
        }
        if (level >= 12) {
          holding = true;
          holdStart = now;
        }
      } else {
        if (level > 0) {
          level--;
          int ch = (dirMode == 1) ? (11 - level) : level;
          setSolenoid(ch, false);
        }
        if (level <= 0) {
          holding = true;
          holdStart = now;
          if (dirMode == 2) filling = true; // alternate always refills
          else filling = true;
        }
      }
    }
    setModulator(mod && level > 6);
  }

  void stop() {
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
    setModulator(false);
  }
}

// ============================================================
// Program 10: Juggle — multiple chase heads at different speeds,
//             overall activity modulated by sine
// ============================================================
namespace Juggle {
  static const int NUM_HEADS = 3;
  int headPos[NUM_HEADS];
  unsigned long headNext[NUM_HEADS];
  unsigned long startTime;

  void init() {
    startTime = millis();
    for (int h = 0; h < NUM_HEADS; h++) {
      headPos[h] = h * 4; // space them out
      headNext[h] = millis();
    }
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
    setModulator(false);
  }

  void update(unsigned long now) {
    float baseSpeed = programs[9].params[0].value;
    float spread    = programs[9].params[1].value;
    float cycleSec  = programs[9].params[2].value;
    bool mod        = programs[9].params[3].value > 0.5;

    float sf = sineFactor(now, startTime, cycleSec);

    // Each head runs at a different speed offset
    bool channelHit[12] = {};
    for (int h = 0; h < NUM_HEADS; h++) {
      // Head speed varies: head 0 at base, head 1 faster, head 2 slower
      float headSpeed = baseSpeed + (h - 1) * spread;
      if (headSpeed < 250) headSpeed = 250;
      // Sine modulates all speeds
      float effSpeed = headSpeed * (0.4 + 1.2 * (1.0 - sf));
      if (effSpeed < 250) effSpeed = 250;

      if (now >= headNext[h]) {
        headNext[h] = now + (unsigned long)effSpeed;
        headPos[h] = (headPos[h] + 1) % 12;
      }
      channelHit[headPos[h]] = true;
    }

    for (int i = 0; i < 12; i++) {
      setSolenoid(i, channelHit[i]);
    }
    setModulator(mod && sf > 0.5);
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
      {"Density % — chance each channel fires",          30, 30, 0, 100, 5},
      {"Min Interval — shortest pause between toggles", 300, 300, 250, 2000, 50},
      {"Max Interval — longest pause between toggles", 1000, 1000, 500, 5000, 100},
      {"Modulator % — chance modulator activates",        50, 50, 0, 100, 5},
      {"Density Cycle s — sine period, 0=off",             0, 0, 0, 60, 1},
      {"", 0, 0, 0, 0, 0},
    },
    5
  },
  {
    "Chase",
    Chase::init, Chase::update, Chase::stop,
    {
      {"Speed ms — time per half-step",            250, 250, 250, 2000, 25},
      {"Width — number of lit channels",              2, 2, 1, 6, 1},
      {"Direction — 0:L>R 1:R>L 2:bounce",           0, 0, 0, 2, 1},
      {"Modulator — boost on alternating steps",      1, 1, 0, 1, 1},
      {"", 0, 0, 0, 0, 0},
      {"", 0, 0, 0, 0, 0},
    },
    4
  },
  {
    "Wave",
    Wave::init, Wave::update, Wave::stop,
    {
      {"Speed ms — full wave cycle period",  1000, 1000, 250, 5000, 100},
      {"Width — channels per wavelength",       6, 6, 1, 12, 1},
      {"Modulator — on/off",                    1, 1, 0, 1, 1},
      {"", 0, 0, 0, 0, 0},
      {"", 0, 0, 0, 0, 0},
      {"", 0, 0, 0, 0, 0},
    },
    3
  },
  {
    "All Pulse",
    AllPulse::init, AllPulse::update, AllPulse::stop,
    {
      {"Chan On ms — channel on duration",   500, 500, 250, 5000, 50},
      {"Chan Off ms — channel off duration", 500, 500, 250, 5000, 50},
      {"Mod On ms — modulator on duration",  300, 300, 250, 5000, 50},
      {"Mod Off ms — modulator off duration", 700, 700, 250, 5000, 50},
      {"", 0, 0, 0, 0, 0},
      {"", 0, 0, 0, 0, 0},
    },
    4
  },
  {
    "Pairs",
    Pairs::init, Pairs::update, Pairs::stop,
    {
      {"Speed ms — time between pair steps",    300, 300, 250, 3000, 50},
      {"Mode — 0:inward 1:outward 2:alternate",  0, 0, 0, 2, 1},
      {"Modulator — on/off",                      1, 1, 0, 1, 1},
      {"", 0, 0, 0, 0, 0},
      {"", 0, 0, 0, 0, 0},
      {"", 0, 0, 0, 0, 0},
    },
    3
  },
  {
    "Burst",
    Burst::init, Burst::update, Burst::stop,
    {
      {"Fire Rate ms — time between each shot",  250, 250, 250, 500, 10},
      {"Pause ms — gap between bursts",          3000, 3000, 500, 10000, 250},
      {"Channels — how many fire per burst",       12, 12, 1, 12, 1},
      {"Modulator — on during burst",               1, 1, 0, 1, 1},
      {"", 0, 0, 0, 0, 0},
      {"", 0, 0, 0, 0, 0},
    },
    4
  },
  {
    "Rainfall",
    Rainfall::init, Rainfall::update, Rainfall::stop,
    {
      {"Density % — chance of new drops",       15, 15, 1, 100, 5},
      {"Drop ms — how long each drop lasts",   300, 300, 250, 1000, 25},
      {"Modulator — on/off",                      0, 0, 0, 1, 1},
      {"", 0, 0, 0, 0, 0},
      {"", 0, 0, 0, 0, 0},
      {"", 0, 0, 0, 0, 0},
    },
    3
  },
  {
    "Sparkle",
    Sparkle::init, Sparkle::update, Sparkle::stop,
    {
      {"Max Flashes — peak simultaneous channels",  6, 6, 1, 12, 1},
      {"Flash ms — duration of each flash",        300, 300, 250, 1000, 25},
      {"Gap ms — cooldown before re-flash",        500, 500, 250, 3000, 50},
      {"Cycle s — sine wave activity period",       10, 10, 0, 60, 1},
      {"Modulator — on when flashing",               1, 1, 0, 1, 1},
      {"", 0, 0, 0, 0, 0},
    },
    5
  },
  {
    "Stack",
    Stack::init, Stack::update, Stack::stop,
    {
      {"Step ms — time between adding a channel",  300, 300, 250, 2000, 25},
      {"Hold ms — pause when full or empty",       2000, 2000, 500, 10000, 250},
      {"Direction — 0:up 1:down 2:alternate",         0, 0, 0, 2, 1},
      {"Cycle s — sine wave speed variation",          0, 0, 0, 60, 1},
      {"Modulator — on when more than half full",      1, 1, 0, 1, 1},
      {"", 0, 0, 0, 0, 0},
    },
    5
  },
  {
    "Juggle",
    Juggle::init, Juggle::update, Juggle::stop,
    {
      {"Speed ms — base head movement speed",    350, 350, 250, 2000, 25},
      {"Spread ms — speed difference per head",  100, 100, 0, 500, 25},
      {"Cycle s — sine wave speed variation",      8, 8, 0, 60, 1},
      {"Modulator — on at sine peak",              1, 1, 0, 1, 1},
      {"", 0, 0, 0, 0, 0},
      {"", 0, 0, 0, 0, 0},
    },
    4
  },
};

const int NUM_PROGRAMS = sizeof(programs) / sizeof(programs[0]);
