#pragma once
#include <Preferences.h>
#include "programs.h"

#define MAX_PLAYLIST_ENTRIES 8
#define MAX_PLAYLISTS 8
#define PLAYLIST_NAME_LEN 20

struct PlaylistEntry {
  int programIndex;
  unsigned long durationMs;
  float paramValues[MAX_PARAMS];  // per-entry param overrides (-1 = use default)
};

struct Playlist {
  char name[PLAYLIST_NAME_LEN];
  PlaylistEntry entries[MAX_PLAYLIST_ENTRIES];
  int numEntries;
};

Playlist playlists[MAX_PLAYLISTS];
int numPlaylists = 0;

Preferences playlistPrefs;

// --- Persistence ---

void savePlaylistToNVS(int idx) {
  playlistPrefs.begin("playlists", false);
  char key[16];
  snprintf(key, sizeof(key), "pl%d", idx);
  playlistPrefs.putBytes(key, &playlists[idx], sizeof(Playlist));
  playlistPrefs.putInt("count", numPlaylists);
  playlistPrefs.end();
}

void saveAllPlaylists() {
  playlistPrefs.begin("playlists", false);
  playlistPrefs.putInt("count", numPlaylists);
  for (int i = 0; i < numPlaylists; i++) {
    char key[16];
    snprintf(key, sizeof(key), "pl%d", i);
    playlistPrefs.putBytes(key, &playlists[i], sizeof(Playlist));
  }
  playlistPrefs.end();
}

void loadPlaylistsFromNVS() {
  playlistPrefs.begin("playlists", true);  // read-only
  numPlaylists = playlistPrefs.getInt("count", 0);
  if (numPlaylists > MAX_PLAYLISTS) numPlaylists = MAX_PLAYLISTS;
  for (int i = 0; i < numPlaylists; i++) {
    char key[16];
    snprintf(key, sizeof(key), "pl%d", i);
    playlistPrefs.getBytes(key, &playlists[i], sizeof(Playlist));
  }
  playlistPrefs.end();
}

void deletePlaylistFromNVS(int idx) {
  if (idx < 0 || idx >= numPlaylists) return;
  // Shift playlists down
  for (int i = idx; i < numPlaylists - 1; i++) {
    playlists[i] = playlists[i + 1];
  }
  numPlaylists--;
  saveAllPlaylists();
}

// --- Default playlists (first boot) ---

void initDefaultEntry(PlaylistEntry& e, int progIdx, unsigned long durMs) {
  e.programIndex = progIdx;
  e.durationMs = durMs;
  for (int i = 0; i < MAX_PARAMS; i++) e.paramValues[i] = -1;  // use defaults
}

void createDefaultPlaylists() {
  // Gentle Flow
  numPlaylists = 3;
  strncpy(playlists[0].name, "Gentle Flow", PLAYLIST_NAME_LEN);
  playlists[0].numEntries = 3;
  initDefaultEntry(playlists[0].entries[0], 6, 30000);  // Rainfall
  initDefaultEntry(playlists[0].entries[1], 2, 30000);  // Wave
  initDefaultEntry(playlists[0].entries[2], 3, 20000);  // All Pulse

  // High Energy
  strncpy(playlists[1].name, "High Energy", PLAYLIST_NAME_LEN);
  playlists[1].numEntries = 4;
  initDefaultEntry(playlists[1].entries[0], 1, 15000);  // Chase
  initDefaultEntry(playlists[1].entries[1], 5, 15000);  // Burst
  initDefaultEntry(playlists[1].entries[2], 0, 20000);  // Random Fire
  initDefaultEntry(playlists[1].entries[3], 4, 15000);  // Pairs

  // Full Demo
  strncpy(playlists[2].name, "Full Demo", PLAYLIST_NAME_LEN);
  playlists[2].numEntries = 7;
  for (int i = 0; i < 7; i++) {
    initDefaultEntry(playlists[2].entries[i], i, 20000);
  }

  saveAllPlaylists();
}

void initPlaylists() {
  loadPlaylistsFromNVS();
  if (numPlaylists == 0) {
    Serial.println("No saved playlists, creating defaults");
    createDefaultPlaylists();
  }
  Serial.printf("Loaded %d playlists\n", numPlaylists);
}

// --- Playlist entry helpers ---

void playlistAddEntry(int plIdx, int progIdx, unsigned long durMs) {
  Playlist& pl = playlists[plIdx];
  if (pl.numEntries >= MAX_PLAYLIST_ENTRIES) return;
  initDefaultEntry(pl.entries[pl.numEntries], progIdx, durMs);
  pl.numEntries++;
  savePlaylistToNVS(plIdx);
}

void playlistRemoveEntry(int plIdx, int entryIdx) {
  Playlist& pl = playlists[plIdx];
  if (entryIdx < 0 || entryIdx >= pl.numEntries) return;
  for (int i = entryIdx; i < pl.numEntries - 1; i++) {
    pl.entries[i] = pl.entries[i + 1];
  }
  pl.numEntries--;
  savePlaylistToNVS(plIdx);
}

void playlistMoveEntry(int plIdx, int entryIdx, int direction) {
  Playlist& pl = playlists[plIdx];
  int newIdx = entryIdx + direction;
  if (newIdx < 0 || newIdx >= pl.numEntries) return;
  PlaylistEntry tmp = pl.entries[entryIdx];
  pl.entries[entryIdx] = pl.entries[newIdx];
  pl.entries[newIdx] = tmp;
  savePlaylistToNVS(plIdx);
}

int createNewPlaylist(const char* name) {
  if (numPlaylists >= MAX_PLAYLISTS) return -1;
  int idx = numPlaylists;
  strncpy(playlists[idx].name, name, PLAYLIST_NAME_LEN - 1);
  playlists[idx].name[PLAYLIST_NAME_LEN - 1] = '\0';
  playlists[idx].numEntries = 0;
  numPlaylists++;
  savePlaylistToNVS(idx);
  return idx;
}

