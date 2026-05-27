#include "openchessboard.h"
/* ---------------------------------------
 *  function to get substring between string firstdel and enddel
 *  @params[in] void
 *  @return substring
*/
String GetStringBetweenStrings(String input, String firstdel, String enddel) {
  int posfrom = input.indexOf(firstdel) + firstdel.length();
  int posto   = input.indexOf(enddel);
  return input.substring(posfrom, posto);
}

/* ---------------------------------------
 *  function to check if move is part of castling move and wait for rook move to complete castling
 *  @params[in] String move_input
 *  @return void
*/
void checkCastling(String move_input) {
  // Derive the matching rook move for each of the four castling king
  // moves so we can light up the rook's from/to squares (the previous
  // implementation showed the king's squares again, leaving the user
  // guessing which rook to move).
  String expectedRook = "";
  if      (move_input == "e1g1") expectedRook = "h1f1";  // white O-O
  else if (move_input == "e1c1") expectedRook = "a1d1";  // white O-O-O
  else if (move_input == "e8g8") expectedRook = "h8f8";  // black O-O
  else if (move_input == "e8c8") expectedRook = "a8d8";  // black O-O-O
  else return; // not a castling king move

  DEBUG_SERIAL.print("Castling king move detected (");
  DEBUG_SERIAL.print(move_input);
  DEBUG_SERIAL.print("); waiting for rook ");
  DEBUG_SERIAL.println(expectedRook);

  bool castlingDone = false;
  while (!castlingDone && is_game_running) {
    displayMove(expectedRook);
    String rookMove = getMoveInput();
    if (restart_requested) return; // let outer loop handle the restart
    DEBUG_SERIAL.print("rook move on board: ");
    DEBUG_SERIAL.println(rookMove);
    if (rookMove == expectedRook) {
      castlingDone = true;
    }
    // Any other motion is ignored — user will see the LEDs still
    // pointing to the right squares and try again.
  }
}


/* ---------------------------------------
 *  function to set booting state and initializes state variables
 *  @params[in] void
 *  @return void
*/
void setStateBooting(void){
  is_game_running = false;
  is_booting = true;
  is_connecting = false;
  is_updating = false;
  is_seeking = false;   
  myLastMove = "xx";
  oppLastMove = "xy";
  latestMove = "zz";
  myMove = "noMove";
  moves = "noMoves";
  currentGameID = "noGame";
  //myturn = false;
}

void setStateUpdating(void){
  is_game_running = false;
  is_booting = false;
  is_connecting = false;
  is_updating = true;
  is_seeking = false;   
  myLastMove = "xx";
  oppLastMove = "xy";
  latestMove = "zz";
  myMove = "yy";
  moves = "";
  currentGameID = "noGame";
  //myturn = false;
}
/* ---------------------------------------
 *  function to set connecting state and initializes state variables
 *  @params[in] void
 *  @return void
*/
void setStateConnecting(void){
  is_game_running = false;
  is_booting = false;
  is_connecting  = true;
  is_updating = false;
  is_seeking = false;   
  myLastMove = "xx";
  oppLastMove = "xy";
  latestMove = "zz";
  myMove = "yy";
  moves = "";
  currentGameID = "noGame";
  //myturn = false;
}

void setStatePlaying(void){
  is_seeking = false;    
  is_game_running = true;
  is_updating = false;
  is_connecting = false;
  displayNewGame();
}

String urlDecode(const String &encoded) {
  String decoded = "";
  char ch;
  int i = 0;
  while (i < encoded.length()) {
    ch = encoded.charAt(i);
    if (ch == '%') {
      // Get the next two characters after %
      String hexCode = encoded.substring(i + 1, i + 3);
      decoded += (char) strtol(hexCode.c_str(), NULL, 16);
      i += 3;
    } else if (ch == '+') {
      // Decode the + as a space
      decoded += ' ';
      i++;
    } else {
      decoded += ch;
      i++;
    }
  }
  return decoded;
}

void readSettings(void){

  preferences.begin("settings", true);

  if (preferences.isKey("ssid")) {
    wifi_ssid = urlDecode(preferences.getString("ssid", ""));
    wifi_password = urlDecode(preferences.getString("password", ""));
    lichess_api_token = urlDecode(preferences.getString("token", ""));
    board_gameMode = urlDecode(preferences.getString("gameMode", ""));
    board_startupType = preferences.getString("startupType", "");
    DEBUG_SERIAL.println("Settings Loaded from Flash:");
    DEBUG_SERIAL.println("SSID: " + wifi_ssid);
    DEBUG_SERIAL.println("Password: " + wifi_password);
    DEBUG_SERIAL.println("Token: " + lichess_api_token);
    DEBUG_SERIAL.println("Game Mode: " + board_gameMode);
    DEBUG_SERIAL.println("Startup Type: " + board_startupType);
  } else {
    DEBUG_SERIAL.println("No settings found, using default values.");
  }
  preferences.end();
}

/* ---------------------------------------
 *  Blocks until the user signals "start a new game" via either of:
 *    1. All pieces placed in the chess starting position.
 *    2. Both kings (e1 and e8) removed from the board continuously for
 *       at least [bothKingsRemovedMs] milliseconds.
 *
 *  Returns true once a trigger fires (always — there is no timeout in
 *  this version; the WiFi app stays in this wait until the user acts).
 *
 *  King hall coordinates (raw, pre-rotation; derived empirically from
 *  the mode-select diagnostic logs):
 *    e1 → hallBoardState[4] bit 7
 *    e8 → hallBoardState[4] bit 0
 *
 *  isStartingPosition() updates the LED display each call with the diff
 *  vs the starting layout, so the user gets visual feedback either way.
 *  @return bool always true
*/
bool waitForNewGameTrigger(unsigned long bothKingsRemovedMs) {
  unsigned long kingsOffSinceMs = 0;
  bool warnedKingsOff = false;
  byte hall[8];

  while (true) {
    // Trigger 1: full starting position. This call also updates the
    // LED display so the user sees which squares are wrong.
    if (isStartingPosition()) {
      DEBUG_SERIAL.println("Trigger: starting position detected");
      return true;
    }

    // Trigger 2: both kings off for sustained duration.
    readHall(hall);
    const bool e1Empty = bitRead(hall[4], 7) == 0;
    const bool e8Empty = bitRead(hall[4], 0) == 0;
    if (e1Empty && e8Empty) {
      if (kingsOffSinceMs == 0) {
        kingsOffSinceMs = millis();
        warnedKingsOff = false;
      } else if (!warnedKingsOff) {
        DEBUG_SERIAL.print("Both kings off; waiting ");
        DEBUG_SERIAL.print(bothKingsRemovedMs);
        DEBUG_SERIAL.println("ms for sustained trigger...");
        warnedKingsOff = true;
      } else if (millis() - kingsOffSinceMs >= bothKingsRemovedMs) {
        DEBUG_SERIAL.println("Trigger: both kings off for required duration");
        return true;
      }
    } else if (kingsOffSinceMs != 0) {
      DEBUG_SERIAL.println("King replaced; resetting both-kings-off timer");
      kingsOffSinceMs = 0;
      warnedKingsOff = false;
    }

    delay(100);
  }
}

bool isStartingPosition(void){
  byte read_hall_array[8];
  byte diff_pattern[8];
  byte pattern1[8];
  memset(pattern1, 0xC3, sizeof(pattern1));
  readHall(read_hall_array);

  calculateDifference(diff_pattern, read_hall_array, pattern1);
  rotate180(diff_pattern);
  dimLEDs = true;
  displayArray(diff_pattern);

  if (memcmp(read_hall_array, pattern1, 8) == 0){
    return true;
  }
  else{
    return false;
  }
}

void readBoardSelection(){
  // Hall-sensor coordinates for the three mode-select squares, empirically
  // determined from real boards: a piece lifted from chess square c1
  // changes hallBoardState[2] bit 7. By the same pattern:
  //   a1 → hallBoardState[0] bit 7
  //   b1 → hallBoardState[1] bit 7
  //   c1 → hallBoardState[2] bit 7
  // (The createFen() pipeline applies rotate90CCW + rotate180 to bring
  // these raw coordinates into FEN orientation, which is why an exact
  // empty / starting-position pattern like 0xC3 fits across all bytes.)
  const uint8_t modeBytes[3] = {0, 1, 2};
  const uint8_t modeBit = 7;
  const char* modeNames[3] = {"WiFi", "BLE", "AP"};

  // Hint LEDs marking a1, b1, c1 so the user sees which piece to lift
  // (or place) to select a mode. LED address space is independent of the
  // hall-sensor address space, so the LED bytes/bits stay as they were —
  // it's only the *hall* mapping that was wrong.
  byte hintLeds[8] = {0};
  hintLeds[7] = 0x01;
  hintLeds[6] = 0x01;
  hintLeds[5] = 0x01;
  displayArray(hintLeds);

  // Brief settling delay so the hall sensors have time to stabilise after
  // boot before we capture the baseline. Without this, the initial read
  // can be noisy on cold-boot and a transition is "missed" because the
  // baseline was already wrong.
  delay(500);

  // Snapshot initial state so we can detect *transitions* on the three
  // squares rather than requiring an exact full-board pattern.
  byte hallInitial[8];
  readHall(hallInitial);

  // Also track the queen-puzzle layout (8 pieces stacked on the h column).
  byte puzzlePattern[8] = {0xFF, 0, 0, 0, 0, 0, 0, 0};

  // Full window: 20s. Early-exit window: if the user has touched
  // nothing in the first 3 seconds AND a non-default saved mode
  // exists, fall through to the saved mode immediately — most boots
  // are the user wanting to resume the same mode (WiFi) without
  // waiting through the full 20s. The full 20s still applies if no
  // saved mode is set, so first-time setup still works.
  const unsigned long selectionWindowMs = 20000;
  const unsigned long earlyExitMs = 3000;
  const unsigned long pollIntervalMs = 100;
  const unsigned long startMs = millis();
  bool matched = false;
  byte hallCurrent[8];
  byte hallLast[8];
  memcpy(hallLast, hallInitial, 8);

  // Has the user ever made a saved mode choice? If preferences holds
  // a non-empty startup type, we trust the 3s early-exit.
  preferences.begin("settings", true);
  const String savedStartupType = preferences.getString("startupType", "");
  preferences.end();
  const bool hasSavedMode = savedStartupType.length() > 0;

  DEBUG_SERIAL.print("--- Mode-select window open (");
  DEBUG_SERIAL.print(selectionWindowMs / 1000);
  DEBUG_SERIAL.print("s, early-exit after ");
  DEBUG_SERIAL.print(earlyExitMs / 1000);
  DEBUG_SERIAL.println("s if no input and saved mode exists) ---");

  while (millis() - startMs < selectionWindowMs) {
    if (hasSavedMode && (millis() - startMs > earlyExitMs)) {
      DEBUG_SERIAL.println("No mode-select input within early-exit window; using saved mode immediately");
      break;
    }
    readHall(hallCurrent);

    // Diagnostic: log every bit that changed since the previous poll so
    // the user can correlate physical actions to raw hall coordinates.
    // Print format: `change @ byte=B bit=b : 0->1` (or 1->0).
    for (int b = 0; b < 8; b++) {
      const byte diff = hallCurrent[b] ^ hallLast[b];
      if (diff == 0) continue;
      for (int bit = 0; bit < 8; bit++) {
        if (!bitRead(diff, bit)) continue;
        const int now = bitRead(hallCurrent[b], bit);
        DEBUG_SERIAL.print("change @ byte=");
        DEBUG_SERIAL.print(b);
        DEBUG_SERIAL.print(" bit=");
        DEBUG_SERIAL.print(bit);
        DEBUG_SERIAL.print(" : ");
        DEBUG_SERIAL.print(1 - now);
        DEBUG_SERIAL.print("->");
        DEBUG_SERIAL.println(now);
      }
    }
    memcpy(hallLast, hallCurrent, 8);

    // Mode A/B/C: any of the three target squares' hall sensor changed
    // state in either direction (piece lifted OR placed). Whichever
    // square the user touches first wins.
    for (int i = 0; i < 3; i++) {
      const bool wasPresent = bitRead(hallInitial[modeBytes[i]], modeBit) != 0;
      const bool nowPresent = bitRead(hallCurrent[modeBytes[i]], modeBit) != 0;
      if (wasPresent != nowPresent) {
        board_startupType = modeNames[i];
        matched = true;
        break;
      }
    }
    if (matched) break;

    // Queen-puzzle: explicit absolute layout.
    if (memcmp(hallCurrent, puzzlePattern, 8) == 0) {
      board_startupType = "PUZZLE";
      matched = true;
      break;
    }

    delay(pollIntervalMs);
  }

  // Debug snapshot for serial diagnostics.
  readHall(hallCurrent);
  DEBUG_SERIAL.print("hall_initial: ");
  for (int i = 0; i < 8; i++) {
    Serial.print(hallInitial[i], HEX);
    if (i < 8 - 1) DEBUG_SERIAL.print(", ");
  }
  DEBUG_SERIAL.println();
  DEBUG_SERIAL.print("hall_current: ");
  for (int i = 0; i < 8; i++) {
    Serial.print(hallCurrent[i], HEX);
    if (i < 8 - 1) DEBUG_SERIAL.print(", ");
  }
  DEBUG_SERIAL.println();

  if (matched) {
    DEBUG_SERIAL.println("Mode selection matched: " + board_startupType);
  } else {
    // No piece touched within the window. Fall back to whatever
    // readSettings() loaded from flash (the user-configured default via
    // the access point). If flash is empty / unset, last-resort to BLE so
    // there's always a runnable mode.
    if (board_startupType.length() == 0) {
      board_startupType = "BLE";
      DEBUG_SERIAL.println("No mode selection and no saved default; using BLE");
    } else {
      DEBUG_SERIAL.println(
        "No mode selection within 20s; using saved default: " + board_startupType);
    }
  }

  // Clear hint LEDs before the chosen mode takes over.
  clearDisplay();
}