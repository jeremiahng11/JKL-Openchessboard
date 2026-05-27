#include "openchessboard.h"



// LED and state variables
bool update_flipstate = true;
bool is_booting = true;
bool is_updating = false;
bool is_connecting = false;
bool is_game_running = false;
bool is_seeking = false;
bool dimLEDs  = false;
bool restart_requested = false;
bool ever_left_start_pos = false;
unsigned long kings_off_since_ms = 0;
unsigned long start_pos_since_ms = 0;

// WiFi, and timer variables
String wifi_ssid;
String wifi_password;
int status = WL_IDLE_STATUS;
WiFiClientSecure StreamClient;
WiFiClientSecure PostClient;

//lichess variables
String lichess_api_token;
char server[] = "lichess.org"; 
String username = "no";
String currentGameID = "noGame";
bool myturn = false;
String myLastMove = "xx";
String oppLastMove = "xy";
String latestMove = "zz";
String myMove = "yy";
String moves = "";
bool is_castling_allowed = true;


void run_WiFi_app(void){

  setStateConnecting();
  wifi_setup();
  wifi_firmwareUpdate();

  PostClient.setInsecure();
  StreamClient.setInsecure();
  DEBUG_SERIAL.println("\nStarting connection to server...");
 
  while (WiFi.status() == WL_CONNECTED){
    DEBUG_SERIAL.println("\nConnected to Server...");
    DEBUG_SERIAL.println("Find ongoing game");

    // The wifi-setup phase blinks a4 via displayConnectWait(); once
    // we're connected, clear that stuck LED before showing the
    // 'searching for game' indicator. Without this the a4 LED stayed
    // lit indefinitely while the board polled for ongoing games.
    is_connecting = false;
    clearDisplay();

    // Tracks whether we just posted a new game ourselves. The override
    // below would otherwise immediately resign it (pieces are in start
    // position because the user just placed them to trigger the new game)
    // and we'd ping-pong: post -> getGameID finds it -> override resigns
    // it -> post again -> ... forever.
    bool wePostedNewGame = false;

    while(!is_game_running){
      getGameID(PostClient);

      // If a game was found on Lichess but the user has all pieces in
      // the starting position on the physical board, they're signalling
      // 'I want a new game, not to resume this one'. Resign the found
      // game and loop back so the new-game flow takes over.
      //
      // SKIP this check on the iteration immediately after our own
      // postNewGame — the game we just created always satisfies the
      // condition (pieces are in start, board found a game) but we
      // want to play it, not resign it.
      if (wePostedNewGame) {
        wePostedNewGame = false;
        DEBUG_SERIAL.println("Skipping override-resume: this is the game we just posted");
      } else if (is_game_running && board_gameMode != "None" && isStartingPosition()) {
        DEBUG_SERIAL.print("Override resume: board in start position, resigning ");
        DEBUG_SERIAL.println(currentGameID);
        resignGame(PostClient, currentGameID);
        is_game_running = false;
        currentGameID = "noGame";
        delay(800); // let the server process the resignation

        // Board is already in start position — that gesture WAS the
        // new-game trigger. Post the new game directly instead of
        // re-polling getGameID and waiting for another trigger
        // detection (which would produce two redundant "no Game found"
        // log lines and an unnecessary 5s wait window).
        DEBUG_SERIAL.println("\nStart Game with prefered settings: " + board_gameMode);
        postNewGame(PostClient, board_gameMode);
        ever_left_start_pos = false;
        restart_requested = false;
        kings_off_since_ms = 0;
        start_pos_since_ms = 0;
        wePostedNewGame = true;
        continue;
      }

      //Start new game if no game is running and seek not already started
      if (board_gameMode != "None"  && !is_seeking &&  !is_game_running){
        DEBUG_SERIAL.println("\nWait for new-game trigger (start position OR both kings off 5s)");
        waitForNewGameTrigger(5000);

        // Belt-and-braces: a game might have appeared on Lichess while
        // we were waiting (correspondence challenge accepted, etc.).
        // Resign it before posting our own new game so the user actually
        // gets the game their gesture asked for.
        getGameID(PostClient);
        if (is_game_running) {
          DEBUG_SERIAL.print("Game appeared during trigger wait, resigning ");
          DEBUG_SERIAL.println(currentGameID);
          resignGame(PostClient, currentGameID);
          is_game_running = false;
          currentGameID = "noGame";
          delay(800);
        }

        DEBUG_SERIAL.println("\nStart Game with prefered settings: "+ board_gameMode);
        postNewGame(PostClient,  board_gameMode);
        // Reset the start-position-restart guard for the new game so
        // the trigger only fires after pieces have actually moved.
        ever_left_start_pos = false;
        restart_requested = false;
        kings_off_since_ms = 0;
        start_pos_since_ms = 0;
        // Suppress the override on the next iteration so getGameID
        // finding *our* newly-created game doesn't immediately resign it.
        wePostedNewGame = true;
      } else if (!is_game_running && !is_seeking) {
        // gameMode is "None" (or we're between polls): show the
        // center-squares 'searching for game' animation from the README
        // so the user knows the board is alive and waiting.
        displayWaitForGame();
      }
    }

    getStream(StreamClient);   
    dimLEDs = false;
    timerFlag = true;
    String boardMove;

    // Tracks both restart triggers' start times at the in-game-loop
    // scope, so they also fire during the OPPONENT's turn (when control
    // is here instead of inside getMoveInput).
    unsigned long ingameKingsOffSinceMs = 0;
    unsigned long ingameStartPosSinceMs = 0;

    while (is_game_running | is_seeking)
    {
      // If getMoveInput requested a mid-game restart, resign here and
      // break out so the outer loop starts a new game.
      if (restart_requested) {
        DEBUG_SERIAL.print("Mid-game restart requested, resigning ");
        DEBUG_SERIAL.println(currentGameID);
        resignGame(PostClient, currentGameID);
        is_game_running = false;
        is_seeking = false;
        restart_requested = false;
        currentGameID = "noGame";
        delay(800);
        break;
      }

      // Same triggers when control is here (opponent's turn — no
      // getMoveInput running). Sample a single hall read per iteration.
      {
        byte hallNow[8];
        readHall(hallNow);

        // Trigger #1: both kings off >=5s.
        const bool e1Empty = bitRead(hallNow[4], 7) == 0;
        const bool e8Empty = bitRead(hallNow[4], 0) == 0;
        if (e1Empty && e8Empty) {
          if (ingameKingsOffSinceMs == 0) {
            ingameKingsOffSinceMs = millis();
          } else if (millis() - ingameKingsOffSinceMs >= 5000) {
            DEBUG_SERIAL.println("in-game loop: both kings off >=5s, requesting restart");
            restart_requested = true;
            ingameKingsOffSinceMs = 0;
            continue;
          }
        } else if (ingameKingsOffSinceMs != 0) {
          ingameKingsOffSinceMs = 0;
        }

        // Trigger #2: pieces back in starting position >=5s, gated by
        // ever_left_start_pos so a brand-new game doesn't self-resign.
        bool inStartPos = true;
        for (int i = 0; i < 8; i++) {
          if (hallNow[i] != 0xC3) { inStartPos = false; break; }
        }
        if (!inStartPos) {
          ever_left_start_pos = true;
          ingameStartPosSinceMs = 0;
        } else if (ever_left_start_pos) {
          if (ingameStartPosSinceMs == 0) {
            ingameStartPosSinceMs = millis();
          } else if (millis() - ingameStartPosSinceMs >= 5000) {
            DEBUG_SERIAL.println("in-game loop: pieces back in start position >=5s, requesting restart");
            restart_requested = true;
            ingameStartPosSinceMs = 0;
            continue;
          }
        }
      }

      moveStreamHandler();

      if (myturn & latestMove == oppLastMove){
        DEBUG_SERIAL.println("Wait for accept move input...");
        while (boardMove != latestMove){
          displayMove(latestMove);

          boardMove = getMoveInput();
          if (restart_requested) break;
          String swapped_move = boardMove.substring(2, 4) + boardMove.substring(0, 2);
          if(boardMove.substring(0, 2) == boardMove.substring(2, 4) | swapped_move == latestMove){
            boardMove = latestMove;
            break;
          }
          DEBUG_SERIAL.print("move played on board: ");
          DEBUG_SERIAL.println(boardMove);


          if (boardMove != latestMove){
            displayMoveRecect(boardMove);
            break;
          }
        }

      }

      if (myturn){
        DEBUG_SERIAL.println("Wait for board move input...");
        bool moveSuccess = false;
        while(is_game_running){ // wait for sucessful move transmission to get to opponents turn
          boardMove = getMoveInput();
          if (restart_requested) break;
          DEBUG_SERIAL.print("move played on board: ");
          DEBUG_SERIAL.println(boardMove);
          DEBUG_SERIAL.println("try to send move...");
          moveSuccess = postMove(PostClient, boardMove);
          bool once = true;
          String swapped_move;
          if (moveSuccess){
            myLastMove = boardMove;
            myturn = false;
            break;
          }
          else{
            if (once){
              swapped_move = boardMove.substring(2, 4) + boardMove.substring(0, 2);
              moveSuccess = postMove(PostClient,swapped_move);
              once = false;
            }
            if(moveSuccess){
              boardMove = swapped_move;
              myLastMove = boardMove;
              myturn = false;
              break;
            }
            DEBUG_SERIAL.println("invalid move. wait for move take back...");
            displayMoveRecect(boardMove);
            boardMove = getMoveInput();
          }
        }
      }
    }
    
    byte frame[8];
    flickeringAnimation(frame);
    clearDisplay();
    DEBUG_SERIAL.println("game ended...");
    disableClient(StreamClient);
    disableClient(PostClient);
    WiFi.disconnect(true,true);
    ESP.restart();
  }  
}

