
#include <M5Cardputer.h>
#include <IRremote.hpp>
#include <FS.h>
#include <SD.h>
#include <LittleFS.h>
#include <math.h>
#include <algorithm>
#include <vector>
#include "config.h"
#include "splash_sprite.h"

// IR Cloner - stable single-file Cardputer build
// Libraries: M5Cardputer, IRremote
// TX/RX pins are configurable at runtime from the SETTINGS screen (persisted in NVS)
// Keys: ;=up  .=down  ,=left  /=right  Enter=select  `=back  S=save  R=replay  D=delete file  Fn+S=screenshot
// Hotkeys: a button can carry a hotkey, saved in the .ir file as a comment line under its name:
//   name: Power
//   # hotkey: ENT
// Remotes with at least one hotkey open in the compact key list (press a key to send, TAB = next page).
// Remotes with none open in the original button grid. ESC, DEL and TAB can never be hotkeys.


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
  REMOTE_LIST,
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
  String hotkey;                  // "" = none, else ENT UP DN LT RT SPC or one of A-Z 0-9
  bool hotkeyAuto = false;        // true = handed out automatically on load, never written back
};

struct Remote {
  String name;
  Signal signals[MAX_SIGNALS];
  uint8_t count = 0;
  bool hasHotkeys = false;  // at least one explicit hotkey: open in the compact list instead of the grid
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

// --- Hotkeys / compact remote list / marquee ---
constexpr uint8_t LIST_PAGE_SIZE = 21;  // compact list: 3 columns x 7 rows
constexpr uint16_t UI_GREY = 0x8C71;    // hints and automatically assigned keys
uint8_t listPage = 0;
uint8_t flashIndex = 255;               // button that was just sent (drawn cyan briefly)
uint32_t flashUntil = 0;
uint8_t editField = 0;                  // EDIT_BUTTON: 0 = name, 1 = hotkey
bool hotkeyCapture = false;             // waiting for the key to assign
int8_t hotkeyConflict = -1;             // index of the button already using pendingKey
String editHotkey;
String pendingKey;
bool editingSaved = false;             // REVIEW/EDIT_* are editing a remote loaded from storage
Store editStore = STORE_SD;             // where that remote lives
String editPath;                        // its file, so saving overwrites it in place
String editOrigName;                    // its name when loaded (a rename saves a new file)
bool keysChanged = false;               // Keyboard.isChange() answered once per loop() pass (it is stateful)
bool marqueeActive = false;             // set by drawMarquee() while something is scrolling
uint32_t marqueeEpoch = 0;              // scrolling restarts from the left when this is reset
String files[32];
uint8_t fileCount = 0;
uint8_t reviewTop = 0;   // first visible row of the REVIEW (rename) list's scroll window
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
bool wasEdit = false;
bool wasScreenshot = false;
bool wasTab = false;
std::vector<char> previousWord;
std::vector<char> previousHotkeys;  // physical (unshifted) keys held, for hotkey capture

// --- Screenshot capture (Fn+S) ---
uint16_t screenshotCounter = 0;

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
bool editPressed() {
  return keyEdge(M5Cardputer.Keyboard.isKeyPressed('e'), wasEdit);
}
bool deletePressed() {
  return keyEdge(M5Cardputer.Keyboard.isKeyPressed(DELETE_KEY), wasDelete);
}
// Fn+S, checked separately from the plain 's' used by savePressed() so the
// two don't both fire off the same physical keypress - see its use in loop().
bool screenshotPressed() {
  bool held = M5Cardputer.Keyboard.keysState().fn && M5Cardputer.Keyboard.isKeyPressed('s');
  return keyEdge(held, wasScreenshot);
}
bool tabPressed() {
  return keyEdge(M5Cardputer.Keyboard.keysState().tab, wasTab);
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
  wasEdit = M5Cardputer.Keyboard.isKeyPressed('e');
  wasScreenshot = M5Cardputer.Keyboard.keysState().fn && M5Cardputer.Keyboard.isKeyPressed('s');
  wasTab = M5Cardputer.Keyboard.keysState().tab;
  marqueeEpoch = millis();  // every screen change restarts any scrolling text
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
  if (!keysChanged) return false;
  if (!M5Cardputer.Keyboard.isPressed()) {
    previousWord.clear(); previousHotkeys.clear();
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

// --- Hotkeys ---------------------------------------------------------------
// A hotkey is stored as a short canonical name: ENT UP DN LT RT SPC, or a single
// A-Z / 0-9. UP / DN / LT / RT are the ; . , / keys (the Cardputer's arrow keys).
// ESC (`), DEL and TAB are reserved and can never be hotkeys.

// Every key that can be a hotkey, by its plain (unshifted) label. ` is Esc and is
// reserved; Tab, Del, Fn, Shift, Ctrl, Alt and Opt are not in this list either.
const char HOTKEY_KEYS[] = "1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./ ";

bool isHotkeyKey(char c) {
  return c != 0 && strchr(HOTKEY_KEYS, c) != nullptr;
}

// Accepts what a person might type into an .ir file by hand (case-insensitive)
// and returns the canonical name, or "" if it isn't a usable hotkey. Shifted
// symbols are accepted as the key they sit on (+ means the = key, { the [ key).
String normaliseHotkey(String text) {
  text.trim();
  text.toUpperCase();
  if (text == "ENTER" || text == "RETURN" || text == "ENT") return "ENT";
  if (text == "UP" || text == ";") return "UP";
  if (text == "DOWN" || text == "DN" || text == ".") return "DN";
  if (text == "LEFT" || text == "LT" || text == ",") return "LT";
  if (text == "RIGHT" || text == "RT" || text == "/") return "RT";
  if (text == "SPACE" || text == "SPC") return "SPC";
  if (text.length() != 1) return "";
  char c = text[0];
  switch (c) {
    case '+': c = '='; break;
    case '_': c = '-'; break;
    case '{': c = '['; break;
    case '}': c = ']'; break;
    case '|': c = '\\'; break;
    case '"': c = '\''; break;
    default: break;
  }
  if (c == ' ' || !isHotkeyKey((char)tolower((unsigned char)c))) return "";
  return String(c);
}

String hotkeyLongName(const String &key) {
  if (key == "ENT") return "ENTER";
  if (key == "DN") return "DOWN";
  if (key == "LT") return "LEFT";
  if (key == "RT") return "RIGHT";
  if (key == "SPC") return "SPACE";
  return key;
}

// Maps a physical key (its plain, unshifted character) to its hotkey name
// ("" = not usable as a hotkey).
String keyNameFromChar(char c) {
  switch (c) {
    case ';': return "UP";
    case '.': return "DN";
    case ',': return "LT";
    case '/': return "RT";
    case ' ': return "SPC";
    default: break;
  }
  char lower = (char)tolower((unsigned char)c);
  if (!isHotkeyKey(lower)) return "";
  if (isalpha((unsigned char)lower)) return String((char)toupper((unsigned char)lower));
  return String(lower);
}

// The keys currently held, as the plain character printed on each key. This reads
// the key positions, so Shift, Aa (caps) and Fn do not change what a key means:
// 1 stays "1" instead of turning into "!", [ stays "[" instead of "{".
std::vector<char> physicalKeys() {
  std::vector<char> keys;
  for (const auto &pos : M5Cardputer.Keyboard.keyList()) {
    char c = M5Cardputer.Keyboard.getKeyValue(pos).value_first;
    if (isHotkeyKey(c)) keys.push_back(c);
  }
  return keys;
}

// Returns the hotkey name of a key that has just been pressed, or "" if nothing
// new was pressed. Fires once per physical press (held keys don't repeat).
// While Fn is held only ; . , / count (they are the arrow keys), and every other
// key is still remembered as held, so Fn+S can't fire an S hotkey on release.
String newHotkeyPress() {
  if (enterPressed()) return "ENT";
  if (!keysChanged) return "";
  if (!M5Cardputer.Keyboard.isPressed()) {
    previousWord.clear(); previousHotkeys.clear();
    return "";
  }

  bool fn = M5Cardputer.Keyboard.keysState().fn;
  std::vector<char> now = physicalKeys();
  String result;
  for (char c : now) {
    bool alreadyHeld = std::find(previousHotkeys.begin(), previousHotkeys.end(), c) != previousHotkeys.end();
    if (alreadyHeld || result.length()) continue;
    if (fn && !strchr(";.,/", c)) continue;
    result = keyNameFromChar(c);
  }
  previousHotkeys = now;
  return result;
}

// Run after a remote is loaded. Explicit hotkeys win (the first button keeps a
// key that two buttons claim). If the remote has any explicit hotkey, every
// button that still has none is handed an automatic one (1-9, 0, then unused
// letters) so nothing becomes unreachable. A remote with no explicit hotkeys
// is left alone and opens in the original grid.
void finalizeHotkeys(Remote &remote) {
  remote.hasHotkeys = false;
  for (uint8_t i = 0; i < remote.count; ++i) {
    Signal &s = remote.signals[i];
    s.hotkeyAuto = false;
    if (!s.hotkey.length()) continue;
    bool duplicate = false;
    for (uint8_t j = 0; j < i; ++j) {
      if (remote.signals[j].hotkey == s.hotkey) duplicate = true;
    }
    if (duplicate) s.hotkey = "";
    else remote.hasHotkeys = true;
  }
  if (!remote.hasHotkeys) return;

  const char *pool = "1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  for (uint8_t i = 0; i < remote.count; ++i) {
    Signal &s = remote.signals[i];
    if (s.hotkey.length()) continue;
    for (const char *p = pool; *p; ++p) {
      String candidate = String(*p);
      bool used = false;
      for (uint8_t j = 0; j < remote.count; ++j) {
        if (remote.signals[j].hotkey == candidate) used = true;
      }
      if (!used) {
        s.hotkey = candidate;
        s.hotkeyAuto = true;
        break;
      }
    }
  }
}

// More protocols IRremote can decode and send with an address and a command.
// They are listed by name so a signal keeps working when it is saved and reloaded.
// All of these exist in IRremote 4.3 through 5.x, and IrSender.write() knows how to
// send each of them. (Left out on purpose: BANG_OLUFSEN, which write() cannot send,
// MAGIQUEST, whose 32-bit ID does not fit an address, and LEGO_PF and WHYNTER.)
struct ExtraProtocol {
  const char *name;
  decode_type_t type;
};
const ExtraProtocol EXTRA_PROTOCOLS[] = {
  {"APPLE", APPLE},
  {"DENON", DENON},
  {"SHARP", SHARP},
  {"SAMSUNG48", SAMSUNG48},
  {"KASEIKYO_DENON", KASEIKYO_DENON},
  {"KASEIKYO_SHARP", KASEIKYO_SHARP},
  {"KASEIKYO_JVC", KASEIKYO_JVC},
  {"KASEIKYO_MITSUBISHI", KASEIKYO_MITSUBISHI},
  {"BOSEWAVE", BOSEWAVE},
  {"FAST", FAST},
};

// Index into EXTRA_PROTOCOLS, or -1. (Returns an int, not a pointer to ExtraProtocol: the Arduino IDE
// puts generated function prototypes above this struct, where that type is not known yet.)
int extraProtocolIndex(const String &name) {
  for (size_t i = 0; i < sizeof(EXTRA_PROTOCOLS) / sizeof(EXTRA_PROTOCOLS[0]); ++i) {
    if (name == EXTRA_PROTOCOLS[i].name) return (int)i;
  }
  return -1;
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
    default: break;
  }
  for (const ExtraProtocol &x : EXTRA_PROTOCOLS) {
    if (x.type == type) return x.name;
  }
  return "";
}

// ---- Raw capture -------------------------------------------------------------
// A frame IRremote cannot turn into a protocol + address + command (the Dyson fan
// remotes, many air conditioners, ...) is recorded as raw mark/space timings instead,
// the same thing a Flipper stores as "type: raw". Many of these remotes send the same
// frame twice about 100 ms apart, so after the first frame we wait briefly for a second
// matching one and store both, with the measured gap between them.
constexpr uint32_t RAW_SECOND_FRAME_WAIT_MS = 250;   // how long to wait for a second frame
constexpr uint32_t RAW_REPEAT_SUPPRESS_MS = 600;     // a held button keeps sending: ignore those
constexpr uint32_t RAW_GAP_MIN_US = 10000;           // a second frame must follow after 10 ms ...
constexpr uint32_t RAW_GAP_MAX_US = 400000;          // ... to 400 ms
constexpr size_t RAW_MIN_ENTRIES = 12;               // shorter bursts are treated as noise

std::vector<uint32_t> rawPending;      // frame(s) waiting for a possible second frame
uint32_t rawPendingAt = 0;
uint8_t rawPendingFrames = 0;
size_t rawFirstFrameLength = 0;        // entries in the first frame (before any gap + second frame)
std::vector<uint32_t> lastRawFrame;    // first frame of the last delivered capture
uint32_t lastRawSeenAt = 0;

void resetRawCapture() {
  rawPending.clear();
  rawPendingFrames = 0;
  rawFirstFrameLength = 0;
  lastRawFrame.clear();
  lastRawSeenAt = 0;
}

// Two frames count as the same when every mark and space is within 25 % (+100 us).
bool framesSimilar(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, size_t bLength) {
  if (a.size() != bLength) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    uint32_t hi = std::max(a[i], b[i]);
    uint32_t lo = std::min(a[i], b[i]);
    if (hi - lo > hi / 4 + 100) return false;
  }
  return true;
}

// Reads the frame IRremote just received as microsecond marks and spaces (first value is a
// mark). Must run before IrReceiver.resume(), which lets the next frame overwrite the buffer.
// Returns the gap before this frame (time since the previous frame ended) in microseconds.
uint32_t readRawFrame(std::vector<uint32_t> &frame) {
  frame.clear();
  uint32_t rawlen = IrReceiver.irparams.rawlen;
  for (uint32_t i = 1; i < rawlen; ++i) {            // entry 0 is not part of the frame
    uint32_t us = (uint32_t)IrReceiver.irparams.rawbuf[i] * MICROS_PER_TICK;
    if (i & 1) us = us > MARK_EXCESS_MICROS ? us - MARK_EXCESS_MICROS : us;   // marks are stretched by the receiver
    else us += MARK_EXCESS_MICROS;                                          // spaces are shortened
    frame.push_back(us);
  }
#if defined(VERSION_IRREMOTE_MAJOR) && ((VERSION_IRREMOTE_MAJOR > 4) || (VERSION_IRREMOTE_MAJOR == 4 && VERSION_IRREMOTE_MINOR >= 4))
  return (uint32_t)IrReceiver.irparams.initialGapTicks * MICROS_PER_TICK;
#else
  return (uint32_t)IrReceiver.irparams.rawbuf[0] * MICROS_PER_TICK;   // IRremote 4.3 keeps the gap in entry 0
#endif
}

bool finishRawCapture(Signal &signal, uint32_t now) {
  signal = Signal();
  signal.isRaw = true;
  signal.frequency = 38000;
  signal.rawData = rawPending;
  lastRawFrame.assign(rawPending.begin(), rawPending.begin() + rawFirstFrameLength);
  lastRawSeenAt = now;
  rawPending.clear();
  rawPendingFrames = 0;
  rawFirstFrameLength = 0;
  return true;
}

// Called with every frame that has no protocol. Returns true when a finished raw capture is ready.
bool handleRawFrame(const std::vector<uint32_t> &frame, uint32_t gapUs, Signal &signal) {
  if (frame.size() < RAW_MIN_ENTRIES) return false;
  uint32_t now = millis();

  // The button is still held (or the remote repeats): not a new press.
  if (lastRawFrame.size() && now - lastRawSeenAt < RAW_REPEAT_SUPPRESS_MS &&
      framesSimilar(frame, lastRawFrame, lastRawFrame.size())) {
    lastRawSeenAt = now;
    return false;
  }

  if (rawPending.size()) {
    bool second = rawPendingFrames == 1 && gapUs >= RAW_GAP_MIN_US && gapUs <= RAW_GAP_MAX_US &&
                  framesSimilar(frame, rawPending, rawFirstFrameLength);
    if (second) {
      rawPending.push_back(gapUs);                   // the gap is a space between the two frames
      rawPending.insert(rawPending.end(), frame.begin(), frame.end());
      rawPendingFrames = 2;
      return finishRawCapture(signal, now);
    }
    // A different frame arrived: hand over the waiting one and start on the new one.
    bool ready = finishRawCapture(signal, now);
    rawPending = frame;
    rawPendingAt = now;
    rawPendingFrames = 1;
    rawFirstFrameLength = frame.size();
    return ready;
  }

  rawPending = frame;
  rawPendingAt = now;
  rawPendingFrames = 1;
  rawFirstFrameLength = frame.size();
  return false;
}

bool captureSignal(Signal &signal) {
  // A raw frame is waiting and no second frame came: that was the whole signal.
  if (rawPending.size() && millis() - rawPendingAt >= RAW_SECOND_FRAME_WAIT_MS) {
    return finishRawCapture(signal, millis());
  }

  if (!IrReceiver.decode()) return false;

  auto &data = IrReceiver.decodedIRData;
  bool repeat = data.flags & IRDATA_FLAGS_IS_REPEAT;
  bool overflow = data.flags & IRDATA_FLAGS_WAS_OVERFLOW;
  String protocol = protocolName(data.protocol);

  // No usable protocol: record it as raw timings (read before resume() frees the buffer).
  std::vector<uint32_t> rawFrame;
  uint32_t gapUs = 0;
  bool isRawFrame = !protocol.length() && !repeat && !overflow;
  if (isRawFrame) gapUs = readRawFrame(rawFrame);
  IrReceiver.resume();

  if (isRawFrame) return handleRawFrame(rawFrame, gapUs, signal);
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
  else {
    // The extra protocols go through IRremote's generic sender.
    int extraIndex = extraProtocolIndex(p);
    if (extraIndex < 0) return false;
    const ExtraProtocol *extra = &EXTRA_PROTOCOLS[extraIndex];
    IRData data;
    memset(&data, 0, sizeof(data));
    data.protocol = extra->type;
    data.address = (uint16_t)signal.address;
    data.command = (uint16_t)signal.command;
    data.numberOfBits = signal.bits;
    return IrSender.write(&data, 0) != 0;
  }
  return true;
}

void resetRemote(Remote &remote) {
  remote = Remote();
}

bool saveRemote(Store store, const Remote &remote, String &error, const String &pathOverride = "") {
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

  File file = fs->open(pathOverride.length() ? pathOverride : String(IR_DIR) + "/" + filenameFor(remote.name) + ".ir", FILE_WRITE);
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
    if (s.hotkey.length() && !s.hotkeyAuto) file.printf("# hotkey: %s\n", s.hotkey.c_str());
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
    } else if (line.startsWith("# ") && !active && !remote.name.length() && !line.startsWith("# hotkey:")) {
      remote.name = cleanName(line.substring(2));
    } else if (line.startsWith("name:")) {
      commit();
      current = Signal();
      active = true;
      current.name = cleanName(valueOf(line));
    } else if (active && (line.startsWith("# hotkey:") || line.startsWith("hotkey:"))) {
      current.hotkey = normaliseHotkey(valueOf(line));
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
  finalizeHotkeys(remote);
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

// Draws text inside a box maxW pixels wide. If it fits it is drawn normally
// (centred in the box if `center`). If it is too long it scrolls right-to-left
// and loops, pausing briefly at the start. Uses the current text size. The text
// restarts from the left whenever marqueeEpoch is reset (see syncKeys()).
void drawMarquee(const String &text, int x, int y, int maxW,
                 uint16_t fg, uint16_t bg, bool center) {
  canvas.setTextColor(fg, bg);
  int w = canvas.textWidth(text);
  canvas.setClipRect(x, y, maxW, canvas.fontHeight());
  if (w <= maxW) {
    canvas.setTextDatum(center ? top_center : top_left);
    canvas.drawString(text, center ? x + maxW / 2 : x, y);
  } else {
    marqueeActive = true;
    const int GAP = 24;         // blank pixels between the end and the repeat
    const uint32_t PAUSE_MS = 1000;
    const uint32_t PX_PER_SEC = 30;
    uint32_t period = (uint32_t)(w + GAP) * 1000 / PX_PER_SEC + PAUSE_MS;
    uint32_t t = (millis() - marqueeEpoch) % period;
    int offset = t < PAUSE_MS ? 0 : (int)((t - PAUSE_MS) * PX_PER_SEC / 1000);
    canvas.setTextDatum(top_left);
    canvas.drawString(text, x - offset, y);
    canvas.drawString(text, x - offset + w + GAP, y);  // the wrap-around copy
  }
  canvas.clearClipRect();
  canvas.setTextDatum(top_left);
}

// Static version for rows that are not in focus: cut to fit and add "..".
String fitText(String s, int maxW) {
  if (canvas.textWidth(s) <= maxW) return s;
  while (s.length() > 1 && canvas.textWidth(s + "..") > maxW) s.remove(s.length() - 1);
  return s + "..";
}

void header(const String &title) {
  canvas.fillScreen(UI_BG);
  canvas.setTextSize(2);
  drawMarquee(title, 6, 10, 228, UI_PINK, UI_BG, true);  // long remote names scroll
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

// Shared scroll-window math for any "selected row in a list longer than the
// screen" case. Keeps `currentTop` so that `selectedIndex` stays within the
// next `visibleCount` rows: scroll up as soon as the selection goes above the
// window, scroll down as soon as it goes below, and never scroll past the
// end of the list. The file browser and the REVIEW (rename) list both use
// this - previously only the browser had it, which is why REVIEW only ever
// showed its first few rows and scrolling further did nothing.
uint8_t clampScrollTop(uint8_t selectedIndex, uint8_t total, uint8_t currentTop, uint8_t visibleCount) {
  if (total == 0) return 0;

  uint8_t top = currentTop;
  if (selectedIndex < top) {
    top = selectedIndex;
  }
  if (selectedIndex >= (uint16_t)top + visibleCount) {
    top = selectedIndex - visibleCount + 1;
  }

  uint8_t maxTop = total > visibleCount ? total - visibleCount : 0;
  if (top > maxTop) {
    top = maxTop;
  }
  return top;
}

void keepBrowserSelectionVisible() {
  constexpr uint8_t VISIBLE_FILES = 4;
  browserTop = clampScrollTop(menu, fileCount, browserTop, VISIBLE_FILES);
}

void keepReviewSelectionVisible() {
  constexpr uint8_t VISIBLE_BUTTONS = 5;
  reviewTop = clampScrollTop(selected, workRemote.count, reviewTop, VISIBLE_BUTTONS);
}

void drawScreen() {
  marqueeActive = false;

  if (screen == HOME) {
    const String items[] = { "CAPTURE REMOTE", "REMOTES", "SPAM SIGNALS", "SETTINGS", "SYSTEM", "ABOUT" };
    drawList("IR CLONER", items, 6, menu, "[ENTER] Select");
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
    header(editingSaved ? "EDIT REMOTE" : "REVIEW REMOTE");

    keepReviewSelectionVisible();

    constexpr uint8_t VISIBLE_BUTTONS = 5;
    uint8_t shown = min((uint8_t)(workRemote.count - reviewTop), VISIBLE_BUTTONS);

    for (uint8_t row = 0; row < shown; ++row) {
      uint8_t buttonIndex = reviewTop + row;
      int y = 41 + row * 14;
      bool active = buttonIndex == selected;

      String label = String(buttonIndex + 1) + ". " + workRemote.signals[buttonIndex].name;
      String hk = workRemote.signals[buttonIndex].hotkey;
      int hkWidth = hk.length() ? canvas.textWidth(hk) + 8 : 0;

      if (active) canvas.fillRoundRect(10, y, 220, 12, 2, UI_CYAN);
      canvas.setTextColor(active ? UI_BG : UI_WHITE, active ? UI_CYAN : UI_BG);
      canvas.drawString(fitText(String(active ? "> " : "  ") + label, 207 - hkWidth), 19, y + 2);
      if (hk.length()) {
        canvas.setTextDatum(top_right);
        canvas.setTextColor(active ? UI_BG : UI_YELLOW, active ? UI_CYAN : UI_BG);
        canvas.drawString(hk, 225, y + 2);
        canvas.setTextDatum(top_left);
      }
    }

    footer("[ENTER] Name [R] Replay [S] Save");
  }

  else if (screen == EDIT_BUTTON) {
    header("NAME BUTTON");
    bool nameFocus = editField == 0;

    canvas.setTextColor(UI_WHITE, UI_BG);
    canvas.drawString("NAME", 12, 36);
    canvas.drawRoundRect(10, 46, 220, 18, 2, nameFocus ? UI_CYAN : UI_DIM);
    canvas.setTextColor(nameFocus ? UI_YELLOW : UI_WHITE, UI_BG);
    canvas.drawString("> " + editText + (nameFocus ? "_" : ""), 16, 51);

    canvas.setTextColor(UI_WHITE, UI_BG);
    canvas.drawString("HOTKEY", 12, 69);
    uint16_t keyBorder = hotkeyConflict >= 0 ? UI_RED
                         : hotkeyCapture     ? UI_PINK
                         : !nameFocus        ? UI_CYAN
                                             : UI_DIM;
    canvas.drawRoundRect(10, 79, 220, 18, 2, keyBorder);

    String keyText;
    uint16_t keyColor = UI_YELLOW;
    if (hotkeyConflict >= 0) {
      keyText = hotkeyLongName(pendingKey);
    } else if (hotkeyCapture) {
      keyText = "PRESS A KEY...";
    } else if (editHotkey.length()) {
      keyText = hotkeyLongName(editHotkey);
      keyColor = nameFocus ? UI_WHITE : UI_YELLOW;
    } else {
      keyText = "NONE";
      keyColor = UI_GREY;
    }
    canvas.setTextColor(keyColor, UI_BG);
    canvas.drawString(keyText, 16, 84);

    String hint;
    uint16_t hintColor = UI_GREY;
    if (hotkeyConflict >= 0) {
      hint = hotkeyLongName(pendingKey) + " IS ALREADY USED BY " + workRemote.signals[hotkeyConflict].name;
      hintColor = UI_RED;
    } else if (hotkeyCapture) {
      hint = "ANY KEY EXCEPT ESC / TAB / DEL";
    } else {
      hint = "ESC / TAB / DEL CAN'T BE HOTKEYS";
    }
    canvas.setTextDatum(top_center);
    canvas.setTextColor(hintColor, UI_BG);
    canvas.drawString(fitText(hint, 228), 120, 102);
    canvas.setTextDatum(top_left);

    if (hotkeyConflict >= 0) footer("[ENTER] Reassign  [ESC] Cancel");
    else if (hotkeyCapture) footer("[DEL] No hotkey  [ESC] Cancel");
    else if (nameFocus) footer("[ENTER] Hotkey [TAB] Skip [ESC] Cancel");
    else footer("[ENTER] Save [DEL] Clear [TAB] Name");
  }

  else if (screen == EDIT_REMOTE) {
    header("SAVE REMOTE");
    canvas.setTextColor(UI_WHITE, UI_BG);
    canvas.drawString("TYPE NAME:", 12, 48);
    canvas.drawRoundRect(10, 63, 220, 22, 2, UI_CYAN);
    canvas.setTextColor(UI_YELLOW, UI_BG);
    canvas.drawString("> " + editText + "_", 16, 70);
    footer("[ENTER] Confirm [ESC] Cancel");
  }

  else if (screen == SAVE_WHERE || screen == LOAD_WHERE) {
    String items[] = {
      String("SD CARD") + (sdOK ? "" : " (NOT READY)"),
      String("LITTLEFS") + (littlefsOK ? "" : " (NOT READY)")
    };
    drawList(screen == SAVE_WHERE ? "SAVE LOCATION" : "LOAD LOCATION", items, 2, menu,
             "[ENTER] Select [ESC] Back");
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

        if (active) {
          canvas.fillRoundRect(10, y, 220, 12, 2, UI_CYAN);
        }

        // The selected row scrolls if the name is too long; the others are
        // cut with "..".
        String prefix = active ? "> " : "  ";
        int textX = 19 + canvas.textWidth(prefix);
        int maxW = 226 - textX;

        canvas.setTextColor(
          active ? UI_BG : UI_WHITE,
          active ? UI_CYAN : UI_BG);
        canvas.drawString(prefix, 19, y + 2);
        if (active) {
          drawMarquee(name, textX, y + 2, maxW, UI_BG, UI_CYAN, false);
        } else {
          canvas.drawString(fitText(name, maxW), textX, y + 2);
        }
      }


      footer(screen == BROWSER ? "[ENTER] Load [E] Edit [D] Delete"
                                : "[ENTER] Load [D] Delete [ESC] Back");
    }
  }

  else if (screen == REMOTE_GRID) {
    header(loadedRemote.name);
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

  else if (screen == REMOTE_LIST) {
    header(loadedRemote.name);
    canvas.setTextSize(1);
    canvas.setTextDatum(top_left);

    uint16_t count = loadedRemote.count;
    uint8_t pages = (count + LIST_PAGE_SIZE - 1) / LIST_PAGE_SIZE;
    if (listPage >= pages) listPage = 0;
    uint16_t pageStart = (uint16_t)listPage * LIST_PAGE_SIZE;

    for (uint8_t slot = 0; slot < LIST_PAGE_SIZE; ++slot) {
      uint16_t index = pageStart + slot;
      if (index >= count) break;
      const Signal &s = loadedRemote.signals[index];

      int x = 6 + (slot % 3) * 76;
      int y = 40 + (slot / 3) * 10;
      bool flash = index == flashIndex;

      // Yellow = a hotkey somebody set, grey = handed out automatically or none.
      String key = s.hotkey.length() ? s.hotkey : String("--");
      uint16_t keyColor = flash ? UI_CYAN : ((s.hotkeyAuto || !s.hotkey.length()) ? UI_GREY : UI_YELLOW);
      int keyWidth = canvas.textWidth(key);

      // 12 characters per column: key + "-" + name.
      int maxName = 12 - (int)key.length() - 1;
      String name = s.name;
      if ((int)name.length() > maxName) name = name.substring(0, maxName - 1) + ".";

      canvas.setTextColor(keyColor, UI_BG);
      canvas.drawString(key, x, y);
      canvas.setTextColor(flash ? UI_CYAN : UI_PINK, UI_BG);
      canvas.drawString("-", x + keyWidth, y);
      canvas.setTextColor(flash ? UI_CYAN : UI_WHITE, UI_BG);
      canvas.drawString(name, x + keyWidth + 6, y);
    }

    // Footer: page range in yellow, then the key hints.
    String pageText = String(pageStart + 1) + "-" + String(min((int)pageStart + LIST_PAGE_SIZE, (int)count)) + " / " + String(count);
    String rest = pages > 1 ? "  [TAB] Next  [ESC] Back" : "  [ESC] Back";
    int startX = 120 - canvas.textWidth(pageText + rest) / 2;
    canvas.drawFastHLine(6, 115, 228, UI_YELLOW);
    canvas.setTextColor(UI_YELLOW, UI_BG);
    canvas.drawString(pageText, startX, 121);
    canvas.setTextColor(UI_WHITE, UI_BG);
    canvas.drawString(rest, startX + canvas.textWidth(pageText), 121);
  }

  else if (screen == SPAM_SETUP) {
    header("SPAM SETTINGS");

    String target = spamTarget == 0
                      ? "ALL SIGNALS"
                      : loadedRemote.signals[spamTarget - 1].name;
    target = fitText(target, 140);  // keep a long button name clear of the "SEND:" label

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

    canvas.drawString("SD CARD", 31, 48);
    canvas.setTextColor(sdOK ? UI_GREEN : UI_RED, UI_BG);
    canvas.setTextDatum(top_right);
    canvas.drawString(sdOK ? "MOUNTED" : "NOT MOUNTED", 225, 48);

    canvas.setTextDatum(top_left);
    canvas.setTextColor(UI_WHITE, UI_BG);
    canvas.drawString("IR MODULE", 31, 63);
    canvas.setTextColor(irModuleOK ? UI_GREEN : UI_RED, UI_BG);
    canvas.setTextDatum(top_right);
    canvas.drawString(irTestActive ? "TESTING..." : (irModuleOK ? "CONNECTED" : "NOT CONNECTED"), 225, 63);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(UI_WHITE, UI_BG);
    canvas.drawString("BATTERY", 31, 78);
    canvas.setTextDatum(top_right);
    canvas.setTextColor(UI_YELLOW, UI_BG);
    canvas.drawString(String(M5Cardputer.Power.getBatteryLevel()) + "%", 225, 78);
    canvas.setTextDatum(top_left);
    canvas.drawString("VERSION", 31, 93);
    canvas.setTextDatum(top_right);
    canvas.setTextColor(UI_YELLOW, UI_BG);
    canvas.drawString(APP_VERSION, 225, 93);
    canvas.setTextDatum(top_left);
    footer(irTestActive ? "Connect IR Module" : "[ENTER] Test IR  [ESC] Back");
  }


   else if (screen == ABOUT) {
    header("ABOUT");
    centered("IR CLONER", 40, UI_YELLOW, 2);
    centered("Created by Fr4nx40", 65, UI_WHITE);
    centered(APP_VERSION, 80, UI_GREEN);
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
  editHotkey = "";
  if (button) editHotkey = workRemote.signals[editIndex].hotkey;
  editField = 0;
  hotkeyCapture = false;
  hotkeyConflict = -1;
  screen = button ? EDIT_BUTTON : EDIT_REMOTE;
  previousWord.clear(); previousHotkeys.clear();
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
        editingSaved = false;
        lastCapturedProtocol = "";
        lastCapturedAddress = 0;
        lastCapturedCommand = 0;
        lastCapturedAt = 0;
        resetRawCapture();
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
      reviewTop = 0;
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
    bool movedSelection = menuWrap(selected, workRemote.count);
    if (movedSelection) {
      keepReviewSelectionVisible();
      redraw = true;
    }
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
      // Editing a saved remote: Esc drops the changes and goes back to the file list.
      screen = editingSaved ? BROWSER : CAPTURE_REMOTE;
      editingSaved = false;
      syncKeys();
      redraw = true;
    }
  }

  else if (screen == EDIT_BUTTON) {
    bool tab = tabPressed();
    bool delPress = keysChanged && M5Cardputer.Keyboard.isPressed() &&
                    M5Cardputer.Keyboard.keysState().del;

    if (hotkeyCapture) {
      if (back) {
        // ESC abandons the capture (the old hotkey is kept).
        hotkeyCapture = false;
        hotkeyConflict = -1;
        syncKeys();
        redraw = true;
      } else if (hotkeyConflict >= 0) {
        // The key already belongs to another button: ENTER takes it over.
        if (enterPressed()) {
          workRemote.signals[hotkeyConflict].hotkey = "";
          editHotkey = pendingKey;
          hotkeyConflict = -1;
          hotkeyCapture = false;
          editField = 1;   // stay on the hotkey field: the next ENTER saves
          syncKeys();
          redraw = true;
        }
      } else if (delPress) {
        // DEL = no hotkey for this button.
        editHotkey = "";
        hotkeyCapture = false;
        editField = 1;
        redraw = true;
      } else {
        String key = newHotkeyPress();
        if (key.length()) {
          int8_t other = -1;
          for (uint8_t i = 0; i < workRemote.count; ++i) {
            if (i != editIndex && workRemote.signals[i].hotkey == key) {
              other = (int8_t)i;
              break;
            }
          }
          if (other >= 0) {
            pendingKey = key;
            hotkeyConflict = other;
          } else {
            editHotkey = key;
            hotkeyCapture = false;
            editField = 1;   // stay on the hotkey field: the next ENTER saves
          }
          redraw = true;
        }
      }
    } else if (back) {
      screen = REVIEW;
      syncKeys();
      redraw = true;
    } else if (tab) {
      editField = editField ? 0 : 1;
      previousWord.clear(); previousHotkeys.clear();
      redraw = true;
    } else if (editField == 0) {
      redraw |= textInput(editText, 24);
      if (enterPressed()) {
        // NAME, then ENTER: go straight to the hotkey and wait for a key press.
        String cleaned = cleanName(editText);
        if (!cleaned.length()) {
          editText = cleaned;
          showMessage("NAME REQUIRED");
        } else {
          editText = cleaned;
          editField = 1;
          hotkeyCapture = true;
          previousWord.clear(); previousHotkeys.clear();
        }
        redraw = true;
      }
    } else {
      // Hotkey field, nothing pending: ENTER saves the button and returns to the list.
      if (enterPressed()) {
        String cleaned = cleanName(editText);
        if (!cleaned.length()) {
          editField = 0;
          showMessage("NAME REQUIRED");
        } else {
          workRemote.signals[editIndex].name = cleaned;
          workRemote.signals[editIndex].hotkey = editHotkey;
          screen = REVIEW;
          syncKeys();
        }
        redraw = true;
      } else if (delPress) {
        editHotkey = "";
        redraw = true;
      }
    }
  }

  else if (screen == EDIT_REMOTE) {
    redraw |= textInput(editText, 24);
    if (back) {
      screen = REVIEW;
      syncKeys();
      redraw = true;
    } else if (enterPressed()) {
      editText = cleanName(editText);
      if (!editText.length()) {
        showMessage("NAME REQUIRED");
      } else {
        workRemote.name = editText;
        if (editingSaved) {
          // Saved remote: overwrite its own file. Renaming it writes a new file instead.
          String error;
          bool renamed = workRemote.name != editOrigName;
          if (saveRemote(editStore, workRemote, error, renamed ? String("") : editPath)) {
            editingSaved = false;
            scanFiles(editStore);
            if (menu >= fileCount) menu = fileCount ? fileCount - 1 : 0;
            screen = BROWSER;
            showMessage("REMOTE SAVED", storeName(editStore));
          } else {
            showMessage("SAVE FAILED", error);
          }
        } else {
          menu = 0;
          screen = SAVE_WHERE;
          syncKeys();
        }
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
    } else if (!spam && editPressed() && fileCount) {
      // E: open the saved remote in the editor (rename buttons, set or change hotkeys).
      String error;
      if (loadRemote(activeStore, files[menu], workRemote, error)) {
        for (uint8_t i = 0; i < workRemote.count; ++i) {
          if (workRemote.signals[i].hotkeyAuto) {   // automatic keys are never saved
            workRemote.signals[i].hotkey = "";
            workRemote.signals[i].hotkeyAuto = false;
          }
        }
        editingSaved = true;
        editStore = activeStore;
        editPath = files[menu];
        editOrigName = workRemote.name;
        selected = 0;
        reviewTop = 0;
        screen = REVIEW;
        syncKeys();
      } else {
        showMessage("LOAD FAILED", error);
      }
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
          listPage = 0;
          flashIndex = 255;
          previousWord.clear(); previousHotkeys.clear();
          // Remotes with hotkeys open in the compact key list, the rest in the grid.
          screen = loadedRemote.hasHotkeys ? REMOTE_LIST : REMOTE_GRID;
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
  }

  else if (screen == REMOTE_LIST) {
    uint16_t count = loadedRemote.count;
    uint8_t pages = (count + LIST_PAGE_SIZE - 1) / LIST_PAGE_SIZE;
    bool tab = tabPressed();

    if (back) {
      screen = BROWSER;
      syncKeys();
      redraw = true;
    } else {
      if (tab && pages > 1) {
        listPage = (listPage + 1) % pages;
        redraw = true;
      }

      // A hotkey sends its button straight away, even from another page.
      String key = newHotkeyPress();
      if (key.length()) {
        for (uint8_t i = 0; i < count; ++i) {
          if (loadedRemote.signals[i].hotkey == key) {
            listPage = i / LIST_PAGE_SIZE;
            flashIndex = i;
            flashUntil = millis() + 250;
            if (!sendSignal(loadedRemote.signals[i])) showMessage("SEND FAILED");
            redraw = true;
            break;
          }
        }
      }

      if (flashIndex != 255 && millis() > flashUntil) {
        flashIndex = 255;
        redraw = true;
      }
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

    // Handle left/right changes (leftPressed/rightPressed already do edge detection)
    bool left = leftPressed();
    bool right = rightPressed();

    if (left || right) {
      const bool isRight = right;

      if (menu == 0) {
        // SEND: target
        if (isRight) {
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
          spamLimit = constrain((int)spamLimit + (isRight ? 10 : -10), 1, 9999);
          redraw = true;
        }

      } else if (menu == 3) {
        // DELAY
        spamDelay = constrain((int)spamDelay + (isRight ? 10 : -10), 20, 5000);
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
    // Exit settings: save once, then go home
    if (back || enterPressed()) {
      saveSettings();
      applyPins();
      screen = HOME;   
      syncKeys();
      redraw = true;
      return redraw; 
    }

    if (upPressed()) {
      settingsMenu = settingsMenu == 0 ? 3 : settingsMenu - 1;
      redraw = true;
    }

    if (downPressed()) {
      settingsMenu = settingsMenu >= 3 ? 0 : settingsMenu + 1;
      redraw = true;
    }

    // Use leftPressed()/rightPressed() directly (they already do edge detection)
    bool left = leftPressed();
    bool right = rightPressed();

    if (left || right) {
      const bool isRight = right;
      const int dir = isRight ? 1 : -1;

      if (settingsMenu == 0) {
        uint8_t next = irTxPin;

        for (uint8_t tries = 0; tries <= MAX_GPIO; ++tries) {
          next = (uint8_t)(((int)next + dir + MAX_GPIO + 1) % (MAX_GPIO + 1));
          if (next != irRxPin) {
            break;
          }
        }

        if (next != irTxPin) {
          irTxPin = next;
          applyPins();
          redraw = true;
        }

      } else if (settingsMenu == 1) {
        uint8_t next = irRxPin;

        for (uint8_t tries = 0; tries <= MAX_GPIO; ++tries) {
          next = (uint8_t)(((int)next + dir + MAX_GPIO + 1) % (MAX_GPIO + 1));
          if (next != irTxPin) {
            break;
          }
        }

        if (next != irRxPin) {
          irRxPin = next;
          applyPins();
          redraw = true;
        }

      } else if (settingsMenu == 2) {
        int value = (int)screenBrightness + (isRight ? BRIGHTNESS_STEP : -BRIGHTNESS_STEP);
        screenBrightness = (uint8_t)constrain(
          value,
          (int)MIN_BRIGHTNESS,
          (int)MAX_BRIGHTNESS
        );

        if (!screenDimmed) {
          M5Cardputer.Display.setBrightness(screenBrightness);
        }

        redraw = true;

      } else if (settingsMenu == 3) {
        int value = (int)dimIndex + (isRight ? 1 : -1);
        dimIndex = (uint8_t)constrain(
          value,
          0,
          (int)DIM_CHOICE_COUNT - 1
        );

        redraw = true;
      }
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

// Writes the current canvas straight out as an uncompressed 24-bit BMP to
// <IR_DIR>/screenshots, on SD if it's mounted, LittleFS otherwise. Uses the
// canvas's own readRectRGB() to fetch each row, rather than reading
// canvas.getBuffer()'s raw RGB565 and unpacking it by hand. Despite its
// name, LGFXBase::readRectRGB(uint8_t*) writes bytes in R,G,B order per
// pixel (confirmed against LovyanGFX's bgr888_t struct, whose fields are
// declared r,g,b) - a BMP needs B,G,R, so each pixel's R and B bytes are
// swapped below before writing. Stored top-down (negative height in the
// header), so no bottom-to-top row-flip loop is needed either.
bool saveScreenshotBMP() {
  FS *fs = sdOK ? static_cast<FS *>(&SD) : (littlefsOK ? static_cast<FS *>(&LittleFS) : nullptr);
  if (!fs) return false;

  String dir = String(IR_DIR) + "/screenshots";
  if (!fs->exists(dir) && !fs->mkdir(dir)) return false;

  const int width = 240;
  const int height = 135;
  const uint32_t rowBytes = width * 3;  // 720 - already a multiple of 4, so no row padding needed
  const uint32_t pixelDataSize = rowBytes * height;
  const uint32_t fileSize = 54 + pixelDataSize;

  uint8_t header[54] = {0};
  header[0] = 'B'; header[1] = 'M';
  memcpy(&header[2], &fileSize, 4);
  uint32_t dataOffset = 54;
  memcpy(&header[10], &dataOffset, 4);
  uint32_t dibSize = 40;
  memcpy(&header[14], &dibSize, 4);
  int32_t w = width, h = -height;  // negative height = stored top-down
  memcpy(&header[18], &w, 4);
  memcpy(&header[22], &h, 4);
  header[26] = 1;   // planes
  header[28] = 24;  // bits per pixel
  memcpy(&header[34], &pixelDataSize, 4);

  String path = dir + "/screenshot_" + String(millis()) + "_" + String(screenshotCounter++) + ".bmp";
  File f = fs->open(path, FILE_WRITE);
  if (!f) return false;
  f.write(header, sizeof(header));

  uint8_t row[width * 3];
  for (int y = 0; y < height; ++y) {
    canvas.readRectRGB(0, y, width, 1, row);  // R,G,B order per pixel
    for (int x = 0; x < width; ++x) {
      uint8_t *px = row + x * 3;
      uint8_t r = px[0];
      px[0] = px[2];  // B
      px[2] = r;      // R  (px[1]=G is already correct)
    }
    f.write(row, sizeof(row));
  }
  f.close();
  return true;
}

// Copies the splash sprite pixel-by-pixel, skipping any pixel that matches


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

  // Keyboard.isChange() remembers the last key count, so a second call in the same
  // pass would return false. Ask exactly once here; textInput(), the hotkey code and
  // the edit screen all read keysChanged instead of calling isChange() themselves.
  keysChanged = M5Cardputer.Keyboard.isChange();

  // Fn+S is handled before anything else, from any screen, so it works as a
  // global "grab a screenshot" shortcut. It also pre-marks the plain 's' key
  // as already held, so the per-screen savePressed() check (CAPTURE_REMOTE,
  // REVIEW, ...) doesn't also fire for the same physical keypress once Fn is
  // released while 's' is still down.
  if (screenshotPressed()) {
    wasSave = M5Cardputer.Keyboard.isKeyPressed('s');
    previousWord = M5Cardputer.Keyboard.keysState().word;
    previousHotkeys = physicalKeys();  // 's' must not also fire as a hotkey
    bool ok = saveScreenshotBMP();
    showMessage(ok ? "SCREENSHOT SAVED" : "SCREENSHOT FAILED");
    drawScreen();
    delay(12);
    return;
  }

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

  // Keep animating while a long name is scrolling (about 20 frames a second).
  static uint32_t lastMarqueeDraw = 0;
  if (marqueeActive && millis() - lastMarqueeDraw >= 50) redraw = true;

  if (redraw || irTestChanged || (screen == SYSTEM && irTestActive)) {
    drawScreen();
    lastMarqueeDraw = millis();
  }
  delay(12);
}
