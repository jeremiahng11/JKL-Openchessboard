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
  byte read_hall_array[8];
  byte pattern1[8];
  byte pattern2[8];
  byte pattern3[8];
  byte pattern4[8];

  memset(pattern1, 0xC3, sizeof(pattern1));
  memset(pattern2, 0xC3, sizeof(pattern2));
  memset(pattern3, 0xC3, sizeof(pattern3));
  memset(pattern4, 0x00, sizeof(pattern4));

  pattern1[7] = 0xC2; // remove a1 (mode A): WiFi standalone
  pattern2[6] = 0xC2; // remove b1 (mode B): BLE app
  pattern3[5] = 0xC2; // remove c1 (mode C): config access point

  pattern4[0] = 0xFF; // place 8 pieces on h column (plug at right): queen-puzzle

  // Hint LEDs that mark the three single-piece-removal squares (a1, b1, c1)
  // so the user knows which piece to lift to select a mode. Bit positions
  // match the hall-sensor mapping for those squares.
  byte hintLeds[8] = {0};
  hintLeds[7] = 0x01;
  hintLeds[6] = 0x01;
  hintLeds[5] = 0x01;
  displayArray(hintLeds);

  // Poll for up to selectionWindowMs; exit early on the first pattern
  // match. The previous one-shot read fired before the user could even
  // place pieces, so on every reboot the board fell through to the saved
  // default and the mode-select feature was effectively dead.
  const unsigned long selectionWindowMs = 10000;
  const unsigned long pollIntervalMs = 100;
  const unsigned long startMs = millis();
  bool matched = false;

  while (millis() - startMs < selectionWindowMs) {
    readHall(read_hall_array);
    if (memcmp(read_hall_array, pattern1, 8) == 0) {
      board_startupType = "WiFi";
      matched = true;
      break;
    }
    if (memcmp(read_hall_array, pattern2, 8) == 0) {
      board_startupType = "BLE";
      matched = true;
      break;
    }
    if (memcmp(read_hall_array, pattern3, 8) == 0) {
      board_startupType = "AP";
      matched = true;
      break;
    }
    if (memcmp(read_hall_array, pattern4, 8) == 0) {
      board_startupType = "PUZZLE";
      matched = true;
      break;
    }
    delay(pollIntervalMs);
  }

  // Final sensor snapshot for the debug log so we can diagnose
  // mode-selection failures from serial output.
  readHall(read_hall_array);
  DEBUG_SERIAL.print("read_hall_array: ");
  for (int i = 0; i < 8; i++) {
    Serial.print(read_hall_array[i], HEX);
    if (i < 8 - 1) {
      DEBUG_SERIAL.print(", ");
    }
  }
  DEBUG_SERIAL.println();
  if (matched) {
    DEBUG_SERIAL.println("Mode selection matched: " + board_startupType);
  } else {
    // No piece removed within the window. Force BLE so the user always
    // gets a predictable startup mode regardless of what was previously
    // saved via the access point config.
    board_startupType = "BLE";
    DEBUG_SERIAL.println("No mode selection within 10s; defaulting to BLE");
  }

  // Clear hint LEDs before the chosen mode takes over.
  clearDisplay();
}