#include <Arduino.h>
#include <WiFi.h>
#include <ESPUI.h>
#include <PCA9539.h>
#include "programs.h"
#include "playlists.h"

// WiFi settings — connect to existing network
const char* ssid = "Blossom 2.4 GHz";
const char* password = "pollinate";

// PCA9539 I2C GPIO expander for solenoids
PCA9539 pca9539(0x77);
const int NUM_SOLENOIDS = 12;
bool solenoidStates[NUM_SOLENOIDS] = {};

// Modulator — solenoid that doubles flow when active
const int MODULATOR_PIN = 3;
bool modulatorState = false;

// Output mapping: logical channel index → physical PCA9539 pin index
// Edit this array to match your wiring
const int outputMap[NUM_SOLENOIDS] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

// Test sequence state
bool testRunning = false;
int testIndex = 0;
unsigned long testNextTime = 0;
const int TEST_ON_MS = 500;
const int TEST_OFF_MS = 300;
bool testPhaseOn = true;

// Playlist cycle button (D0)
const int BUTTON_PIN = D0;
bool lastButtonState = HIGH;          // active-low with INPUT_PULLUP
unsigned long lastButtonTime = 0;
const unsigned long DEBOUNCE_MS = 50;

// Confirmation pulse state: after button press, pulse N solenoids for 500ms
bool confirmPulseActive = false;
unsigned long confirmPulseStart = 0;
const unsigned long CONFIRM_PULSE_MS = 500;

// Mode system
enum Mode { MODE_STOP = 0, MODE_PROGRAM = 1, MODE_PLAYLIST = 2 };
Mode currentMode = MODE_STOP;

// ESPUI control IDs
uint16_t uptimeLabel;
uint16_t ipLabel;
uint16_t wifiStatusLabel;
uint16_t solenoidSwitches[NUM_SOLENOIDS];
uint16_t modulatorSwitch;
uint16_t testButton;

// Programs tab controls
uint16_t modeSelector;
uint16_t playlistSelector;
uint16_t programSelector;
uint16_t programStatusLabel;
uint16_t paramSliders[MAX_PARAMS];
uint16_t paramSliderMin[MAX_PARAMS];
uint16_t paramSliderMax[MAX_PARAMS];

// Program/playlist state
int selectedProgram = 0;
int selectedPlaylist = 1;  // default to Moderate
int currentEntry = 0;
unsigned long entryStartTime = 0;

unsigned long lastUiUpdate = 0;
bool programUIDirty = false;

// Build HTML status string with program name and all params
String buildProgramStatus(int progIdx, const char* prefix = "&#9654; ") {
  Program& p = programs[progIdx];
  String s = prefix;
  s += p.name;
  for (int i = 0; i < p.numParams; i++) {
    s += "<br>&nbsp;&nbsp;";
    // Use short name (before " — ") for compact display
    String pname = p.params[i].name;
    int dash = pname.indexOf(" —");
    if (dash > 0) pname = pname.substring(0, dash);
    s += pname;
    s += ": ";
    if (p.params[i].step >= 1.0)
      s += String((int)p.params[i].value);
    else
      s += String(p.params[i].value, 1);
  }
  return s;
}

// --- Solenoid helpers ---

void setSolenoid(int index, bool state) {
  solenoidStates[index] = state;
  pca9539.digitalWrite(outputMap[index], state ? HIGH : LOW);
}

void setModulator(bool state) {
  modulatorState = state;
  digitalWrite(MODULATOR_PIN, state ? HIGH : LOW);
}

void allSolenoidsOff() {
  for (int i = 0; i < NUM_SOLENOIDS; i++) {
    setSolenoid(i, false);
    ESPUI.updateSwitcher(solenoidSwitches[i], false);
  }
}

void allSolenoidsOn() {
  for (int i = 0; i < NUM_SOLENOIDS; i++) {
    setSolenoid(i, true);
    ESPUI.updateSwitcher(solenoidSwitches[i], true);
  }
}

// --- ESPUI callbacks ---

void modulatorCallback(Control* sender, int type) {
  bool on = (type == S_ACTIVE);
  setModulator(on);
  Serial.printf("Modulator: %s\n", on ? "ON" : "OFF");
}

void solenoidCallback(Control* sender, int type) {
  // Find which solenoid this switch controls
  for (int i = 0; i < NUM_SOLENOIDS; i++) {
    if (sender->id == solenoidSwitches[i]) {
      bool on = (type == S_ACTIVE);
      setSolenoid(i, on);
      Serial.printf("Solenoid %d: %s\n", i, on ? "ON" : "OFF");
      break;
    }
  }
}

void allOnCallback(Control* sender, int type) {
  if (type == B_DOWN) {
    allSolenoidsOn();
    Serial.println("All solenoids ON");
  }
}

void allOffCallback(Control* sender, int type) {
  if (type == B_DOWN) {
    allSolenoidsOff();
    Serial.println("All solenoids OFF");
  }
}

void testCallback(Control* sender, int type) {
  if (type == B_DOWN) {
    if (!testRunning) {
      testRunning = true;
      testIndex = 0;
      testPhaseOn = true;
      testNextTime = millis();
      allSolenoidsOff();
      Serial.println("Test sequence started");
    } else {
      testRunning = false;
      allSolenoidsOff();
      Serial.println("Test sequence stopped");
    }
  }
}

// --- Program helpers ---

void showParamsForProgram(int progIdx) {
  Program& p = programs[progIdx];
  for (int i = 0; i < MAX_PARAMS; i++) {
    if (i < p.numParams) {
      // Update slider min/max bounds for this program's parameter
      ESPUI.getControl(paramSliderMin[i])->value = String((int)p.params[i].min);
      ESPUI.updateControl(paramSliderMin[i]);
      ESPUI.getControl(paramSliderMax[i])->value = String((int)p.params[i].max);
      ESPUI.updateControl(paramSliderMax[i]);
      ESPUI.updateSlider(paramSliders[i], (int)p.params[i].value);
      ESPUI.updateVisibility(paramSliders[i], true);
      ESPUI.getControl(paramSliders[i])->label = p.params[i].name;
      ESPUI.updateControl(paramSliders[i]);
    } else {
      ESPUI.updateVisibility(paramSliders[i], false);
    }
  }
}

void startProgram(int progIdx) {
  // Stop previous program cleanly
  if (currentMode == MODE_PROGRAM || currentMode == MODE_PLAYLIST) {
    programs[selectedProgram].stop();
    allSolenoidsOff();
    setModulator(false);
  }
  selectedProgram = progIdx;
  showParamsForProgram(progIdx);
  programs[progIdx].init();
  Serial.printf("Started program: %s\n", programs[progIdx].name);
}

void startPlaylistEntry(int entryIdx) {
  Playlist& pl = playlists[selectedPlaylist];
  currentEntry = entryIdx;
  entryStartTime = millis();
  int progIdx = pl.entries[entryIdx].programIndex;

  // Copy entry's custom params to program
  for (int i = 0; i < programs[progIdx].numParams; i++) {
    programs[progIdx].params[i].value = pl.entries[entryIdx].params[i];
  }

  startProgram(progIdx);
  ESPUI.print(programSelector, String(progIdx));
}

void stopEverything() {
  if (currentMode != MODE_STOP) {
    programs[selectedProgram].stop();
  }
  currentMode = MODE_STOP;
  testRunning = false;
  allSolenoidsOff();
  setModulator(false);
  ESPUI.print(programStatusLabel, "Stopped");
  ESPUI.print(modeSelector, "0");
}

// --- Callbacks ---

void modeSelectCallback(Control* sender, int type) {
  int idx = sender->value.toInt();
  Mode newMode = (Mode)idx;
  if (newMode == currentMode) return;

  // Stop current activity
  if (currentMode != MODE_STOP) {
    programs[selectedProgram].stop();
    allSolenoidsOff();
    setModulator(false);
  }
  testRunning = false;
  currentMode = newMode;

  switch (currentMode) {
    case MODE_STOP:
      ESPUI.print(programStatusLabel, "Stopped");
      break;
    case MODE_PROGRAM:
      startProgram(selectedProgram);
      ESPUI.print(programStatusLabel, buildProgramStatus(selectedProgram));
      break;
    case MODE_PLAYLIST:
      if (playlists[selectedPlaylist].numEntries > 0) {
        startPlaylistEntry(0);
      }
      break;
  }
}

void playlistSelectCallback(Control* sender, int type) {
  int idx = sender->value.toInt();
  if (idx >= 0 && idx < NUM_PLAYLISTS) {
    selectedPlaylist = idx;
    if (currentMode == MODE_PLAYLIST && playlists[idx].numEntries > 0) {
      startPlaylistEntry(0);
    }
  }
}

void programSelectCallback(Control* sender, int type) {
  int idx = sender->value.toInt();
  if (idx < 0 || idx >= NUM_PROGRAMS) return;
  if (currentMode == MODE_PLAYLIST) return; // playlist controls the program
  selectedProgram = idx;
  showParamsForProgram(idx);
  if (currentMode == MODE_PROGRAM) {
    startProgram(idx);
    ESPUI.print(programStatusLabel, buildProgramStatus(idx));
  }
}

void paramSliderCallback(Control* sender, int type) {
  for (int i = 0; i < MAX_PARAMS; i++) {
    if (sender->id == paramSliders[i]) {
      programs[selectedProgram].params[i].value = sender->value.toFloat();
      break;
    }
  }
  if (currentMode == MODE_PROGRAM) {
    // Restart program with new params
    programs[selectedProgram].stop();
    allSolenoidsOff();
    setModulator(false);
    programs[selectedProgram].init();
  }
  if (currentMode != MODE_STOP) {
    programUIDirty = true;
  }
}

void stopAllCallback(Control* sender, int type) {
  if (type == B_DOWN) {
    stopEverything();
    Serial.println("STOP ALL");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Curl controller starting");

  // PCA9539 reset
  pinMode(D3, OUTPUT);
  digitalWrite(D3, HIGH);

  // Modulator pin
  pinMode(MODULATOR_PIN, OUTPUT);
  digitalWrite(MODULATOR_PIN, LOW);

  // Playlist cycle button
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Configure solenoid pins as outputs, all off
  pca9539.pinMode(pca_A0, OUTPUT);
  pca9539.pinMode(pca_A1, OUTPUT);
  pca9539.pinMode(pca_A2, OUTPUT);
  pca9539.pinMode(pca_A3, OUTPUT);
  pca9539.pinMode(pca_A4, OUTPUT);
  pca9539.pinMode(pca_A5, OUTPUT);
  pca9539.pinMode(pca_A6, OUTPUT);
  pca9539.pinMode(pca_A7, OUTPUT);
  pca9539.pinMode(pca_B0, OUTPUT);
  pca9539.pinMode(pca_B1, OUTPUT);
  pca9539.pinMode(pca_B2, OUTPUT);
  pca9539.pinMode(pca_B3, OUTPUT);
  for (int i = 0; i < NUM_SOLENOIDS; i++) {
    setSolenoid(i, false);
  }
  Serial.println("Solenoids initialized");

  // Connect to WiFi network
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to ");
  Serial.print(ssid);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connection failed - starting anyway");
  }

  // --- ESPUI Setup ---

  // Status tab
  uint16_t statusTab = ESPUI.addControl(ControlType::Tab, "Status", "Status");
  ipLabel = ESPUI.addControl(ControlType::Label, "IP Address",
    WiFi.localIP().toString(), ControlColor::Turquoise, statusTab);
  wifiStatusLabel = ESPUI.addControl(ControlType::Label, "WiFi Status",
    WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected",
    ControlColor::Turquoise, statusTab);
  uptimeLabel = ESPUI.addControl(ControlType::Label, "Uptime",
    "0s", ControlColor::Turquoise, statusTab);

  // Control tab
  uint16_t controlTab = ESPUI.addControl(ControlType::Tab, "Control", "Control");

  // Modulator — doubles flow when active
  modulatorSwitch = ESPUI.addControl(ControlType::Switcher, "Modulator",
    "0", ControlColor::Sunflower, controlTab, modulatorCallback);

  // Compact CSS for channel panels
  const char* compactStyle = "margin:2px;padding:4px;min-height:0;";

  // Channel solenoids — grouped into rows of 4
  // Row 1: channels 0-3
  char label[16];
  snprintf(label, sizeof(label), "Ch %d", 0);
  solenoidSwitches[0] = ESPUI.addControl(ControlType::Switcher, label,
    "0", ControlColor::Alizarin, controlTab, solenoidCallback);
  ESPUI.setPanelStyle(solenoidSwitches[0], compactStyle);
  for (int i = 1; i <= 3; i++) {
    snprintf(label, sizeof(label), "Ch %d", i);
    solenoidSwitches[i] = ESPUI.addControl(ControlType::Switcher, label,
      "0", ControlColor::None, solenoidSwitches[0], solenoidCallback);
  }

  // Row 2: channels 4-7
  snprintf(label, sizeof(label), "Ch %d", 4);
  solenoidSwitches[4] = ESPUI.addControl(ControlType::Switcher, label,
    "0", ControlColor::Alizarin, controlTab, solenoidCallback);
  ESPUI.setPanelStyle(solenoidSwitches[4], compactStyle);
  for (int i = 5; i <= 7; i++) {
    snprintf(label, sizeof(label), "Ch %d", i);
    solenoidSwitches[i] = ESPUI.addControl(ControlType::Switcher, label,
      "0", ControlColor::None, solenoidSwitches[4], solenoidCallback);
  }

  // Row 3: channels 8-11
  snprintf(label, sizeof(label), "Ch %d", 8);
  solenoidSwitches[8] = ESPUI.addControl(ControlType::Switcher, label,
    "0", ControlColor::Alizarin, controlTab, solenoidCallback);
  ESPUI.setPanelStyle(solenoidSwitches[8], compactStyle);
  for (int i = 9; i <= 11; i++) {
    snprintf(label, sizeof(label), "Ch %d", i);
    solenoidSwitches[i] = ESPUI.addControl(ControlType::Switcher, label,
      "0", ControlColor::None, solenoidSwitches[8], solenoidCallback);
  }

  // All On / All Off / Test buttons
  ESPUI.addControl(ControlType::Separator, "Actions", "", ControlColor::None, controlTab);
  ESPUI.addControl(ControlType::Button, "All On", "ALL ON",
    ControlColor::Emerald, controlTab, allOnCallback);
  ESPUI.addControl(ControlType::Button, "All Off", "ALL OFF",
    ControlColor::Carrot, controlTab, allOffCallback);
  testButton = ESPUI.addControl(ControlType::Button, "Test Sequence", "START TEST",
    ControlColor::Peterriver, controlTab, testCallback);

  // --- Programs tab ---
  uint16_t programsTab = ESPUI.addControl(ControlType::Tab, "Programs", "Programs");

  // Mode selector: Stop / Program / Playlist
  modeSelector = ESPUI.addControl(ControlType::Select, "Mode",
    "0", ControlColor::Wetasphalt, programsTab, modeSelectCallback);
  ESPUI.addControl(ControlType::Option, "Stop", "0", ControlColor::None, modeSelector);
  ESPUI.addControl(ControlType::Option, "Program", "1", ControlColor::None, modeSelector);
  ESPUI.addControl(ControlType::Option, "Playlist", "2", ControlColor::None, modeSelector);

  // Playlist selector
  playlistSelector = ESPUI.addControl(ControlType::Select, "Playlist",
    "1", ControlColor::Wetasphalt, programsTab, playlistSelectCallback);
  for (int i = 0; i < NUM_PLAYLISTS; i++) {
    ESPUI.addControl(ControlType::Option, playlists[i].name,
      String(i), ControlColor::None, playlistSelector);
  }

  // Program selector
  programSelector = ESPUI.addControl(ControlType::Select, "Program",
    "0", ControlColor::Wetasphalt, programsTab, programSelectCallback);
  for (int i = 0; i < NUM_PROGRAMS; i++) {
    ESPUI.addControl(ControlType::Option, programs[i].name,
      String(i), ControlColor::None, programSelector);
  }

  // Stop All button
  ESPUI.addControl(ControlType::Button, "", "STOP ALL",
    ControlColor::Alizarin, programsTab, stopAllCallback);

  // Status label
  programStatusLabel = ESPUI.addControl(ControlType::Label, "Status",
    "Stopped", ControlColor::Turquoise, programsTab);

  // Parameter sliders (create MAX_PARAMS, hide unused ones)
  for (int i = 0; i < MAX_PARAMS; i++) {
    const char* pName = (i < programs[0].numParams) ? programs[0].params[i].name : "";
    float pVal = (i < programs[0].numParams) ? programs[0].params[i].value : 0;
    float pMin = (i < programs[0].numParams) ? programs[0].params[i].min : 0;
    float pMax = (i < programs[0].numParams) ? programs[0].params[i].max : 100;

    paramSliders[i] = ESPUI.addControl(ControlType::Slider, pName,
      String((int)pVal), ControlColor::Alizarin, programsTab, paramSliderCallback);
    paramSliderMin[i] = ESPUI.addControl(ControlType::Min, "", String((int)pMin), ControlColor::None, paramSliders[i]);
    paramSliderMax[i] = ESPUI.addControl(ControlType::Max, "", String((int)pMax), ControlColor::None, paramSliders[i]);

    if (i >= programs[0].numParams) {
      ESPUI.updateVisibility(paramSliders[i], false);
    }
  }

  // Inject custom CSS via a label with a <style> tag (no setCustomCSS in v2.2.4)
  static const char cssHack[] =
    "<style>"
    ".section{padding:0.5em !important;margin-bottom:0.4em !important}"
    ".section h3{font-size:1.0em !important;margin-bottom:0.2em !important}"
    ".section span{font-size:0.9em !important}"
    ".section button{padding:0.3em 0.8em !important;font-size:0.85em !important;margin:0.15em !important}"
    ".section select,.section input{font-size:0.9em !important;padding:0.2em !important}"
    "#tabsnav{padding:0.3em !important}"
    "#tabsnav button{padding:0.3em 0.6em !important;font-size:0.9em !important}"
    "</style>";
  uint16_t cssLabel = ESPUI.addControl(ControlType::Label, "", cssHack, ControlColor::None, Control::noParent);
  ESPUI.setPanelStyle(cssLabel, "display:none;");

  // Start ESPUI
  ESPUI.begin("Curl Controller");
  Serial.println("ESPUI started");

  // Auto-start Moderate playlist
  selectedPlaylist = 1;
  currentMode = MODE_PLAYLIST;
  startPlaylistEntry(0);
  ESPUI.print(modeSelector, "2");
  Serial.println("Auto-started Moderate playlist");
}

void loop() {
  unsigned long now = millis();

  // --- Playlist cycle button (D0, active-low) ---
  bool btnState = digitalRead(BUTTON_PIN);
  if (btnState == LOW && lastButtonState == HIGH && (now - lastButtonTime) > DEBOUNCE_MS) {
    lastButtonTime = now;

    // Stop current activity
    if (currentMode != MODE_STOP) {
      programs[selectedProgram].stop();
    }
    testRunning = false;
    allSolenoidsOff();
    setModulator(false);

    // Advance to next playlist (wrap around)
    selectedPlaylist = (selectedPlaylist + 1) % NUM_PLAYLISTS;
    currentMode = MODE_PLAYLIST;

    // Start confirmation pulse: light solenoids 0..(N-1) where N = ordinal
    int ordinal = selectedPlaylist + 1;
    for (int i = 0; i < ordinal && i < NUM_SOLENOIDS; i++) {
      setSolenoid(i, true);
    }
    confirmPulseActive = true;
    confirmPulseStart = now;

    // Update UI
    ESPUI.print(modeSelector, "2");
    ESPUI.print(playlistSelector, String(selectedPlaylist));
    Serial.printf("Button: switched to playlist %d (%s)\n", selectedPlaylist, playlists[selectedPlaylist].name);
  }
  lastButtonState = btnState;

  // --- Confirmation pulse end ---
  if (confirmPulseActive && (now - confirmPulseStart) >= CONFIRM_PULSE_MS) {
    confirmPulseActive = false;
    allSolenoidsOff();
    // Now start the playlist
    startPlaylistEntry(0);
  }

  // Deferred program status refresh
  if (programUIDirty) {
    programUIDirty = false;
    if (currentMode != MODE_STOP) {
      ESPUI.print(programStatusLabel, buildProgramStatus(selectedProgram));
    }
  }

  // Run test sequence (non-blocking)
  if (testRunning && now >= testNextTime) {
    if (testPhaseOn) {
      setSolenoid(testIndex, true);
      ESPUI.updateSwitcher(solenoidSwitches[testIndex], true);
      testNextTime = now + TEST_ON_MS;
      testPhaseOn = false;
    } else {
      setSolenoid(testIndex, false);
      ESPUI.updateSwitcher(solenoidSwitches[testIndex], false);
      testIndex++;
      if (testIndex >= NUM_SOLENOIDS) {
        testRunning = false;
      } else {
        testNextTime = now + TEST_OFF_MS;
        testPhaseOn = true;
      }
    }
  }

  // Run active program (skip during confirmation pulse)
  if (!confirmPulseActive && (currentMode == MODE_PROGRAM || currentMode == MODE_PLAYLIST)) {
    programs[selectedProgram].update(now);
  }

  // Playlist advancement (skip during confirmation pulse)
  if (!confirmPulseActive && currentMode == MODE_PLAYLIST) {
    Playlist& pl = playlists[selectedPlaylist];
    unsigned long elapsed = now - entryStartTime;
    unsigned long duration = pl.entries[currentEntry].durationMs;
    if (elapsed >= duration) {
      int nextEntry = (currentEntry + 1) % pl.numEntries;
      startPlaylistEntry(nextEntry);
    }
  }

  // Update status labels every second
  if (now - lastUiUpdate >= 1000) {
    lastUiUpdate = now;

    unsigned long secs = now / 1000;
    unsigned long mins = secs / 60;
    unsigned long hrs = mins / 60;
    char buf[32];
    snprintf(buf, sizeof(buf), "%luh %lum %lus", hrs, mins % 60, secs % 60);
    ESPUI.updateLabel(uptimeLabel, buf);

    if (WiFi.status() == WL_CONNECTED) {
      ESPUI.updateLabel(wifiStatusLabel, "Connected");
      ESPUI.updateLabel(ipLabel, WiFi.localIP().toString());
    } else {
      ESPUI.updateLabel(wifiStatusLabel, "Disconnected");
    }

    if (currentMode == MODE_PLAYLIST) {
      Playlist& pl = playlists[selectedPlaylist];
      unsigned long elapsed = (now - entryStartTime) / 1000;
      unsigned long total = pl.entries[currentEntry].durationMs / 1000;
      if (elapsed > total) elapsed = total;
      int progIdx = pl.entries[currentEntry].programIndex;
      char buf2[96];
      snprintf(buf2, sizeof(buf2), "&#9654; %s [%d/%d] %s  %lus / %lus",
        pl.name, currentEntry + 1, pl.numEntries, programs[progIdx].name, elapsed, total);
      String status = String(buf2) + "<br>" + buildProgramStatus(progIdx, "");
      ESPUI.print(programStatusLabel, status);
    } else if (currentMode == MODE_PROGRAM) {
      ESPUI.print(programStatusLabel, buildProgramStatus(selectedProgram));
    }
  }
}