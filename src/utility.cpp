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
  //check if last move was king move from castling
  if(((move_input == "e1g1") || (move_input == "e1c1") ||  (move_input == "e8g8") || (move_input == "e8c8")) && is_castling_allowed){
    
   DEBUG_SERIAL.println("Castling... Wait for rook move...");
   
   bool is_castling = true;
   
   //wait until move was rook move from castling
   while(is_castling && is_game_running){
    displayMove(myLastMove);
    move_input = getMoveInput();
    DEBUG_SERIAL.println(move_input);
    
    if((move_input == "h1f1") || (move_input == "a1d1") ||  (move_input == "h8f8") || (move_input == "a8d8"))
    {
      is_castling = false;
      is_castling_allowed = false;
    }
  
   }
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
  currentGameID = "noGameID";
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
  currentGameID = "noGameID";
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
  currentGameID = "noGameID";
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
  // Hall-sensor coordinates for the three mode-select squares. The hall
  // array is column-major (row index = chess column h..a, bit index = chess
  // rank 1..8), so:
  //   a1 → hallBoardState[7] bit 0
  //   b1 → hallBoardState[6] bit 0
  //   c1 → hallBoardState[5] bit 0
  const uint8_t modeRows[3] = {7, 6, 5};
  const uint8_t modeBit = 0;
  const char* modeNames[3] = {"WiFi", "BLE", "AP"};

  // Hint LEDs marking a1, b1, c1 so the user sees which piece to lift
  // (or place) to select a mode.
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

  const unsigned long selectionWindowMs = 20000;
  const unsigned long pollIntervalMs = 100;
  const unsigned long startMs = millis();
  bool matched = false;
  byte hallCurrent[8];

  while (millis() - startMs < selectionWindowMs) {
    readHall(hallCurrent);

    // Mode A/B/C: any of the three target squares' hall sensor changed
    // state in either direction (piece lifted OR placed). Whichever
    // square the user touches first wins.
    for (int i = 0; i < 3; i++) {
      const bool wasPresent = bitRead(hallInitial[modeRows[i]], modeBit) != 0;
      const bool nowPresent = bitRead(hallCurrent[modeRows[i]], modeBit) != 0;
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
    // No piece lifted within the window. Force BLE so the user always
    // gets a predictable startup mode regardless of what was previously
    // saved via the access point config.
    board_startupType = "BLE";
    DEBUG_SERIAL.println("No mode selection within 20s; defaulting to BLE");
  }

  // Clear hint LEDs before the chosen mode takes over.
  clearDisplay();
}