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
//
// Each of the 12 channels independently toggles on/off on its own random
// timer.  When a channel's timer fires, it rolls against "density %" to
// decide whether to turn on; if it loses, it waits another random interval
// between minInterval–maxInterval.  If it wins, it stays on for a shorter
// random period (minInterval to maxInterval/2) before turning off again.
//
// Density can optionally breathe over time via a sine-wave cycle,
// creating natural surges and lulls.  The modulator runs on its own
// independent timer with the same density logic.
//
// Params: density%, minIntervalMs, maxIntervalMs, modulator%, cycleSec
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
// Program 2: Chase
//
// A "head" of 1–6 lit channels sweeps across the 12 outputs.
// Each step is split into two half-steps at the configured speed:
//   half-step 1 — advance the head position, modulator OFF
//   half-step 2 — same position, modulator ON (if enabled)
// This creates a stutter-step "push" effect on each position.
//
// Direction modes: 0 = left-to-right (wraps), 1 = right-to-left (wraps),
//                  2 = bounce (reverses at ends).
//
// Params: speedMs, width, direction, modulator(0/1)
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
//
// A continuous sinusoidal wave propagates across the 12 channels.
// Each channel's on/off state is determined by sampling sin() at
// a phase offset proportional to its position.  "Speed" controls
// the full cycle period — lower values make the wave travel faster.
// "Width" sets how many channels fit in one wavelength: smaller
// values make tighter waves with more peaks visible simultaneously,
// larger values make a single broad wave.
//
// Reverse flips propagation direction: 0 = bottom→top (ch 0→11),
// 1 = top→bottom (ch 11→0).  Channel 11 is physically at the top.
//
// Params: speedMs, width, modulator(0/1), reverse(0/1)
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
    bool rev    = programs[2].params[3].value > 0.5;

    float phase = (float)(now - startTime) / speed * TWO_PI;
    float dir   = rev ? -1.0f : 1.0f;

    for (int i = 0; i < 12; i++) {
      float channelPhase = phase - dir * (float)i / width * TWO_PI;
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
//
// All 12 channels pulse together in unison on a single clock,
// while the modulator runs on a separate independent clock.
// Both clocks have ±20% random jitter on each toggle, so the
// rhythm drifts slightly and feels organic rather than mechanical.
//
// Channel on-time and off-time are independently adjustable,
// as are modulator on-time and off-time.  Equal on/off values
// give a 50% duty cycle; short on + long off creates brief bursts.
//
// Params: chanOnMs, chanOffMs, modOnMs, modOffMs
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
//
// The 12 channels are treated as 6 symmetric pairs:
//   pair 0 = ch 0 + ch 11,  pair 1 = ch 1 + ch 10, ... pair 5 = ch 5 + ch 6
// Each step turns off the previous pair and lights the next one.
//
// Modes:  0 = inward (pairs 0→5, outside→center),
//         1 = outward (pairs 5→0, center→outside),
//         2 = alternate (same as inward, reserved for future bounce).
// Only one pair is active at a time — 2 channels out of 12.
//
// Params: speedMs, mode, modulator(0/1)
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
//
// Fires channels sequentially (0, 1, 2, ...) one at a time at
// "fire rate" speed.  Each new channel turns on as the previous
// turns off, creating a rapid cascade.  After all configured
// channels have fired, everything goes dark for the "pause"
// duration, then the burst repeats from channel 0.
//
// The modulator activates during the burst and turns off during
// the pause, giving extra flow emphasis during the active phase.
// "Channels" controls how many of the 12 fire per burst.
//
// Reverse flips burst direction: 0 = bottom→top (ch 0 first),
// 1 = top→bottom (ch 11 first).  Channel 11 is physically at the top.
//
// Params: fireRateMs, pauseMs, numChannels, modulator(0/1), reverse(0/1)
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
    bool rev         = programs[5].params[4].value > 0.5;

    if (inPause) {
      if (now - lastStep >= (unsigned long)pauseTime) {
        inPause = false;
        burstIndex = 0;
        lastStep = now;
      }
    } else {
      if (now - lastStep >= (unsigned long)fireRate) {
        lastStep = now;
        if (burstIndex > 0) {
          int prevCh = rev ? (11 - (burstIndex - 1)) : (burstIndex - 1);
          setSolenoid(prevCh, false);
        }
        if (burstIndex < numChannels && burstIndex < 12) {
          int ch = rev ? (11 - burstIndex) : burstIndex;
          setSolenoid(ch, true);
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
//
// Random independent "drops" on each channel.  Each channel runs
// its own cycle:  when idle, it checks every ~200ms whether to
// start a new drop (density% chance per check).  A drop stays on
// for the configured duration, then the channel goes idle again
// with a brief mandatory gap before the next check.
//
// Unlike Random Fire, drops have a fixed on-duration and clear
// gaps between them, so the visual reads as distinct drips rather
// than flickering.  Low density (5–15%) gives sparse gentle rain;
// high density (50%+) creates a downpour.
//
// Params: density%, dropMs, modulator(0/1)
// ============================================================
namespace Rainfall {
  unsigned long dropEnd[12];
  unsigned long nextCheck[12];

  void init() {
    unsigned long now = millis();
    for (int i = 0; i < 12; i++) {
      dropEnd[i] = 0;
      nextCheck[i] = now + random(0, 500);  // stagger initial checks
      setSolenoid(i, false);
    }
  }

  void update(unsigned long now) {
    float density  = programs[6].params[0].value;
    float duration = programs[6].params[1].value;
    bool mod       = programs[6].params[2].value > 0.5;

    for (int i = 0; i < 12; i++) {
      // Turn off expired drops, add a brief gap before next check
      if (solenoidStates[i] && now >= dropEnd[i]) {
        setSolenoid(i, false);
        nextCheck[i] = now + (unsigned long)(duration * 0.5) + random(0, (int)duration);
      }
      // Try to start a new drop (only when idle and check interval elapsed)
      if (!solenoidStates[i] && now >= nextCheck[i]) {
        if (random(100) < (int)density) {
          setSolenoid(i, true);
          dropEnd[i] = now + (unsigned long)duration;
        } else {
          nextCheck[i] = now + 100 + random(0, 200);
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
// Program 8: Sparkle
//
// Brief random flashes across the 12 channels, with overall
// activity level breathing via a sine wave.  The sine cycle
// modulates "target active" from 1 up to maxFlashes.  Each
// channel that finishes a flash enters a "gap" cooldown before
// it can flash again, preventing rapid re-triggers.
//
// At sine peak, many channels flash simultaneously; at the
// trough, only 1–2 are active.  This creates a natural
// breathing/twinkling effect.  Flash duration controls how
// long each individual flash stays on; gap controls the
// minimum cooldown between flashes on the same channel.
//
// Params: maxFlashes, flashMs, gapMs, cycleSec, modulator(0/1)
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
// Program 9: Stack
//
// Channels accumulate one at a time (0→1→2→...→11), pause when
// all 12 are full, then drain one at a time back to empty, pause
// again, and repeat.  The step speed is modulated by a sine wave:
// at sine peak the steps are 0.3x the base speed (fast), at the
// trough they're 1.7x (slow), creating organic acceleration and
// deceleration.
//
// Direction: 0 = up (ch 0 first), 1 = down (ch 11 first),
//            2 = alternate (always refills from same direction).
// The modulator activates when more than half the channels are on.
//
// Params: stepMs, holdMs, direction, cycleSec, modulator(0/1)
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
// Program 10: Juggle
//
// Three independent chase heads circle the 12 channels at
// different speeds.  Head 0 runs at (base - spread), head 1 at
// base speed, and head 2 at (base + spread).  All three speeds
// are further modulated by a sine wave — at the peak everything
// accelerates (0.4x), at the trough it slows (1.6x).
//
// Channels light up wherever any head currently sits; when two
// heads overlap the same channel it just stays on.  Typically
// 3 channels are lit at once, but overlaps reduce that.  The
// speed differences cause the heads to converge and diverge,
// creating evolving patterns.
//
// Reverse flips head movement: 0 = bottom→top (ch 0→11),
// 1 = top→bottom (ch 11→0).  Channel 11 is physically at the top.
//
// Params: speedMs, spreadMs, cycleSec, modulator(0/1), reverse(0/1)
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
    bool rev        = programs[9].params[4].value > 0.5;

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
        if (rev)
          headPos[h] = (headPos[h] - 1 + 12) % 12;
        else
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
// Program 11: Drift
//
// A tight cluster of 1–3 adjacent channels sweeps top→bottom
// then bottom→top in a sawtooth bounce.  The sweep speed varies
// sinusoidally:  slow → moderate → fast → fastest → fast →
// moderate → slow, creating a breathing rhythm of movement.
//
// At the sine peak, step time is 0.25x the base speed (fast);
// at the trough, it's 1.75x (slow).  "Width" controls how many
// adjacent channels are lit as the cluster moves.  The modulator
// fires on every other step for extra flow emphasis when enabled.
//
// Start direction: 0 = starts falling (top→bottom first),
// 1 = starts rising (bottom→top first).
//
// Params: speedMs, width, cycleSec, modulator(0/1), startDir(0/1)
// ============================================================
namespace Drift {
  int pos;
  int dir;       // +1 = moving up, -1 = moving down
  bool modStep;
  unsigned long lastStep;
  unsigned long startTime;

  void init() {
    startTime = millis();
    bool startUp = programs[10].params[4].value > 0.5;
    if (startUp) {
      pos = 0;
      dir = 1;
    } else {
      pos = 11;
      dir = -1;
    }
    modStep = false;
    lastStep = millis();
    for (int i = 0; i < 12; i++) setSolenoid(i, false);
    setModulator(false);
  }

  void update(unsigned long now) {
    float baseSpeed = programs[10].params[0].value;
    int width       = (int)programs[10].params[1].value;
    float cycleSec  = programs[10].params[2].value;
    bool modEnabled = programs[10].params[3].value > 0.5;

    // Sine modulates step speed: range 0.25x (fast) to 1.75x (slow)
    float sf = sineFactor(now, startTime, cycleSec);
    float effectiveSpeed = baseSpeed * (0.25 + 1.5 * (1.0 - sf));
    if (effectiveSpeed < 100) effectiveSpeed = 100;

    if (now - lastStep >= (unsigned long)effectiveSpeed) {
      lastStep = now;

      // Advance and bounce at ends
      pos += dir;
      if (pos >= 12) { pos = 10; dir = -1; }
      if (pos < 0)   { pos = 1;  dir = 1;  }

      // Light the cluster (width channels starting at pos)
      for (int i = 0; i < 12; i++) {
        bool on = false;
        for (int w = 0; w < width; w++) {
          int ch = pos + (dir > 0 ? w : -w);
          if (ch < 0) ch += 12;
          if (ch >= 12) ch -= 12;
          if (i == ch) { on = true; break; }
        }
        setSolenoid(i, on);
      }

      // Modulator on alternating steps
      modStep = !modStep;
      setModulator(modEnabled && modStep);
    }
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
      {"Reverse — 0:up 1:down",                 0, 0, 0, 1, 1},
      {"", 0, 0, 0, 0, 0},
      {"", 0, 0, 0, 0, 0},
    },
    4
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
      {"Reverse — 0:up 1:down",                     0, 0, 0, 1, 1},
      {"", 0, 0, 0, 0, 0},
    },
    5
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
      {"Reverse — 0:up 1:down",                    0, 0, 0, 1, 1},
      {"", 0, 0, 0, 0, 0},
    },
    5
  },
  {
    "Drift",
    Drift::init, Drift::update, Drift::stop,
    {
      {"Speed ms — base step time",               400, 400, 100, 2000, 25},
      {"Width — number of lit channels",             2, 2, 1, 3, 1},
      {"Cycle s — sine speed variation period",     10, 10, 2, 60, 1},
      {"Modulator — on alternating steps",           1, 1, 0, 1, 1},
      {"Start Dir — 0:fall first 1:rise first",   0, 0, 0, 1, 1},
      {"", 0, 0, 0, 0, 0},
    },
    5
  },
};

const int NUM_PROGRAMS = sizeof(programs) / sizeof(programs[0]);
