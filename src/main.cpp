#include <Arduino.h>
#include <WiFi.h>
#include <ESPUI.h>
#include <PCA9539.h>
#include "programs.h"
#include "playlists.h"

// WiFi settings — connect to existing network
const char* ssid = "HMS Honeypot";
const char* password = "cutebutdangerous";

// PCA9539 I2C GPIO expander for solenoids
PCA9539 pca9539(0x77);
const int NUM_SOLENOIDS = 12;
bool solenoidStates[NUM_SOLENOIDS] = {};

// Modulator — solenoid that doubles flow when active (inverted pin logic)
const int MODULATOR_PIN = LED_BUILTIN;
bool modulatorState = false;

// Test sequence state
bool testRunning = false;
int testIndex = 0;
unsigned long testNextTime = 0;
const int TEST_ON_MS = 500;
const int TEST_OFF_MS = 300;
bool testPhaseOn = true;

// ESPUI control IDs
uint16_t uptimeLabel;
uint16_t ipLabel;
uint16_t wifiStatusLabel;
uint16_t solenoidSwitches[NUM_SOLENOIDS];
uint16_t modulatorSwitch;
uint16_t testButton;

// Program state
int selectedProgram = 0;
bool programRunning = false;
uint16_t programSelector;
uint16_t programStartStopBtn;
uint16_t programStatusLabel;
uint16_t paramSliders[MAX_PARAMS];

// Playlist state
int selectedPlaylist = 0;
bool playlistRunning = false;
int currentEntry = 0;
unsigned long entryStartTime = 0;

// Playlist editor state
int editEntry = 0;  // cursor into the selected playlist's entries

// Playlist ESPUI control IDs
uint16_t playlistSelector;
uint16_t playlistStartStopBtn;
uint16_t playlistStatusLabel;
uint16_t plEntryListLabel;
uint16_t plNewNameInput;
uint16_t plAddProgramSelect;
uint16_t plAddDurationInput;
uint16_t plEditDurationSlider;
uint16_t plEntryParamSliders[MAX_PARAMS];

unsigned long lastUiUpdate = 0;
bool playlistUIDirty = false;  // deferred UI refresh flag
bool programUIDirty = false;   // deferred program status refresh

// Build HTML status string with program name and all params
String buildProgramStatus(int progIdx, const char* prefix = "&#9654; ") {
  Program& p = programs[progIdx];
  String s = prefix;
  s += p.name;
  for (int i = 0; i < p.numParams; i++) {
    s += "<br>&nbsp;&nbsp;";
    s += p.params[i].name;
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
  pca9539.digitalWrite(index, state ? HIGH : LOW);
}

void setModulator(bool state) {
  modulatorState = state;
  digitalWrite(MODULATOR_PIN, state ? LOW : HIGH);  // inverted
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
      ESPUI.updateSlider(paramSliders[i], (int)p.params[i].value);
      ESPUI.updateVisibility(paramSliders[i], true);
      // Update slider label text via the control
      ESPUI.getControl(paramSliders[i])->label = p.params[i].name;
      ESPUI.updateControl(paramSliders[i]);
    } else {
      ESPUI.updateVisibility(paramSliders[i], false);
    }
  }
}

void stopCurrentProgram() {
  if (programRunning) {
    programs[selectedProgram].stop();
    programRunning = false;
  }
  allSolenoidsOff();
  setModulator(false);
}

void resetParamsToDefaults(int progIdx) {
  Program& p = programs[progIdx];
  for (int i = 0; i < p.numParams; i++) {
    p.params[i].value = p.params[i].defaultValue;
  }
}

void startProgram(int progIdx) {
  stopCurrentProgram();
  selectedProgram = progIdx;
  Program& p = programs[progIdx];
  showParamsForProgram(progIdx);
  p.init();
  programRunning = true;
  Serial.printf("Started program: %s\n", p.name);
}

// --- Program callbacks ---

void programSelectCallback(Control* sender, int type) {
  int idx = sender->value.toInt();
  selectedProgram = idx;
  resetParamsToDefaults(idx);
  showParamsForProgram(idx);
  Serial.printf("Selected program: %s\n", programs[idx].name);
}

void programStartStopCallback(Control* sender, int type) {
  if (type == B_DOWN) {
    if (programRunning) {
      stopCurrentProgram();
      ESPUI.print(programStatusLabel, "&#9632; Stopped");
      Serial.println("Program stopped");
    } else {
      startProgram(selectedProgram);
      ESPUI.print(programStatusLabel, buildProgramStatus(selectedProgram));
    }
  }
}

void paramSliderCallback(Control* sender, int type) {
  // Find which slider
  for (int i = 0; i < MAX_PARAMS; i++) {
    if (sender->id == paramSliders[i]) {
      float val = sender->value.toFloat();
      programs[selectedProgram].params[i].value = val;
      break;
    }
  }
  // Update status with current params if running
  if (programRunning) {
    programUIDirty = true;
  }
}

// --- Playlist helpers ---

// Build a text summary of the playlist entries
void updateEntryListLabel() {
  if (numPlaylists == 0) {
    ESPUI.print(plEntryListLabel, "(no playlists)");
    return;
  }
  Playlist& pl = playlists[selectedPlaylist];
  String text = "";
  for (int i = 0; i < pl.numEntries; i++) {
    if (i == editEntry)
      text += "<b>&#9654; " + String(i + 1) + ". " + programs[pl.entries[i].programIndex].name + " (" + String(pl.entries[i].durationMs / 1000) + "s)</b>";
    else
      text += "&nbsp;&nbsp;" + String(i + 1) + ". " + programs[pl.entries[i].programIndex].name + " (" + String(pl.entries[i].durationMs / 1000) + "s)";
    text += "<br>";
  }
  if (pl.numEntries == 0) text = "(empty)";
  ESPUI.print(plEntryListLabel, text);
}

void showEditEntryParams() {
  if (numPlaylists == 0) return;
  Playlist& pl = playlists[selectedPlaylist];
  if (editEntry < 0 || editEntry >= pl.numEntries) {
    ESPUI.getControl(plEditDurationSlider)->label = "Duration";
    ESPUI.updateControl(plEditDurationSlider);
    ESPUI.updateSlider(plEditDurationSlider, 0);
    for (int i = 0; i < MAX_PARAMS; i++) {
      ESPUI.updateVisibility(plEntryParamSliders[i], false);
    }
    return;
  }
  PlaylistEntry& e = pl.entries[editEntry];
  Program& prog = programs[e.programIndex];

  // Update duration slider label to show entry context
  static char durLabelBuf[48];
  snprintf(durLabelBuf, sizeof(durLabelBuf), "%d. %s — Duration (s)", editEntry + 1, prog.name);
  ESPUI.getControl(plEditDurationSlider)->label = durLabelBuf;
  ESPUI.updateControl(plEditDurationSlider);
  ESPUI.updateSlider(plEditDurationSlider, e.durationMs / 1000);

  for (int i = 0; i < MAX_PARAMS; i++) {
    if (i < prog.numParams) {
      float val = (e.paramValues[i] < 0) ? prog.params[i].defaultValue : e.paramValues[i];
      ESPUI.updateSlider(plEntryParamSliders[i], (int)val);
      ESPUI.getControl(plEntryParamSliders[i])->label = prog.params[i].name;
      ESPUI.updateControl(plEntryParamSliders[i]);
      ESPUI.updateVisibility(plEntryParamSliders[i], true);
    } else {
      ESPUI.updateVisibility(plEntryParamSliders[i], false);
    }
  }
}

void refreshPlaylistUI() {
  updateEntryListLabel();
  showEditEntryParams();
}

void rebuildPlaylistSelector() {
  // ESPUI doesn't support removing options, so we update the label text
  // The selector was built with MAX_PLAYLISTS options; show/hide via value
  // Actually ESPUI doesn't support dynamic option changes well.
  // We'll just update the status to remind user to reboot for selector changes.
  // The entry list label is the primary navigation.
}

void startPlaylistEntry(int entryIdx) {
  Playlist& pl = playlists[selectedPlaylist];
  currentEntry = entryIdx;
  entryStartTime = millis();
  PlaylistEntry& e = pl.entries[entryIdx];
  int progIdx = e.programIndex;

  // Apply per-entry param overrides
  Program& prog = programs[progIdx];
  for (int i = 0; i < prog.numParams; i++) {
    if (e.paramValues[i] >= 0) {
      prog.params[i].value = e.paramValues[i];
    } else {
      prog.params[i].value = prog.params[i].defaultValue;
    }
  }

  startProgram(progIdx);
  char buf[96];
  snprintf(buf, sizeof(buf), "&#9654; %s [%d/%d] %s  0s / %lus",
    pl.name, entryIdx + 1, pl.numEntries, programs[progIdx].name, e.durationMs / 1000);
  String plStatus = String(buf) + "<br>" + buildProgramStatus(progIdx, "");
  ESPUI.print(playlistStatusLabel, plStatus);
  ESPUI.print(programStatusLabel, buildProgramStatus(progIdx));
}

// --- Playlist callbacks ---

void playlistSelectCallback(Control* sender, int type) {
  int idx = sender->value.toInt();
  if (idx >= 0 && idx < numPlaylists) {
    selectedPlaylist = idx;
    editEntry = 0;
    playlistUIDirty = true;
    Serial.printf("Selected playlist: %s\n", playlists[idx].name);
  }
}

void playlistStartStopCallback(Control* sender, int type) {
  if (type == B_DOWN) {
    if (playlistRunning) {
      playlistRunning = false;
      stopCurrentProgram();
      ESPUI.print(playlistStatusLabel, "&#9632; Stopped");
      ESPUI.print(programStatusLabel, "&#9632; Stopped");
      Serial.println("Playlist stopped");
    } else {
      if (numPlaylists > 0 && playlists[selectedPlaylist].numEntries > 0) {
        playlistRunning = true;
        startPlaylistEntry(0);
        Serial.printf("Playlist started: %s\n", playlists[selectedPlaylist].name);
      }
    }
  }
}

void plNewPlaylistCallback(Control* sender, int type) {
  if (type == B_DOWN) {
    Control* nameCtrl = ESPUI.getControl(plNewNameInput);
    String name = nameCtrl->value;
    if (name.length() > 0 && numPlaylists < MAX_PLAYLISTS) {
      int idx = createNewPlaylist(name.c_str());
      if (idx >= 0) {
        selectedPlaylist = idx;
        editEntry = 0;
        playlistUIDirty = true;
        Serial.printf("Created playlist: %s\n", name.c_str());
      }
    }
  }
}

void plDeletePlaylistCallback(Control* sender, int type) {
  if (type == B_DOWN && numPlaylists > 0) {
    Serial.printf("Deleted playlist: %s\n", playlists[selectedPlaylist].name);
    deletePlaylistFromNVS(selectedPlaylist);
    if (selectedPlaylist >= numPlaylists) selectedPlaylist = numPlaylists - 1;
    if (selectedPlaylist < 0) selectedPlaylist = 0;
    editEntry = 0;
    playlistUIDirty = true;
  }
}

void plAddEntryCallback(Control* sender, int type) {
  if (type == B_DOWN && numPlaylists > 0) {
    Control* progCtrl = ESPUI.getControl(plAddProgramSelect);
    Control* durCtrl = ESPUI.getControl(plAddDurationInput);
    int progIdx = progCtrl->value.toInt();
    unsigned long dur = durCtrl->value.toInt() * 1000UL;
    if (dur < 1000) dur = 5000;
    playlistAddEntry(selectedPlaylist, progIdx, dur);
    playlistUIDirty = true;
  }
}

void plRemoveEntryCallback(Control* sender, int type) {
  if (type == B_DOWN && numPlaylists > 0) {
    Playlist& pl = playlists[selectedPlaylist];
    if (editEntry >= 0 && editEntry < pl.numEntries) {
      playlistRemoveEntry(selectedPlaylist, editEntry);
      if (editEntry >= pl.numEntries && pl.numEntries > 0) editEntry = pl.numEntries - 1;
      playlistUIDirty = true;
    }
  }
}

void plMoveUpCallback(Control* sender, int type) {
  if (type == B_DOWN && numPlaylists > 0 && editEntry > 0) {
    playlistMoveEntry(selectedPlaylist, editEntry, -1);
    editEntry--;
    playlistUIDirty = true;
  }
}

void plMoveDownCallback(Control* sender, int type) {
  if (type == B_DOWN && numPlaylists > 0) {
    Playlist& pl = playlists[selectedPlaylist];
    if (editEntry < pl.numEntries - 1) {
      playlistMoveEntry(selectedPlaylist, editEntry, 1);
      editEntry++;
      playlistUIDirty = true;
    }
  }
}

void plPrevEntryCallback(Control* sender, int type) {
  if (type == B_DOWN && editEntry > 0) {
    editEntry--;
    playlistUIDirty = true;
  }
}

void plNextEntryCallback(Control* sender, int type) {
  if (type == B_DOWN && numPlaylists > 0) {
    Playlist& pl = playlists[selectedPlaylist];
    if (editEntry < pl.numEntries - 1) {
      editEntry++;
      playlistUIDirty = true;
    }
  }
}

void plEditDurationCallback(Control* sender, int type) {
  if (numPlaylists == 0) return;
  Playlist& pl = playlists[selectedPlaylist];
  if (editEntry < 0 || editEntry >= pl.numEntries) return;
  int secs = sender->value.toInt();
  pl.entries[editEntry].durationMs = secs * 1000UL;
  savePlaylistToNVS(selectedPlaylist);
  playlistUIDirty = true;
}

void plEntryParamCallback(Control* sender, int type) {
  if (numPlaylists == 0) return;
  Playlist& pl = playlists[selectedPlaylist];
  if (editEntry < 0 || editEntry >= pl.numEntries) return;
  for (int i = 0; i < MAX_PARAMS; i++) {
    if (sender->id == plEntryParamSliders[i]) {
      float val = sender->value.toFloat();
      pl.entries[editEntry].paramValues[i] = val;
      savePlaylistToNVS(selectedPlaylist);
      break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Curl controller starting");

  // PCA9539 reset
  pinMode(D3, OUTPUT);
  digitalWrite(D3, HIGH);

  // Modulator pin (inverted: HIGH = off)
  pinMode(MODULATOR_PIN, OUTPUT);
  digitalWrite(MODULATOR_PIN, HIGH);

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

  // Load playlists from NVS (or create defaults)
  initPlaylists();

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
    Serial.println("WiFi connection failed — starting anyway, will keep trying");
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

  // Program selector dropdown
  programSelector = ESPUI.addControl(ControlType::Select, "Program",
    "0", ControlColor::Wetasphalt, programsTab, programSelectCallback);
  for (int i = 0; i < NUM_PROGRAMS; i++) {
    ESPUI.addControl(ControlType::Option, programs[i].name,
      String(i), ControlColor::Alizarin, programSelector);
  }

  // Start/Stop button
  programStartStopBtn = ESPUI.addControl(ControlType::Button, "Program Control", "START / STOP",
    ControlColor::Emerald, programsTab, programStartStopCallback);

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
    ESPUI.addControl(ControlType::Min, "", String((int)pMin), ControlColor::None, paramSliders[i]);
    ESPUI.addControl(ControlType::Max, "", String((int)pMax), ControlColor::None, paramSliders[i]);

    if (i >= programs[0].numParams) {
      ESPUI.updateVisibility(paramSliders[i], false);
    }
  }

  // --- Playlists tab ---
  uint16_t playlistsTab = ESPUI.addControl(ControlType::Tab, "Playlists", "Playlists");

  // Playlist selector + start/stop + status in one card
  playlistSelector = ESPUI.addControl(ControlType::Select, "Playlist",
    "0", ControlColor::Wetasphalt, playlistsTab, playlistSelectCallback);
  for (int i = 0; i < numPlaylists; i++) {
    ESPUI.addControl(ControlType::Option, playlists[i].name,
      String(i), ControlColor::Alizarin, playlistSelector);
  }
  playlistStartStopBtn = ESPUI.addControl(ControlType::Button, "", "START / STOP",
    ControlColor::Emerald, playlistSelector, playlistStartStopCallback);
  playlistStatusLabel = ESPUI.addControl(ControlType::Label, "Status",
    "Stopped", ControlColor::None, playlistSelector);

  // Entry list + nav buttons in one card
  plEntryListLabel = ESPUI.addControl(ControlType::Label, "Entries",
    "", ControlColor::Wetasphalt, playlistsTab);
  ESPUI.addControl(ControlType::Button, "", "\xe2\x97\x80 PREV",
    ControlColor::Peterriver, plEntryListLabel, plPrevEntryCallback);
  ESPUI.addControl(ControlType::Button, "", "NEXT \xe2\x96\xb6",
    ControlColor::Peterriver, plEntryListLabel, plNextEntryCallback);
  ESPUI.addControl(ControlType::Button, "", "\xe2\x96\xb2 UP",
    ControlColor::Carrot, plEntryListLabel, plMoveUpCallback);
  ESPUI.addControl(ControlType::Button, "", "\xe2\x96\xbc DN",
    ControlColor::Carrot, plEntryListLabel, plMoveDownCallback);
  ESPUI.addControl(ControlType::Button, "", "\xe2\x9c\x95 DEL",
    ControlColor::Alizarin, plEntryListLabel, plRemoveEntryCallback);

  // Duration slider (label updated dynamically to show entry context)
  plEditDurationSlider = ESPUI.addControl(ControlType::Slider, "Duration (s)",
    "20", ControlColor::Alizarin, playlistsTab, plEditDurationCallback);
  ESPUI.addControl(ControlType::Min, "", "1", ControlColor::None, plEditDurationSlider);
  ESPUI.addControl(ControlType::Max, "", "120", ControlColor::None, plEditDurationSlider);

  // Per-entry param sliders
  for (int i = 0; i < MAX_PARAMS; i++) {
    plEntryParamSliders[i] = ESPUI.addControl(ControlType::Slider, "",
      "0", ControlColor::Alizarin, playlistsTab, plEntryParamCallback);
    ESPUI.addControl(ControlType::Min, "", "0", ControlColor::None, plEntryParamSliders[i]);
    ESPUI.addControl(ControlType::Max, "", "100", ControlColor::None, plEntryParamSliders[i]);
    ESPUI.updateVisibility(plEntryParamSliders[i], false);
  }

  // Add entry: program select + duration + button in one card
  plAddProgramSelect = ESPUI.addControl(ControlType::Select, "Add Entry",
    "0", ControlColor::Wetasphalt, playlistsTab, [](Control*, int){});
  for (int i = 0; i < NUM_PROGRAMS; i++) {
    ESPUI.addControl(ControlType::Option, programs[i].name,
      String(i), ControlColor::Alizarin, plAddProgramSelect);
  }
  plAddDurationInput = ESPUI.addControl(ControlType::Number, "Duration (s)",
    "20", ControlColor::None, plAddProgramSelect, [](Control*, int){});
  ESPUI.addControl(ControlType::Min, "", "1", ControlColor::None, plAddDurationInput);
  ESPUI.addControl(ControlType::Max, "", "120", ControlColor::None, plAddDurationInput);
  ESPUI.addControl(ControlType::Button, "", "ADD",
    ControlColor::Emerald, plAddProgramSelect, plAddEntryCallback);

  // New / Delete playlist in one card
  plNewNameInput = ESPUI.addControl(ControlType::Text, "New Playlist",
    "", ControlColor::Wetasphalt, playlistsTab, [](Control*, int){});
  ESPUI.addControl(ControlType::Button, "", "CREATE",
    ControlColor::Emerald, plNewNameInput, plNewPlaylistCallback);
  ESPUI.addControl(ControlType::Button, "", "DELETE SELECTED",
    ControlColor::Alizarin, plNewNameInput, plDeletePlaylistCallback);

  // Initialize the entry list display
  refreshPlaylistUI();

  // Start ESPUI
  ESPUI.begin("Curl Controller");
  Serial.println("ESPUI started");
}

void loop() {
  unsigned long now = millis();

  // Deferred program status refresh
  if (programUIDirty) {
    programUIDirty = false;
    if (programRunning) {
      ESPUI.print(programStatusLabel, buildProgramStatus(selectedProgram));
    }
  }

  // Deferred playlist UI refresh (from callbacks)
  if (playlistUIDirty) {
    playlistUIDirty = false;
    refreshPlaylistUI();
  }

  // Run test sequence (non-blocking)
  if (testRunning && now >= testNextTime) {
    if (testPhaseOn) {
      setSolenoid(testIndex, true);
      ESPUI.updateSwitcher(solenoidSwitches[testIndex], true);
      Serial.printf("Test: Solenoid %d ON\n", testIndex);
      testNextTime = now + TEST_ON_MS;
      testPhaseOn = false;
    } else {
      setSolenoid(testIndex, false);
      ESPUI.updateSwitcher(solenoidSwitches[testIndex], false);
      Serial.printf("Test: Solenoid %d OFF\n", testIndex);
      testIndex++;
      if (testIndex >= NUM_SOLENOIDS) {
        testRunning = false;
        Serial.println("Test sequence complete");
      } else {
        testNextTime = now + TEST_OFF_MS;
        testPhaseOn = true;
      }
    }
  }

  // Run active program
  if (programRunning) {
    programs[selectedProgram].update(now);
  }

  // Playlist runner — advance to next entry when duration expires
  if (playlistRunning) {
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

    // Live playlist status
    if (playlistRunning) {
      Playlist& pl = playlists[selectedPlaylist];
      unsigned long elapsed = (now - entryStartTime) / 1000;
      unsigned long total = pl.entries[currentEntry].durationMs / 1000;
      if (elapsed > total) elapsed = total;
      int progIdx = pl.entries[currentEntry].programIndex;
      char buf[96];
      snprintf(buf, sizeof(buf), "&#9654; %s [%d/%d] %s  %lus / %lus",
        pl.name, currentEntry + 1, pl.numEntries, programs[progIdx].name, elapsed, total);
      String plStatus = String(buf) + "<br>" + buildProgramStatus(progIdx, "");
      ESPUI.print(playlistStatusLabel, plStatus);
      ESPUI.print(programStatusLabel, buildProgramStatus(progIdx));
    } else if (programRunning) {
      ESPUI.print(programStatusLabel, buildProgramStatus(selectedProgram));
    }
  }
}