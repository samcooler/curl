#pragma once
#include "programs.h"

// ============================================================
//  Playlist Configuration — edit entries below, then re-upload
//
//  Each entry = { programIndex, durationMs, {p0, p1, p2, p3, p4, p5} }
//
//  Program index → name and parameter order:
//    0  Random Fire : density%, minIntMs, maxIntMs, mod%, cycleSec, (unused)
//    1  Chase       : speedMs, width, direction(0:L>R 1:R>L 2:bounce), mod(0/1), -, -
//    2  Wave        : speedMs, width, mod(0/1), reverse(0:up 1:down), -, -
//    3  All Pulse   : chanOnMs, chanOffMs, modOnMs, modOffMs, -, -
//    4  Pairs       : speedMs, mode(0:inward 1:outward 2:alt), mod(0/1), -, -, -
//    5  Burst       : fireRateMs, pauseMs, numChannels, mod(0/1), reverse(0:up 1:down), -
//    6  Rainfall    : density%, dropMs, mod(0/1), -, -, -
//    7  Sparkle     : maxFlashes, flashMs, gapMs, cycleSec, mod(0/1), -
//    8  Stack       : stepMs, holdMs, dir(0:up 1:down 2:alt), cycleSec, mod(0/1), -
//    9  Juggle      : speedMs, spreadMs, cycleSec, mod(0/1), reverse(0:up 1:down), -
//   10  Drift       : speedMs, width, cycleSec, mod(0/1), startDir(0:fall 1:rise), -
//
//  Unused param slots should be 0.  Duration is in milliseconds.
// ============================================================

#define MAX_PLAYLIST_ENTRIES 12

struct PlaylistEntry {
  int programIndex;
  unsigned long durationMs;
  float params[MAX_PARAMS];
};

struct Playlist {
  const char* name;
  PlaylistEntry entries[MAX_PLAYLIST_ENTRIES];
  int numEntries;
};

Playlist playlists[] = {

  // ---- CHILL: slow changes, few solenoids, modulator mostly off ----
  {
    "Chill",
    {
      {0,  9000, {15, 500, 2000, 20, 20, 0}},  // Random Fire — sparse, slow
      {1,  8000, {500, 1, 0, 0, 0, 0}},         // Chase — single head, L→R
      {2, 10000, {2000, 4, 0, 0, 0, 0}},        // Wave — slow wide wave, rising
      {4,  8000, {600, 0, 0, 0, 0, 0}},         // Pairs — slow inward
      {6,  9000, {5, 400, 0, 0, 0, 0}},         // Rainfall — gentle drops
      {7, 10000, {3, 400, 1000, 15, 0, 0}},     // Sparkle — few slow flashes
      {8,  9000, {500, 3000, 0, 20, 0, 0}},     // Stack — slow fill & drain
      {9,  8000, {500, 50, 15, 0, 0, 0}},       // Juggle — lazy heads, rising
      {10, 10000, {600, 1, 15, 0, 0, 0}},        // Drift — single slow drifter, falling
    },
    9
  },

  // ---- MODERATE: medium speed, medium activity, modulator active ----
  {
    "Moderate",
    {
      {0, 7000, {35, 300, 1000, 50, 10, 0}},    // Random Fire — balanced
      {1, 7000, {350, 2, 2, 1, 0, 0}},          // Chase — bounce, mod on
      {2, 8000, {1000, 6, 1, 1, 0, 0}},         // Wave — default, mod on, falling
      {3, 6000, {500, 500, 300, 700, 0, 0}},    // All Pulse — steady rhythm
      {5, 7000, {300, 3000, 10, 1, 0, 0}},      // Burst — 10-channel bursts, rising
      {6, 8000, {15, 300, 0, 0, 0, 0}},         // Rainfall — moderate
      {7, 7000, {6, 300, 500, 10, 1, 0}},       // Sparkle — active flashes
      {9, 6000, {350, 100, 8, 1, 0, 0}},        // Juggle — lively heads, rising
      {10, 7000, {400, 2, 10, 1, 0, 0}},         // Drift — pair drifting, falling
    },
    9
  },

  // ---- INTENSE: fast changes, many solenoids, heavy modulator ----
  {
    "Intense",
    {
      {0, 6000, {70, 250, 600, 80, 5, 0}},      // Random Fire — dense & fast
      {1, 5000, {250, 4, 2, 1, 0, 0}},          // Chase — fast wide bounce
      {3, 5000, {300, 250, 250, 300, 0, 0}},    // All Pulse — rapid toggle
      {4, 6000, {250, 2, 1, 0, 0, 0}},          // Pairs — fast alternating
      {5, 5000, {250, 1500, 12, 1, 1, 0}},      // Burst — full 12-channel, falling
      {7, 6000, {10, 250, 300, 5, 1, 0}},       // Sparkle — max flashes
      {8, 7000, {250, 1000, 2, 5, 1, 0}},       // Stack — rapid fill/drain
      {9, 5000, {250, 200, 4, 1, 1, 0}},        // Juggle — frantic heads, falling
      {10, 5000, {250, 3, 5, 1, 1, 0}},          // Drift — fast triple, rising
    },
    9
  },
};

const int NUM_PLAYLISTS = sizeof(playlists) / sizeof(playlists[0]);

