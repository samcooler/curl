#pragma once
#include <Preferences.h>
#include "programs.h"

#define MAX_PLAYLIST_ENTRIES 12
#define MAX_PLAYLISTS 8
#define PLAYLIST_NAME_LEN 20

struct PlaylistEntry {
  int programIndex;
  unsigned long durationMs;
};

struct Playlist {
  char name[PLAYLIST_NAME_LEN];
  PlaylistEntry entries[MAX_PLAYLIST_ENTRIES];
  int numEntries;
};

Playlist playlists[MAX_PLAYLISTS];
int numPlaylists = 0;

Preferences playlistPrefs;

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
  playlistPrefs.begin("playlists", true);
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
  for (int i = idx; i < numPlaylists - 1; i++) {
    playlists[i] = playlists[i + 1];
  }
  numPlaylists--;
  saveAllPlaylists();
}

void createDefaultPlaylists() {
  numPlaylists = 1;
  strncpy(playlists[0].name, "default", PLAYLIST_NAME_LEN);
  playlists[0].numEntries = 10;
  playlists[0].entries[0] = {0, 7000};   // Random Fire
  playlists[0].entries[1] = {1, 6000};   // Chase
  playlists[0].entries[2] = {2, 8000};   // Wave
  playlists[0].entries[3] = {3, 5000};   // All Pulse
  playlists[0].entries[4] = {4, 6000};   // Pairs
  playlists[0].entries[5] = {5, 5000};   // Burst
  playlists[0].entries[6] = {6, 7000};   // Rainfall
  playlists[0].entries[7] = {7, 8000};   // Sparkle
  playlists[0].entries[8] = {8, 8000};   // Stack
  playlists[0].entries[9] = {9, 6000};   // Juggle
  saveAllPlaylists();
}

void initPlaylists() {
  // Force clear stale NVS data from old struct layout
  playlistPrefs.begin("playlists", false);
  playlistPrefs.clear();
  playlistPrefs.end();
  Serial.println("NVS cleared, creating default playlists");
  createDefaultPlaylists();
  Serial.printf("Loaded %d playlists\n", numPlaylists);
}

