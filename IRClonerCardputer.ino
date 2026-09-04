
#include <M5Cardputer.h>
#include <IRremote.hpp>
#include <FS.h>
#include <SD.h>
#include <LittleFS.h>
#include <math.h>
#include <algorithm>
#include <vector>
#include "splash_sprite.h"

// IR Cloner - stable single-file Cardputer build
// Libraries: M5Cardputer, IRremote
// TX/RX pins are configurable at runtime from the SETTINGS screen (persisted in NVS)
// Keys: ;=up  .=down  ,=left  /=right  Enter=select  `=back  S=save  R=replay  X=delete file

constexpr uint8_t DEFAULT_IR_TX_PIN = 44;
constexpr uint8_t DEFAULT_IR_RX_PIN = 1;
constexpr uint8_t MAX_GPIO = 48;  // ESP32-S3 highest usable GPIO number
constexpr uint8_t MAX_SIGNALS = 64;
constexpr uint8_t GRID_COLUMNS = 4;
constexpr uint8_t GRID_ROWS = 3;
constexpr uint8_t GRID_PAGE_SIZE = GRID_COLUMNS * GRID_ROWS;
uint8_t browserTop = 0;
uint32_t remoteTitleAt = 0;
uint16_t remoteTitleOffset = 0;
int8_t remoteTitleDirection = 1;
constexpr char BACK_KEY = '`';
constexpr char DELETE_KEY = 'd';
constexpr uint32_t CAPTURE_DEBOUNCE_MS = 450;
const char *IR_DIR = "/ircloner";

// Screen timeout choices in seconds; index 0 (0s) means "never dim"
constexpr uint16_t DIM_CHOICES[] = { 0, 15, 30, 60, 120, 300 };
constexpr uint8_t DIM_CHOICE_COUNT = sizeof(DIM_CHOICES) / sizeof(DIM_CHOICES[0]);
constexpr uint8_t MIN_BRIGHTNESS = 10;
constexpr uint8_t MAX_BRIGHTNESS = 255;
constexpr uint8_t BRIGHTNESS_STEP = 15;
constexpr uint32_t RAW_SEND_SPLIT_US = 60000;         // IRremote's raw buffer entries are uint16_t (<=65535us)
const char *CONFIG_PATH = "/IRCloner/config.config";  // JSON settings file at the root of the SD card
const char *APP_VERSION = "v1.0";

constexpr uint16_t UI_BG = 0x0841;
constexpr uint16_t UI_PINK = 0xF1B5;
constexpr uint16_t UI_CYAN = 0x07FF;
constexpr uint16_t UI_YELLOW = 0xFFE0;
constexpr uint16_t UI_WHITE = 0xFFFF;
constexpr uint16_t UI_DIM = 0x4A49;
constexpr uint16_t UI_RED = 0xF800;
constexpr uint16_t UI_GREEN = 0x07E0;


enum Screen : uint8_t {
  HOME,
  CAPTURE_REMOTE,
  REVIEW,
  EDIT_BUTTON,
  EDIT_REMOTE,
  SAVE_WHERE,
  LOAD_WHERE,
  BROWSER,
  REMOTE_GRID,
  SPAM_BROWSER,
  SPAM_SETUP,
  SPAM_RUNNING,
  SETTINGS,
  SYSTEM,
  ABOUT,
  CONFIRM_DELETE,
  MESSAGE
};

enum Store : uint8_t { STORE_SD,
                       STORE_LITTLEFS };

struct Signal {
  String name;
  String protocol;
  uint32_t address = 0;
  uint32_t command = 0;
  uint16_t bits = 0;
  bool isRaw = false;
  uint32_t frequency = 38000;     // Hz, from the .ir "frequency:" field
  std::vector<uint32_t> rawData;  // microsecond mark/space durations
};

struct Remote {
  String name;
  Signal signals[MAX_SIGNALS];
  uint8_t count = 0;
};

M5Canvas canvas(&M5Cardputer.Display);
Screen screen = HOME;
Screen returnScreen = HOME;
Store activeStore = STORE_LITTLEFS;

Remote workRemote;
Remote loadedRemote;

bool sdOK = false;
bool littlefsOK = false;
uint8_t menu = 0;
uint8_t selected = 0;
uint8_t gridIndex = 0;
uint8_t editIndex = 0;
String editText;
String files[32];
uint8_t fileCount = 0;
String messageTitle;
String messageDetail;

bool spamContinuous = true;
uint8_t spamTarget = 0;
uint8_t spamSignalIndex = 0;
uint16_t spamLimit = 100;
uint16_t spamSent = 0;
uint16_t spamDelay = 150;
uint32_t lastSpam = 0;
uint32_t animAt = 0;
uint8_t anim = 0;

String lastCapturedProtocol;
uint32_t lastCapturedAddress = 0;
uint32_t lastCapturedCommand = 0;
uint32_t lastCapturedAt = 0;

// A complete IR self-test needs the transmitter to be facing the receiver.
// The System screen runs this test on demand and only reports green after the
// receiver decodes one of the test frames.
bool irModuleOK = false;
bool irTestActive = false;
uint32_t irTestStartedAt = 0;
uint32_t lastIrTestSendAt = 0;

// --- Settings (persisted as JSON on the SD card, see CONFIG_PATH) ---
uint8_t irTxPin = DEFAULT_IR_TX_PIN;
uint8_t irRxPin = DEFAULT_IR_RX_PIN;
uint8_t screenBrightness = 80;
uint8_t dimIndex = 2;      // default 30s, see DIM_CHOICES
uint8_t settingsMenu = 0;  // 0=TX 1=RX 2=BRIGHT 3=TIMEOUT 4=LED
bool screenDimmed = false;
uint32_t lastActivity = 0;

// --- Delete confirmation ---
String deleteTarget;
Screen deleteReturnScreen = BROWSER;
uint8_t deleteMenu = 0;

bool wasUp = false;
bool wasDown = false;
bool wasLeft = false;
bool wasRight = false;
bool wasBack = false;
bool wasEnter = false;
bool wasSave = false;
bool wasReplay = false;
bool wasDelete = false;
std::vector<char> previousWord;

// Needed when this single-file sketch is built as C++ (for example, by
// PlatformIO) rather than through Arduino IDE's automatic prototype pass.
void keepBrowserSelectionVisible();

bool keyEdge(bool held, bool &previous) {
  bool pressed = held && !previous;
  previous = held;
  return pressed;
}

bool upPressed() {
  return keyEdge(M5Cardputer.Keyboard.isKeyPressed(';'), wasUp);
}
bool downPressed() {
  return keyEdge(M5Cardputer.Keyboard.isKeyPressed('.'), wasDown);
}
bool leftPressed() {
  return keyEdge(M5Cardputer.Keyboard.isKeyPressed(','), wasLeft);
}
bool rightPressed() {
  return keyEdge(M5Cardputer.Keyboard.isKeyPressed('/'), wasRight);
}
bool backPressed() {
  return keyEdge(M5Cardputer.Keyboard.isKeyPressed(BACK_KEY), wasBack);
}
bool enterPressed() {
  return keyEdge(M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER), wasEnter);
}
bool savePressed() {
  return keyEdge(M5Cardputer.Keyboard.isKeyPressed('s'), wasSave);
}
bool replayPressed() {
  return keyEdge(M5Cardputer.Keyboard.isKeyPressed('r'), wasReplay);
}
bool deletePressed() {
  return keyEdge(M5Cardputer.Keyboard.isKeyPressed(DELETE_KEY), wasDelete);
}

void syncKeys() {
  wasUp = M5Cardputer.Keyboard.isKeyPressed(';');
  wasDown = M5Cardputer.Keyboard.isKeyPressed('.');
  wasLeft = M5Cardputer.Keyboard.isKeyPressed(',');
  wasRight = M5Cardputer.Keyboard.isKeyPressed('/');
  wasBack = M5Cardputer.Keyboard.isKeyPressed(BACK_KEY);
  wasEnter = M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER);
  wasSave = M5Cardputer.Keyboard.isKeyPressed('s');
  wasReplay = M5Cardputer.Keyboard.isKeyPressed('r');
  wasDelete = M5Cardputer.Keyboard.isKeyPressed(DELETE_KEY);
}

bool menuWrap(uint8_t &index, uint8_t count) {
  if (!count) return false;
  if (upPressed()) {
    index = index == 0 ? count - 1 : index - 1;
    return true;
  }
  if (downPressed()) {
    index = index + 1 >= count ? 0 : index + 1;
    return true;
  }
  return false;
}

bool textInput(String &value, size_t maxLength) {
  if (!M5Cardputer.Keyboard.isChange()) return false;
  if (!M5Cardputer.Keyboard.isPressed()) {
    previousWord.clear();
    return false;
  }

  auto state = M5Cardputer.Keyboard.keysState();
  bool changed = false;
  if (state.del && value.length()) {
    value.remove(value.length() - 1);
    changed = true;
  }

  for (char c : state.word) {
    if (c == BACK_KEY) continue;
    bool alreadyHeld = std::find(previousWord.begin(), previousWord.end(), c) != previousWord.end();
    bool valid = isalnum((unsigned char)c) || c == ' ' || c == '_' || c == '-' || c == '+';
    if (!alreadyHeld && valid && value.length() < maxLength) {
      value += (char)toupper((unsigned char)c);
      changed = true;
    }
  }
  previousWord = state.word;
  return changed;
}

String storeName(Store store) {
  return store == STORE_SD ? "SD CARD" : "LITTLEFS";
}

FS *fsFor(Store store) {
  if (store == STORE_SD) return sdOK ? static_cast<FS *>(&SD) : nullptr;
  return littlefsOK ? static_cast<FS *>(&LittleFS) : nullptr;
}

String cleanName(String text, uint8_t maxLength = 24) {
  text.trim();
  String result;
  for (char c : text) {
    bool valid = isalnum((unsigned char)c) || c == ' ' || c == '_' || c == '-' || c == '+';
    if (valid && result.length() < maxLength) result += (char)toupper((unsigned char)c);
  }
  return result;
}

String filenameFor(String text) {
  String result;
  bool underscore = false;
  for (char c : text) {
    if (isalnum((unsigned char)c)) {
      result += (char)toupper((unsigned char)c);
      underscore = false;
    } else if (!underscore) {
      result += '_';
      underscore = true;
    }
  }
  while (result.endsWith("_")) result.remove(result.length() - 1);
  if (!result.length()) result = "REMOTE";
  return result.substring(0, min((int)result.length(), 24));
}

String hexLE(uint32_t value) {
  char output[12];
  snprintf(output, sizeof(output), "%02lX %02lX %02lX %02lX",
           (unsigned long)(value & 0xFF),
           (unsigned long)((value >> 8) & 0xFF),
           (unsigned long)((value >> 16) & 0xFF),
           (unsigned long)((value >> 24) & 0xFF));
  return String(output);
}

bool parseHexLE(const String &text, uint32_t &value) {
  unsigned a, b, c, d;
  if (sscanf(text.c_str(), "%x %x %x %x", &a, &b, &c, &d) != 4) return false;
  value = (a & 255) | ((b & 255) << 8) | ((c & 255) << 16) | ((d & 255) << 24);
  return true;
}

// Parses a space-separated list of microsecond durations, e.g. "2219 732 750 ..."
void parseRawData(const String &line, std::vector<uint32_t> &out) {
  int start = 0;
  int len = line.length();
  while (start < len) {
    while (start < len && line[start] == ' ') ++start;
    int end = start;
    while (end < len && line[end] != ' ') ++end;
    if (end > start) out.push_back((uint32_t)line.substring(start, end).toInt());
    start = end;
  }
}

String valueOf(const String &line) {
  int colon = line.indexOf(':');
  String value = colon < 0 ? "" : line.substring(colon + 1);
  value.trim();
  return value;
}

String protocolName(decode_type_t type) {
  switch (type) {
    case NEC: return "NEC";
    case NEC2: return "NEC2";
    case SONY: return "SONY";
    case SAMSUNG: return "SAMSUNG";
    case SAMSUNGLG: return "SAMSUNGLG";
    case LG: return "LG";
    case JVC: return "JVC";
    case PANASONIC: return "PANASONIC";
    case KASEIKYO: return "KASEIKYO";
    case RC5: return "RC5";
    case RC6: return "RC6";
    case ONKYO: return "ONKYO";
    default: return "";
  }
}

bool captureSignal(Signal &signal) {
  if (!IrReceiver.decode()) return false;

  auto &data = IrReceiver.decodedIRData;
  bool repeat = data.flags & IRDATA_FLAGS_IS_REPEAT;
  bool overflow = data.flags & IRDATA_FLAGS_WAS_OVERFLOW;
  String protocol = protocolName(data.protocol);
  IrReceiver.resume();

  if (repeat || overflow || !protocol.length()) return false;

  uint32_t now = millis();
  bool same = protocol == lastCapturedProtocol && data.address == lastCapturedAddress && data.command == lastCapturedCommand;
  if (same && now - lastCapturedAt < CAPTURE_DEBOUNCE_MS) return false;

  signal = Signal();
  signal.protocol = protocol;
  signal.address = data.address;
  signal.command = data.command;
  signal.bits = data.numberOfBits;

  lastCapturedProtocol = protocol;
  lastCapturedAddress = data.address;
  lastCapturedCommand = data.command;
  lastCapturedAt = now;
  return true;
}

// Sends a raw timing-capture signal (Flipper-style "type: raw" .ir entries).
// IRremote's sendRaw() takes a uint16_t buffer, so any single duration at or
// above ~65ms (the long pause Flipper leaves between repeated frames) is
// reproduced with an actual delay() between two shorter sendRaw() bursts
// instead of one giant buffer entry.
bool sendRawSignal(const Signal &signal) {
  if (signal.rawData.empty()) return false;
  uint16_t freqKHz = signal.frequency ? (uint16_t)(signal.frequency / 1000) : 38;

  std::vector<uint16_t> chunk;
  chunk.reserve(signal.rawData.size());
  size_t i = 0;
  while (i < signal.rawData.size()) {
    chunk.clear();
    while (i < signal.rawData.size() && signal.rawData[i] < RAW_SEND_SPLIT_US && chunk.size() < 255) {
      chunk.push_back((uint16_t)signal.rawData[i]);
      ++i;
    }
    if (!chunk.empty()) {
      IrSender.sendRaw(chunk.data(), (uint_fast8_t)chunk.size(), freqKHz);
    }
    if (i < signal.rawData.size() && signal.rawData[i] >= RAW_SEND_SPLIT_US) {
      delay(signal.rawData[i] / 1000);
      ++i;
    }
  }
  return true;
}

bool sendSignal(const Signal &signal) {
  if (signal.isRaw) return sendRawSignal(signal);
  const String &p = signal.protocol;
  if (p == "NEC" || p == "NEC2" || p == "ONKYO" || p == "NECEXT") IrSender.sendNEC((uint16_t)signal.address, (uint8_t)signal.command, 0);
  else if (p == "SONY") IrSender.sendSony((uint16_t)signal.command, signal.bits ? signal.bits : 12, 0);
  else if (p == "SAMSUNG" || p == "SAMSUNGLG" || p == "SAMSUNG32") IrSender.sendSamsung((uint16_t)signal.address, (uint8_t)signal.command, 0);
  else if (p == "LG") IrSender.sendLG((uint16_t)signal.address, (uint16_t)signal.command, 0);
  else if (p == "JVC") IrSender.sendJVC((uint8_t)signal.address, (uint8_t)signal.command, (int_fast8_t)0);
  else if (p == "PANASONIC" || p == "KASEIKYO") IrSender.sendPanasonic((uint16_t)signal.address, (uint16_t)signal.command, 0);
  else if (p == "RC5") IrSender.sendRC5((uint8_t)signal.address, (uint8_t)signal.command, 0);
  else if (p == "RC6") IrSender.sendRC6((uint8_t)signal.address, (uint8_t)signal.command, 0);
  else return false;
  return true;
}

void resetRemote(Remote &remote) {
  remote = Remote();
}

bool saveRemote(Store store, const Remote &remote, String &error) {
  FS *fs = fsFor(store);
  if (!fs) {
    error = "STORAGE NOT READY";
    return false;
  }
  if (!remote.count || !remote.name.length()) {
    error = "REMOTE EMPTY";
    return false;
  }
  if (!fs->exists(IR_DIR) && !fs->mkdir(IR_DIR)) {
    error = "CANNOT CREATE /IR";
    return false;
  }

  File file = fs->open(String(IR_DIR) + "/" + filenameFor(remote.name) + ".ir", FILE_WRITE);
  if (!file) {
    error = "OPEN FAILED";
    return false;
  }

  file.println("Filetype: IR signals file");
  file.println("Version: 1");
  file.println("#");
  file.printf("# %s\n", remote.name.c_str());
  file.println("#");

  for (uint8_t i = 0; i < remote.count; ++i) {
    const Signal &s = remote.signals[i];
    file.println("#");
    file.printf("name: %s\n", s.name.c_str());
    if (s.isRaw) {
      file.println("type: raw");
      file.printf("frequency: %lu\n", (unsigned long)s.frequency);
      file.println("duty_cycle: 0.330000");
      file.print("data:");
      for (size_t j = 0; j < s.rawData.size(); ++j) {
        file.print(' ');
        file.print(s.rawData[j]);
      }
      file.println();
    } else {
      file.println("type: parsed");
      file.printf("protocol: %s\n", s.protocol.c_str());
      file.printf("address: %s\n", hexLE(s.address).c_str());
      file.printf("command: %s\n", hexLE(s.command).c_str());
    }
  }

  file.close();
  return true;
}

bool loadRemote(Store store, const String &path, Remote &remote, String &error) {
  FS *fs = fsFor(store);
  if (!fs) {
    error = "STORAGE NOT READY";
    return false;
  }

  File file = fs->open(path, FILE_READ);
  if (!file) {
    error = "OPEN FAILED";
    return false;
  }

  resetRemote(remote);
  Signal current;
  bool active = false;
  bool headerOK = false;

  auto commit = [&]() {
    if (!active || !current.name.length()) return;
    bool validParsed = !current.isRaw && current.protocol.length();
    bool validRaw = current.isRaw && !current.rawData.empty();
    if ((validParsed || validRaw) && remote.count < MAX_SIGNALS) {
      remote.signals[remote.count++] = current;
    }
  };

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;

    if (line.startsWith("Filetype:")) {
      headerOK = valueOf(line).equalsIgnoreCase("IR signals file");
    } else if (line.startsWith("# ") && !active && !remote.name.length()) {
      remote.name = cleanName(line.substring(2));
    } else if (line.startsWith("name:")) {
      commit();
      current = Signal();
      active = true;
      current.name = cleanName(valueOf(line));
    } else if (active && line.startsWith("type:")) {
      String t = valueOf(line);
      t.toLowerCase();
      current.isRaw = (t == "raw");
    } else if (active && line.startsWith("protocol:")) {
      current.protocol = valueOf(line);
      current.protocol.toUpperCase();
    } else if (active && line.startsWith("address:")) {
      parseHexLE(valueOf(line), current.address);
    } else if (active && line.startsWith("command:")) {
      parseHexLE(valueOf(line), current.command);
    } else if (active && line.startsWith("frequency:")) {
      current.frequency = (uint32_t)valueOf(line).toInt();
    } else if (active && line.startsWith("data:")) {
      parseRawData(valueOf(line), current.rawData);
    }
  }

  commit();
  file.close();

  if (!headerOK) {
    error = "BAD .IR HEADER";
    return false;
  }
  if (!remote.count) {
    error = "NO USABLE SIGNALS";
    return false;
  }
  if (!remote.name.length()) remote.name = "REMOTE";
  return true;
}

void scanFiles(Store store) {
  fileCount = 0;
  FS *fs = fsFor(store);
  if (!fs || !fs->exists(IR_DIR)) return;

  File directory = fs->open(IR_DIR);
  if (!directory || !directory.isDirectory()) return;

  for (File file = directory.openNextFile(); file && fileCount < 32; file = directory.openNextFile()) {
    if (!file.isDirectory()) {
      String name = file.name();
      if ((name.endsWith(".ir") || name.endsWith(".IR")) && !name.startsWith("._")) {
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        files[fileCount++] = String(IR_DIR) + "/" + name;
      }
    }
    file.close();
  }
  directory.close();
  if (menu >= fileCount) {
    menu = fileCount ? fileCount - 1 : 0;
  }

  browserTop = 0;
  keepBrowserSelectionVisible();
}

// --- Settings persistence (SD card, JSON) & pin/display application ---
//
// Settings live in /IRCloner/config.config at the root of the SD card, e.g.:
//   { "txPin": 44, "rxPin": 1, "brightness": 80, "dimIndex": 2 }
//
// This is a tiny hand-rolled reader/writer rather than a JSON library, since
// we fully control the file's shape ourselves. If no SD card is present,
// settings simply live in RAM for the session and reset to defaults on the
// next boot (no LittleFS/NVS fallback, per how this was asked for).

void applyPins() {
  IrReceiver.begin(irRxPin, DISABLE_LED_FEEDBACK);
  IrSender.begin(irTxPin, DISABLE_LED_FEEDBACK);
  irModuleOK = false;  // pin changes require a fresh over-the-air self-test
}



void startIrSelfTest() {
  irModuleOK = false;
  irTestActive = true;
  irTestStartedAt = 0;  // causes an immediate test frame in updateIrSelfTest()
  lastIrTestSendAt = 0;
  IrReceiver.resume();
}

bool updateIrSelfTest() {
  if (!irTestActive) return false;
  uint32_t now = millis();
  if (!irTestStartedAt) irTestStartedAt = now;

  // A NEC address/command of zero is used only as a short diagnostic frame.
  // Point the configured transmitter at the configured receiver while testing.
  if (now - lastIrTestSendAt >= 220) {
    lastIrTestSendAt = now;
    IrSender.sendNEC(0x0000, 0x00, 0);
  }

  if (IrReceiver.decode()) {
    IrReceiver.resume();
    irModuleOK = true;
    irTestActive = false;
    return true;
  } else if (now - irTestStartedAt >= 1800) {
    irTestActive = false;
    return true;
  }
  return false;
}

// Finds "key": <int> in a small JSON blob and returns the integer, or fallback.
int jsonIntValue(const String &json, const String &key, int fallback) {
  String pattern = "\"" + key + "\"";
  int idx = json.indexOf(pattern);
  if (idx < 0) return fallback;
  int colon = json.indexOf(':', idx + pattern.length());
  if (colon < 0) return fallback;

  int i = colon + 1;
  while (i < (int)json.length() && isspace((unsigned char)json[i])) ++i;
  int start = i;
  if (i < (int)json.length() && (json[i] == '-' || json[i] == '+')) ++i;
  while (i < (int)json.length() && isdigit((unsigned char)json[i])) ++i;
  if (i == start) return fallback;
  return json.substring(start, i).toInt();
}

bool saveSettingsToSD() {
  if (!sdOK) return false;
  if (SD.exists(CONFIG_PATH)) SD.remove(CONFIG_PATH);
  File f = SD.open(CONFIG_PATH, FILE_WRITE);
  if (!f) return false;
  f.println("{");
  f.printf("  \"txPin\": %u,\n", irTxPin);
  f.printf("  \"rxPin\": %u,\n", irRxPin);
  f.printf("  \"brightness\": %u,\n", screenBrightness);
  f.printf("  \"dimIndex\": %u,\n", dimIndex);
  f.println("}");
  f.close();
  return true;
}

bool loadSettingsFromSD() {
  if (!sdOK || !SD.exists(CONFIG_PATH)) return false;
  File f = SD.open(CONFIG_PATH, FILE_READ);
  if (!f) return false;
  String json = f.readString();
  f.close();

  irTxPin = (uint8_t)constrain(jsonIntValue(json, "txPin", DEFAULT_IR_TX_PIN), 0, (int)MAX_GPIO);
  irRxPin = (uint8_t)constrain(jsonIntValue(json, "rxPin", DEFAULT_IR_RX_PIN), 0, (int)MAX_GPIO);
  screenBrightness = (uint8_t)constrain(jsonIntValue(json, "brightness", 80), (int)MIN_BRIGHTNESS, (int)MAX_BRIGHTNESS);
  dimIndex = (uint8_t)constrain(jsonIntValue(json, "dimIndex", 2), 0, (int)DIM_CHOICE_COUNT - 1);

  return true;
}

void loadSettings() {
  // In-RAM defaults; used whenever there's no SD card or no config file yet.
  irTxPin = DEFAULT_IR_TX_PIN;
  irRxPin = DEFAULT_IR_RX_PIN;
  screenBrightness = 80;
  dimIndex = 2;


  if (sdOK && loadSettingsFromSD()) return;

  // No config file yet (or no SD at all): write one out now so it exists
  // for next time, if a card happens to be present.
  if (sdOK) saveSettingsToSD();
}

void saveSettings() {
  // If there's no SD card, this is a no-op: the change still applies for
  // the rest of this session, it just won't survive a reboot.
  saveSettingsToSD();
}

void header(const String &title) {
  canvas.fillScreen(UI_BG);
  canvas.setTextDatum(top_center);
  canvas.setTextSize(2);
  canvas.setTextColor(UI_PINK, UI_BG);
  canvas.drawString(title, 120, 7);
  canvas.drawFastHLine(6, 32, 228, UI_YELLOW);
  canvas.setTextDatum(top_left);
  canvas.setTextSize(1);
}

void footer(const String &text) {
  canvas.drawFastHLine(6, 115, 228, UI_YELLOW);
  canvas.setTextDatum(top_center);
  canvas.setTextColor(UI_WHITE, UI_BG);
  canvas.drawString(text, 120, 121);
  canvas.setTextDatum(top_left);
}

void centered(const String &text, int y, uint16_t color, uint8_t size = 1) {
  canvas.setTextDatum(top_center);
  canvas.setTextSize(size);
  canvas.setTextColor(color, UI_BG);
  canvas.drawString(text, 120, y);
  canvas.setTextDatum(top_left);
  canvas.setTextSize(1);
}

void statusCircle(int x, int y, bool ok) {
  canvas.fillCircle(x, y, 5, ok ? UI_GREEN : UI_RED);
  canvas.drawCircle(x, y, 5, UI_WHITE);
}

void dashedRect(int x, int y, int w, int h, uint16_t color) {
  for (int i = 0; i < w; i += 7) {
    canvas.drawFastHLine(x + i, y, min(4, w - i), color);
    canvas.drawFastHLine(x + i, y + h - 1, min(4, w - i), color);
  }
  for (int i = 0; i < h; i += 7) {
    canvas.drawFastVLine(x, y + i, min(4, h - i), color);
    canvas.drawFastVLine(x + w - 1, y + i, min(4, h - i), color);
  }
}

void signalInfo(const Signal &signal, int y) {
  canvas.setTextColor(UI_WHITE, UI_BG);
  if (signal.isRaw) {
    canvas.drawString("TYPE : RAW", 12, y);
    canvas.drawString("FREQ : " + String(signal.frequency / 1000) + " KHZ", 12, y + 11);
    canvas.drawString("SAMPLES: " + String((int)signal.rawData.size()), 12, y + 22);
  } else {
    canvas.drawString("PROTO: " + signal.protocol, 12, y);
    canvas.drawString("ADDR : 0X" + String(signal.address, HEX), 12, y + 11);
    canvas.drawString("CMD  : 0X" + String(signal.command, HEX), 12, y + 22);
    canvas.drawString("BITS : " + String(signal.bits), 12, y + 33);
  }
}

void drawList(const String &title, const String items[], uint8_t count,
              uint8_t selectedIndex, const String &footerText,
              const String &subTitle = "") {
  header(title);

  if (subTitle.length()) {
    canvas.setTextColor(UI_YELLOW, UI_BG);
    canvas.drawString(subTitle, 12, 40);
  }

  // Six concise Home items fit cleanly above the footer; other lists retain
  // the original larger row spacing.
  int startY = subTitle.length() ? 54 : (count > 5 ? 38 : 41);
  int rowHeight = count > 5 ? 12 : 14;
  for (uint8_t i = 0; i < count && i < 6; ++i) {
    int y = startY + i * rowHeight;
    bool active = i == selectedIndex;
    if (active) canvas.fillRoundRect(10, y, 220, 12, 2, UI_CYAN);
    canvas.setTextColor(active ? UI_BG : UI_WHITE, active ? UI_CYAN : UI_BG);
    canvas.drawString(String(active ? "> " : "  ") + items[i], 19, y + 2);
  }

  footer(footerText);
}

void keepBrowserSelectionVisible() {
  constexpr uint8_t VISIBLE_FILES = 4;

  if (fileCount == 0) {
    browserTop = 0;
    return;
  }

  if (menu < browserTop) {
    browserTop = menu;
  }

  if (menu >= browserTop + VISIBLE_FILES) {
    browserTop = menu - VISIBLE_FILES + 1;
  }

  uint8_t maxTop = fileCount > VISIBLE_FILES
                     ? fileCount - VISIBLE_FILES
                     : 0;

  if (browserTop > maxTop) {
    browserTop = maxTop;
  }
}
void drawScrollingRemoteTitle(const String &title) {
  constexpr int TITLE_Y = 7;
  constexpr int TITLE_LEFT = 8;
  constexpr int TITLE_RIGHT = 232;
  constexpr int TITLE_WIDTH = TITLE_RIGHT - TITLE_LEFT;

  canvas.fillRect(0, 0, 240, 31, UI_BG);

  canvas.setTextSize(2);
  canvas.setTextColor(UI_PINK, UI_BG);

  int textWidth = canvas.textWidth(title);

  // Short titles behave exactly like the current centred title.
  if (textWidth <= TITLE_WIDTH) {
    canvas.setTextDatum(top_center);
    canvas.drawString(title, 120, TITLE_Y);
    canvas.setTextDatum(top_left);
    return;
  }

  // Long title: draw left-to-right as a marquee inside the header area.
  uint32_t now = millis();
  if (now - remoteTitleAt >= 55) {
    remoteTitleAt = now;

    int maxOffset = textWidth - TITLE_WIDTH;

    if (remoteTitleDirection > 0) {
      if (remoteTitleOffset < maxOffset) {
        ++remoteTitleOffset;
      } else {
        remoteTitleDirection = -1;
      }
    } else {
      if (remoteTitleOffset > 0) {
        --remoteTitleOffset;
      } else {
        remoteTitleDirection = 1;
      }
    }
  }

  canvas.setTextDatum(top_left);
  canvas.drawString(title, TITLE_LEFT - remoteTitleOffset, TITLE_Y);
}

void drawScreen() {
  if (screen == HOME) {
    const String items[] = { "CAPTURE REMOTE", "REMOTES", "SPAM SIGNALS", "SETTINGS", "SYSTEM", "ABOUT" };
    drawList("IR CLONER", items, 6, menu, "[ENTER] SELECT [ESC] BACK");
  }

  else if (screen == CAPTURE_REMOTE) {
    header("CAPTURE REMOTE");
    canvas.setTextColor(UI_YELLOW, UI_BG);
    canvas.drawString("CAPTURED " + String(workRemote.count) + " / " + String(MAX_SIGNALS), 12, 41);
    if (workRemote.count) signalInfo(workRemote.signals[workRemote.count - 1], 57);
    else {
      centered("AIM REMOTE AT RECEIVER", 65, UI_WHITE);
      centered("PRESS BUTTONS TO CAPTURE", 81, UI_WHITE);
    }
    footer("[S] Save [R] Replay [ESC] Cancel");
  }

  else if (screen == REVIEW) {
    String items[5];
    uint8_t shown = min(workRemote.count, (uint8_t)5);
    for (uint8_t i = 0; i < shown; ++i) items[i] = String(i + 1) + ". " + workRemote.signals[i].name;
    drawList("REVIEW REMOTE", items, shown, selected, "[ENTER] NAME [R] REPLAY [S] SAVE");
  }

  else if (screen == EDIT_BUTTON || screen == EDIT_REMOTE) {
    header(screen == EDIT_BUTTON ? "NAME BUTTON" : "SAVE REMOTE");
    canvas.setTextColor(UI_WHITE, UI_BG);
    canvas.drawString("TYPE NAME:", 12, 48);
    canvas.drawRoundRect(10, 63, 220, 22, 2, UI_CYAN);
    canvas.setTextColor(UI_YELLOW, UI_BG);
    canvas.drawString("> " + editText + "_", 16, 70);
    footer("[ENTER] Confirm [ESC] Cancel [DEL] Delete");
  }

  else if (screen == SAVE_WHERE || screen == LOAD_WHERE) {
    String items[] = {
      String("SD CARD") + (sdOK ? "" : " (NOT READY)"),
      String("LITTLEFS") + (littlefsOK ? "" : " (NOT READY)")
    };
    drawList(screen == SAVE_WHERE ? "SAVE LOCATION" : "LOAD LOCATION", items, 2, menu,
             "[ENTER] SELECT [ESC] BACK");
  }

  else if (screen == BROWSER || screen == SPAM_BROWSER) {
    String title = screen == BROWSER
                     ? "SAVED REMOTES"
                     : "SELECT SPAM REMOTE";

    if (!fileCount) {
      header(title);
      canvas.setTextColor(UI_YELLOW, UI_BG);
      canvas.drawString("SOURCE: " + storeName(activeStore), 12, 40);
      centered("NO .IR FILES FOUND", 70, UI_RED);
      footer("[ESC] Back");
    } else {
      header(title);

      canvas.setTextColor(UI_YELLOW, UI_BG);
      canvas.drawString("SOURCE: " + storeName(activeStore), 12, 40);

      keepBrowserSelectionVisible();

      constexpr uint8_t VISIBLE_FILES = 4;
      uint8_t shown = min((uint8_t)(fileCount - browserTop), VISIBLE_FILES);

      for (uint8_t row = 0; row < shown; ++row) {
        uint8_t fileIndex = browserTop + row;
        int y = 55 + row * 14;
        bool active = fileIndex == menu;

        String name = files[fileIndex];
        int slash = name.lastIndexOf('/');
        if (slash >= 0) {
          name = name.substring(slash + 1);
        }

        if (name.endsWith(".ir") || name.endsWith(".IR")) {
          name.remove(name.length() - 3);
        }

        // Leave room for the selection arrow and avoid overflowing.
        if (name.length() > 25) {
          name = name.substring(0, 25);
        }

        if (active) {
          canvas.fillRoundRect(10, y, 220, 12, 2, UI_CYAN);
        }

        canvas.setTextColor(
          active ? UI_BG : UI_WHITE,
          active ? UI_CYAN : UI_BG);

        canvas.drawString(
          String(active ? "> " : "  ") + name,
          19,
          y + 2);
      }


      footer("[ENTER] Load [D] Delete [ESC] Back");
    }
  }

  else if (screen == REMOTE_GRID) {
    header("");
    drawScrollingRemoteTitle(loadedRemote.name);
    canvas.drawFastHLine(6, 32, 228, UI_YELLOW);

    // Important: the scrolling title uses text size 2.
    // Reset button labels and footer to normal size.
    canvas.setTextSize(1);
    canvas.setTextDatum(top_left);

    uint8_t pageStart = (gridIndex / 12) * 12;

    for (uint8_t slot = 0; slot < 12; ++slot) {
      uint8_t signalIndex = pageStart + slot;

      int x = 7 + (slot % 4) * 57;
      int y = 39 + (slot / 4) * 22;

      bool active = signalIndex == gridIndex;
      bool full = signalIndex < loadedRemote.count;

      if (active) {
        canvas.drawRect(x, y, 53, 17, UI_CYAN);
      } else {
        dashedRect(x, y, 53, 17, full ? UI_PINK : UI_DIM);
      }

      if (full) {
        String name = loadedRemote.signals[signalIndex].name;

        // Eight visible characters; final dot means truncated.
        if (name.length() > 8) {
          name = name.substring(0, 7) + ".";
        }

        canvas.setTextSize(1);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(active ? UI_CYAN : UI_WHITE, UI_BG);
        canvas.drawString(name, x + 26, y + 9);

        // Restore defaults so the next UI operation is predictable.
        canvas.setTextDatum(top_left);
        canvas.setTextSize(1);
      }
    }

    String pageText = String(pageStart + 1) + "-" + String(min((int)pageStart + 12, (int)loadedRemote.count)) + " / " + String(loadedRemote.count);

    canvas.setTextDatum(top_center);
    canvas.setTextSize(1);
    canvas.setTextColor(UI_YELLOW, UI_BG);
    canvas.drawString(pageText, 120, 104);

    // Restore the normal drawing origin for any later UI elements.


    canvas.setTextDatum(top_left);
    canvas.setTextSize(1);

    footer("[ENTER] Send [ESC] Back");
  }

  else if (screen == SPAM_SETUP) {
    header("SPAM SETTINGS");

    String target = spamTarget == 0
                      ? "ALL SIGNALS"
                      : loadedRemote.signals[spamTarget - 1].name;

    String modeStr = spamContinuous ? "CONTINUOUS" : "COUNTED";

    const String labels[] = {
      "SEND:",
      "MODE:",
      "COUNT:",
      "DELAY:",
      "START TRANSMISSION"
    };

    String values[] = {
      "< " + target + " >",
      "< " + modeStr + " >",
      "",
      "< " + String(spamDelay) + " MS >",
      ""
    };

    // In continuous mode COUNT is not adjustable or applicable.
    if (!spamContinuous) {
      values[2] = "< " + String(spamLimit) + " >";
    }

    constexpr uint8_t SPAM_MENU_COUNT = 5;
    const int startY = 41;
    const int rowHeight = 14;

    for (uint8_t i = 0; i < SPAM_MENU_COUNT; ++i) {
      int y = startY + i * rowHeight;
      bool active = i == menu;
      bool countDisabled = spamContinuous && i == 2;
      bool isStart = i == 4;

      if (active) {
        canvas.fillRoundRect(10, y, 220, 12, 2, UI_CYAN);
      }

      // Start is an action rather than a label/value setting.
      if (isStart) {
        canvas.setTextDatum(top_center);
        canvas.setTextSize(1);
        canvas.setTextColor(active ? UI_BG : UI_WHITE,
                            active ? UI_CYAN : UI_BG);
        canvas.drawString("START TRANSMISSION", 120, y + 2);
        canvas.setTextDatum(top_left);
        continue;
      }

      // Left-side label.
      canvas.setTextDatum(top_left);
      canvas.setTextSize(1);
      canvas.setTextColor(
        active ? UI_BG : (countDisabled ? UI_DIM : UI_WHITE),
        active ? UI_CYAN : UI_BG);
      canvas.drawString(String(active ? "> " : "  ") + labels[i], 14, y + 2);

      // Right-side value. It includes its own < and > markers.
      if (!countDisabled) {
        canvas.setTextDatum(top_right);
        canvas.setTextColor(
          active ? UI_BG : UI_YELLOW,
          active ? UI_CYAN : UI_BG);
        canvas.drawString(values[i], 225, y + 2);
        canvas.setTextDatum(top_left);

      } else {
        // Disabled COUNT row—right align a clear non-editable indicator.
        canvas.setTextDatum(top_right);
        canvas.setTextColor(active ? UI_BG : UI_DIM,
                            active ? UI_CYAN : UI_BG);

        canvas.setTextDatum(top_left);
      }
    }

    footer("[< >] Change [ENTER] Start [ESC] Back");
  }

  else if (screen == SPAM_RUNNING) {
    header("SPAM SIGNALS");

    // Background grid
    for (int y = 40; y < 108; y += 10) {
      canvas.drawFastHLine(8, y, 224, 0x1082);
    }

    centered("TRANSMITTING", 45, UI_CYAN, 2);

    // Three animated squares
    const int size = 10, gap = 8;
    const int startX = (240 - (size * 3 + gap * 2)) / 2;
    for (uint8_t i = 0; i < 3; ++i) {
      canvas.fillRect(startX + i * (size + gap), 75, size, size,
                      i <= anim ? UI_YELLOW : UI_DIM);
    }

    bool all = spamTarget == 0;
    uint8_t shown = all ? spamSignalIndex : spamTarget - 1;
    if (all && spamSignalIndex == 0 && loadedRemote.count) {
      shown = loadedRemote.count - 1;
    }

    if (shown < loadedRemote.count) {
      String label = loadedRemote.signals[shown].name;
      centered(label, 96, UI_YELLOW);
    }

    bool finished = !spamContinuous && spamSent >= spamLimit;
    if (finished) {
      footer("[ENTER] Continue");
    } else {
      footer("[ESC] Stop Transmitting");
    }
  }

  else if (screen == SETTINGS) {
    header("SETTINGS");

    String timeoutLabel = DIM_CHOICES[dimIndex]
                            ? String(DIM_CHOICES[dimIndex]) + "S"
                            : "OFF";

    const String labels[] = {
      "TX PIN:",
      "RX PIN:",
      "BRIGHTNESS:",
      "SCREEN TIMEOUT:"
    };

    const String values[] = {
      "< GPIO " + String(irTxPin) + " >",
      "< GPIO " + String(irRxPin) + " >",
      "< " + String(screenBrightness) + " >",
      "< " + timeoutLabel + " >"
    };

    constexpr uint8_t SETTINGS_COUNT = 4;
    const int startY = 36;
    const int rowHeight = 12;

    for (uint8_t i = 0; i < SETTINGS_COUNT; ++i) {
      int y = startY + i * rowHeight;
      bool active = i == settingsMenu;

      if (active) {
        canvas.fillRoundRect(10, y, 220, 11, 2, UI_CYAN);
      }

      canvas.setTextDatum(top_left);
      canvas.setTextSize(1);
      canvas.setTextColor(
        active ? UI_BG : UI_WHITE,
        active ? UI_CYAN : UI_BG);
      canvas.drawString(String(active ? "> " : "  ") + labels[i], 14, y + 2);

      canvas.setTextDatum(top_right);
      canvas.setTextColor(
        active ? UI_BG : UI_YELLOW,
        active ? UI_CYAN : UI_BG);
      canvas.drawString(values[i], 225, y + 2);

      canvas.setTextDatum(top_left);
    }

    footer("[< >] Change [ESC] Back");
  }



  else if (screen == SYSTEM) {
    header("SYSTEM");
    canvas.setTextColor(UI_WHITE, UI_BG);

    canvas.drawString("SD CARD", 31, 38);
    canvas.setTextColor(sdOK ? UI_GREEN : UI_RED, UI_BG);
    canvas.setTextDatum(top_right);
    canvas.drawString(sdOK ? "MOUNTED" : "NOT MOUNTED", 225, 38);

    canvas.setTextDatum(top_left);
    canvas.setTextColor(UI_WHITE, UI_BG);
    canvas.drawString("IR MODULE", 31, 57);
    canvas.setTextColor(irModuleOK ? UI_GREEN : UI_RED, UI_BG);
    canvas.setTextDatum(top_right);
    canvas.drawString(irTestActive ? "TESTING..." : (irModuleOK ? "CONNECTED" : "NOT CONNECTED"), 225, 57);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(UI_WHITE, UI_BG);
    canvas.drawString("BATTERY", 31, 76);
    canvas.setTextDatum(top_right);
    canvas.setTextColor(UI_YELLOW, UI_BG);
    canvas.drawString(String(M5Cardputer.Power.getBatteryLevel()) + "%", 225, 76);
    canvas.setTextDatum(top_left);
    canvas.drawString("VERSION", 31, 95);
    canvas.setTextDatum(top_right);
    canvas.setTextColor(UI_YELLOW, UI_BG);
    canvas.drawString(APP_VERSION, 225, 95);
    canvas.setTextDatum(top_left);
    footer(irTestActive ? "POINT TX AT RX..." : "[ENTER] Test IR  [ESC] Back");
  }


  else if (screen == ABOUT) {
    header("ABOUT");
    centered("IR CLONER", 41, UI_YELLOW, 2);
    centered("Lorem ipsum dolor sit amet,", 65, UI_WHITE);
    centered("consectetur adipiscing elit.", 76, UI_WHITE);
    centered("Built for curious makers.", 91, UI_DIM);
    centered("CREDITS / APP INFO", 103, UI_DIM);
    footer("[ESC] Back");
  }

  else if (screen == CONFIRM_DELETE) {
    header("DELETE FILE?");
    String name = deleteTarget;
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    centered(name, 44, UI_YELLOW);

    const String items[] = { "CANCEL", "DELETE" };
    for (uint8_t i = 0; i < 2; ++i) {
      int y = 64 + i * 14;
      bool active = i == deleteMenu;
      uint16_t highlight = i == 1 ? UI_RED : UI_CYAN;
      if (active) {
        canvas.fillRoundRect(10, y, 220, 12, 2, highlight);
        canvas.setTextColor(UI_BG, highlight);
      } else {
        canvas.setTextColor(UI_WHITE, UI_BG);
      }
      canvas.drawString(String(active ? "> " : "  ") + items[i], 19, y + 2);
    }
    footer("[ENTER] Confirm [ESC] Cancel");
  }

  else if (screen == MESSAGE) {
    header(messageTitle);
    centered(messageDetail, 65, UI_CYAN);
    footer("[ENTER] Continue");
  }

  canvas.pushSprite(0, 0);
}

void showMessage(const String &title, const String &detail = "") {
  returnScreen = screen;
  messageTitle = title;
  messageDetail = detail;
  screen = MESSAGE;
  syncKeys();
}

void openEditor(bool button) {
  editText = button ? workRemote.signals[editIndex].name : workRemote.name;
  screen = button ? EDIT_BUTTON : EDIT_REMOTE;
  previousWord.clear();
  syncKeys();
}

void openLoad(bool spam) {
  returnScreen = spam ? SPAM_BROWSER : BROWSER;
  menu = 0;
  browserTop = 0;
  screen = LOAD_WHERE;
  syncKeys();
}

bool handleInput() {
  bool redraw = false;
  bool back = backPressed();

  if (screen == HOME) {


    redraw |= menuWrap(menu, 6);

    if (enterPressed()) {
      uint8_t choice = menu;
      menu = 0;

      if (choice == 0) {
        resetRemote(workRemote);
        lastCapturedProtocol = "";
        lastCapturedAddress = 0;
        lastCapturedCommand = 0;
        lastCapturedAt = 0;
        screen = CAPTURE_REMOTE;

      } else if (choice == 1) {
        openLoad(false);

      } else if (choice == 2) {
        openLoad(true);

      } else if (choice == 3) {
        settingsMenu = 0;
        screen = SETTINGS;
      } else if (choice == 4) {
        screen = SYSTEM;
        startIrSelfTest();
      } else {
        screen = ABOUT;
      }

      syncKeys();
      redraw = true;
    }
  }


  else if (screen == CAPTURE_REMOTE) {
    Signal captured;
    if (workRemote.count < MAX_SIGNALS && captureSignal(captured)) {
      captured.name = "BUTTON " + String(workRemote.count + 1);
      workRemote.signals[workRemote.count++] = captured;
      redraw = true;
    }
    if (replayPressed() && workRemote.count) {
      sendSignal(workRemote.signals[workRemote.count - 1]);
      redraw = true;
    }
    if (savePressed() && workRemote.count) {
      selected = 0;
      screen = REVIEW;
      syncKeys();
      redraw = true;
    }
    if (back) {
      screen = HOME;
      syncKeys();
      redraw = true;
    }
  }

  else if (screen == REVIEW) {
    redraw |= menuWrap(selected, workRemote.count);
    if (replayPressed() && workRemote.count) {
      sendSignal(workRemote.signals[selected]);
      redraw = true;
    }
    if (enterPressed() && workRemote.count) {
      editIndex = selected;
      openEditor(true);
      redraw = true;
    }
    if (savePressed() && workRemote.count) {
      openEditor(false);
      redraw = true;
    }
    if (back) {
      screen = CAPTURE_REMOTE;
      syncKeys();
      redraw = true;
    }
  }

  else if (screen == EDIT_BUTTON || screen == EDIT_REMOTE) {
    redraw |= textInput(editText, 24);
    if (back) {
      screen = REVIEW;
      syncKeys();
      redraw = true;
    } else if (enterPressed()) {
      editText = cleanName(editText);
      if (!editText.length()) {
        showMessage("NAME REQUIRED");
      } else if (screen == EDIT_BUTTON) {
        workRemote.signals[editIndex].name = editText;
        screen = REVIEW;
        syncKeys();
      } else {
        workRemote.name = editText;
        menu = 0;
        screen = SAVE_WHERE;
        syncKeys();
      }
      redraw = true;
    }
  }

  else if (screen == SAVE_WHERE || screen == LOAD_WHERE) {
    redraw |= menuWrap(menu, 2);
    if (back) {
      screen = screen == SAVE_WHERE ? EDIT_REMOTE : HOME;
      syncKeys();
      redraw = true;
    } else if (enterPressed()) {
      Store store = menu ? STORE_LITTLEFS : STORE_SD;
      if (!fsFor(store)) {
        showMessage("STORAGE NOT READY", storeName(store));
      } else if (screen == SAVE_WHERE) {
        String error;
        if (saveRemote(store, workRemote, error)) {
          screen = HOME;
          showMessage("REMOTE SAVED", storeName(store));
        } else {
          showMessage("SAVE FAILED", error);
        }
      } else {
        activeStore = store;
        scanFiles(store);
        menu = 0;
        screen = returnScreen;
        syncKeys();
      }
      redraw = true;
    }
  }

  else if (screen == BROWSER || screen == SPAM_BROWSER) {
    bool moved = menuWrap(menu, fileCount);

    if (moved) {
      keepBrowserSelectionVisible();
      redraw = true;
    }

    bool spam = screen == SPAM_BROWSER;
    if (deletePressed() && fileCount) {
      deleteTarget = files[menu];
      deleteReturnScreen = screen;
      deleteMenu = 0;
      screen = CONFIRM_DELETE;
      syncKeys();
      redraw = true;
    } else if (back) {
      menu = 0;
      screen = HOME;
      syncKeys();
      redraw = true;
    } else if (enterPressed() && fileCount) {
      String error;
      if (loadRemote(activeStore, files[menu], loadedRemote, error)) {
        if (spam) {
          spamTarget = 0;
          spamSignalIndex = 0;
          menu = 0;
          screen = SPAM_SETUP;
        } else {
          gridIndex = 0;
          remoteTitleOffset = 0;
          remoteTitleDirection = 1;
          remoteTitleAt = millis();
          screen = REMOTE_GRID;
        }
        syncKeys();
      } else {
        showMessage("LOAD FAILED", error);
      }
      redraw = true;
    }
  }

  else if (screen == CONFIRM_DELETE) {
    redraw |= menuWrap(deleteMenu, 2);
    if (back) {
      screen = deleteReturnScreen;
      syncKeys();
      redraw = true;
    } else if (enterPressed()) {
      if (deleteMenu == 1) {
        FS *fs = fsFor(activeStore);
        bool ok = fs && fs->remove(deleteTarget);
        scanFiles(activeStore);
        screen = deleteReturnScreen;
        showMessage(ok ? "FILE DELETED" : "DELETE FAILED");
      } else {
        screen = deleteReturnScreen;
        syncKeys();
      }
      redraw = true;
    }
  }

  else if (screen == REMOTE_GRID) {
    uint16_t count = loadedRemote.count;
    if (leftPressed() && gridIndex > 0) {
      --gridIndex;
      redraw = true;
    }
    if (rightPressed() && (uint16_t)(gridIndex + 1) < count) {
      ++gridIndex;
      redraw = true;
    }
    if (upPressed() && gridIndex >= GRID_COLUMNS) {
      gridIndex -= GRID_COLUMNS;
      redraw = true;
    }
    if (downPressed() && (uint16_t)(gridIndex + GRID_COLUMNS) < count) {
      gridIndex += GRID_COLUMNS;
      redraw = true;
    }
    if (back) {
      screen = BROWSER;
      syncKeys();
      redraw = true;
    } else if ((enterPressed() || replayPressed()) && gridIndex < count) {
      if (!sendSignal(loadedRemote.signals[gridIndex])) showMessage("SEND FAILED");
      redraw = true;
    }
    if (canvas.textWidth(loadedRemote.name) > 224) {
      redraw = true;
    }
  }

  else if (screen == SPAM_SETUP) {
    // Navigation with skip of disabled COUNT line (index 2) when continuous
    bool moved = false;

    if (upPressed()) {
      int8_t next = (int8_t)menu - 1;
      if (next < 0) next = 4;
      if (spamContinuous && next == 2) next = 1;
      menu = (uint8_t)next;
      moved = true;
    }

    if (downPressed()) {
      int8_t next = (int8_t)menu + 1;
      if (next > 4) next = 0;
      if (spamContinuous && next == 2) next = 3;
      menu = (uint8_t)next;
      moved = true;
    }

    if (moved) {
      redraw = true;
    }

    bool left = leftPressed();
    bool right = rightPressed();

    if (left || right) {
      if (menu == 0) {
        // SEND: target
        if (right) {
          spamTarget = spamTarget >= loadedRemote.count ? 0 : spamTarget + 1;
        } else {
          spamTarget = spamTarget == 0 ? loadedRemote.count : spamTarget - 1;
        }
        redraw = true;
      } else if (menu == 1) {
        // MODE
        spamContinuous = !spamContinuous;
        redraw = true;
      } else if (menu == 2) {
        // COUNT (only when not continuous)
        if (!spamContinuous) {
          spamLimit = constrain((int)spamLimit + (right ? 10 : -10), 1, 9999);
          redraw = true;
        }
      } else if (menu == 3) {
        // DELAY
        spamDelay = constrain((int)spamDelay + (right ? 10 : -10), 20, 5000);
        redraw = true;
      }
      // menu == 4 is "START SENDING" – no left/right change
    }

    if (back) {
      menu = 0;
      screen = SPAM_BROWSER;
      syncKeys();
      redraw = true;
    } else if (enterPressed() && menu == 4) {
      spamSent = 0;
      spamSignalIndex = 0;
      lastSpam = 0;
      animAt = millis();
      screen = SPAM_RUNNING;
      syncKeys();
      redraw = true;
    }
  }

  else if (screen == SPAM_RUNNING) {
    bool finished = !spamContinuous && spamSent >= spamLimit;

    // Back stops transmission only if not finished
    if (back && !finished) {
      menu = 0;
      screen = SPAM_SETUP;
      syncKeys();
      redraw = true;
      return redraw;
    }

    // If finished, only Enter returns to setup; back is ignored
    if (finished) {
      if (enterPressed()) {
        menu = 0;
        screen = SPAM_SETUP;
        syncKeys();
        redraw = true;
      }
      // Always redraw so the "finished" screen stays visible
      redraw = true;
      return redraw;
    }

    // Still running: update animation and send signals
    uint32_t now = millis();
    if (now - animAt >= 180) {
      animAt = now;
      anim = (anim + 1) % 3;
      redraw = true;
    }

    if (now - lastSpam >= spamDelay && loadedRemote.count) {
      lastSpam = now;
      bool all = spamTarget == 0;
      uint8_t index = all ? spamSignalIndex : spamTarget - 1;
      if (index < loadedRemote.count) {

        sendSignal(loadedRemote.signals[index]);
      }

      if (all) {
        spamSignalIndex = spamSignalIndex + 1 >= loadedRemote.count ? 0 : spamSignalIndex + 1;
        if (spamSignalIndex == 0) ++spamSent;
      } else {
        ++spamSent;
      }

      // Mark for redraw each time we send
      redraw = true;
    }

    // Ensure we keep redrawing while running
    redraw = true;
  }

  else if (screen == SETTINGS) {
    if (upPressed()) {
      settingsMenu = settingsMenu == 0 ? 3 : settingsMenu - 1;
      redraw = true;
    }

    if (downPressed()) {
      settingsMenu = settingsMenu >= 3 ? 0 : settingsMenu + 1;
      redraw = true;
    }

    bool left = leftPressed();
    bool right = rightPressed();

    if (left || right) {
      if (settingsMenu == 0) {
        int dir = right ? 1 : -1;
        uint8_t next = irTxPin;

        for (uint8_t tries = 0; tries <= MAX_GPIO; ++tries) {
          next = (uint8_t)(((int)next + dir + MAX_GPIO + 1) % (MAX_GPIO + 1));

          if (next != irRxPin) {
            break;
          }
        }

        irTxPin = next;
        applyPins();
        saveSettings();
        redraw = true;

      } else if (settingsMenu == 1) {
        int dir = right ? 1 : -1;
        uint8_t next = irRxPin;

        for (uint8_t tries = 0; tries <= MAX_GPIO; ++tries) {
          next = (uint8_t)(((int)next + dir + MAX_GPIO + 1) % (MAX_GPIO + 1));

          if (next != irTxPin) {
            break;
          }
        }

        irRxPin = next;
        applyPins();
        saveSettings();
        redraw = true;

      } else if (settingsMenu == 2) {
        int value = (int)screenBrightness + (right ? BRIGHTNESS_STEP : -BRIGHTNESS_STEP);

        screenBrightness = (uint8_t)constrain(
          value,
          (int)MIN_BRIGHTNESS,
          (int)MAX_BRIGHTNESS);

        if (!screenDimmed) {
          M5Cardputer.Display.setBrightness(screenBrightness);
        }

        saveSettings();
        redraw = true;

      } else if (settingsMenu == 3) {
        int value = (int)dimIndex + (right ? 1 : -1);

        dimIndex = (uint8_t)constrain(
          value,
          0,
          (int)DIM_CHOICE_COUNT - 1);

        saveSettings();
        redraw = true;
      }
    }

    if (back || enterPressed()) {
      screen = HOME;
      syncKeys();
      redraw = true;
    }
  }

  else if (screen == SYSTEM) {
    if (enterPressed()) {
      startIrSelfTest();
      redraw = true;
    }
    if (back) {
      screen = HOME;
      syncKeys();
      redraw = true;
    }
  }

  else if (screen == ABOUT) {
    if (back || enterPressed()) {
      screen = HOME;
      syncKeys();
      redraw = true;
    }
  }

  else if (screen == MESSAGE) {
    if (enterPressed() || back) {
      screen = returnScreen;
      syncKeys();
      redraw = true;
    }
  }

  return redraw;
}

// Copies the splash sprite pixel-by-pixel, skipping any pixel that matches
// splashAlpha (the magic "transparent" color the PNG was exported with).

void showSplash() {
  canvas.fillSprite(0x0000);  // black background behind the transparent areas
  for (int y = 0; y < splashHeight; ++y) {
    for (int x = 0; x < splashWidth; ++x) {
      uint16_t color = splashScreen[y * splashWidth + x];
      if (color == splashAlpha) continue;
      canvas.drawPixel(x, y, color);
    }
  }
  canvas.pushSprite(0, 0);
  while (true) {
    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isPressed()) {
      break;
    }

    delay(10);
  }
}


void setup() {
  auto config = M5.config();
  M5Cardputer.begin(config, true);
  M5Cardputer.Display.setRotation(1);


  canvas.setColorDepth(16);
  canvas.createSprite(240, 135);
  canvas.setTextWrap(false);

  littlefsOK = LittleFS.begin(true);
  sdOK = SD.begin();
  if (littlefsOK && !LittleFS.exists(IR_DIR)) LittleFS.mkdir(IR_DIR);
  if (sdOK && !SD.exists(IR_DIR)) SD.mkdir(IR_DIR);

  loadSettings();
  M5Cardputer.Display.setBrightness(screenBrightness);
  lastActivity = millis();
  applyPins();

  showSplash();

  syncKeys();
  drawScreen();
}

void loop() {
  M5Cardputer.update();


  bool irTestChanged = updateIrSelfTest();

  bool anyKey = M5Cardputer.Keyboard.isPressed();
  if (anyKey) {
    lastActivity = millis();
    if (screenDimmed) {
      M5Cardputer.Display.setBrightness(screenBrightness);
      screenDimmed = false;
    }
  } else if (!screenDimmed && DIM_CHOICES[dimIndex] && millis() - lastActivity > (uint32_t)DIM_CHOICES[dimIndex] * 1000UL) {
    M5Cardputer.Display.setBrightness(0);
    screenDimmed = true;
  }

  bool redraw = handleInput();

  if (redraw || irTestChanged || (screen == SYSTEM && irTestActive)) drawScreen();
  delay(12);
}
