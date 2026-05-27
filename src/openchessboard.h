#pragma once
#include <Arduino.h>
#include <FreeRTOS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "settings.h"
#include "utility.h"
#include "timer.h"
#include "wifi_client.h"
#include "lichess_client.h"
#include "board_driver.h"
#include "settings_accesspoint.h"
#include <Preferences.h>
#include "ble_app.h"
#include "wifi_app.h"
#include "puzzle_app.h"
#include <Update.h>
#include <esp_ota_ops.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

// Debug Settings
//#define MANUAL_MOVE_INPUT
//#define PLUG_AT_TOP // not fully supported yet
#define DEBUG true  
#define DEBUG_SERIAL if(DEBUG)Serial

// WiFi variables
extern int status;
extern char server[];  

extern WiFiClientSecure StreamClient;
extern WiFiClientSecure PostClient;

extern hw_timer_t *timer;
extern volatile bool timerFlag;

//lichess variables
extern String username;
extern String currentGameID;
extern bool myturn;
extern String myLastMove;
extern String oppLastMove;
extern String latestMove;
extern String myMove;
extern String moves; 
extern bool is_castling_allowed;

// LED and state variables
extern bool update_flipstate;
extern bool is_booting;
extern bool is_connecting;
extern bool is_updating;
extern bool is_game_running;
extern bool is_seeking;
extern bool dimLEDs;

// True when the user has signalled a mid-game restart (both kings off
// the board for the configured sustained duration, or all pieces
// returned to the starting position after at least one move). Game
// loops in wifi_app / ble_app should check this between moves and, if
// set, resign the current game and start a fresh one.
extern bool restart_requested;

// millis() at which both kings were first observed off the board.
// 0 = either at least one king is present, or we're not currently
// tracking. Kept global so the 5-second accumulator survives across
// getMoveInput() invocations — the previous function-local timer was
// reset on every call, which prevented the trigger from ever firing
// in real gameplay where each piece motion produces a new
// getMoveInput.
extern unsigned long kings_off_since_ms;

// Parallel timer for the 'pieces back in start position' restart
// trigger. Same reasoning as kings_off_since_ms — needs to persist
// across getMoveInput calls so a sequence of rejected moves doesn't
// keep restarting the accumulator.
extern unsigned long start_pos_since_ms;

// True once the in-game piece layout has been observed to differ from
// the standard starting position. Gates the "all pieces back in start"
// restart trigger so a brand-new game (pieces already in start
// position) doesn't immediately resign itself.
extern bool ever_left_start_pos;

// Settings object
extern Preferences preferences;

// wifi  and board variablesht
extern String wifi_ssid;
extern String wifi_password;
extern String lichess_api_token;
extern String board_gameMode;
extern String board_startupType;