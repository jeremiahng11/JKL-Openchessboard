
#include "openchessboard.h"

hw_timer_t *timer = NULL;
volatile bool timerFlag = false;

// Interrupt Service Routine (ISR)
void IRAM_ATTR onTimer() {
  timerFlag = true;  // Set flag to indicate interrupt
}
              
void gameTimerHandler() {
  DEBUG_SERIAL.println(".");

  // WiFi blips are common on 2.4GHz; don't reboot on the first hint
  // of trouble. Try a graceful WiFi.reconnect() and only restart if
  // we still can't reach the AP after a sustained outage. The 10s
  // window covers typical brief router glitches without losing the
  // in-progress game.
  static unsigned long wifiFirstLostMs = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiFirstLostMs == 0) {
      wifiFirstLostMs = millis();
      DEBUG_SERIAL.println("WiFi connection lost; attempting reconnect...");
      WiFi.reconnect();
    } else if (millis() - wifiFirstLostMs > 10000) {
      DEBUG_SERIAL.println("WiFi still down after 10s; restarting...");
      ESP.restart();
    }
    return;
  }
  // Reconnected (or never disconnected): clear the outage timer.
  wifiFirstLostMs = 0;

  if (!StreamClient.available()){
    return;
  }

    char* char_response = catchResponseFromClient(StreamClient);
    //DEBUG_SERIAL.println(char_response);

    JsonDocument doc;
    String moves_temp;
    String latestMove_temp = "";
    String game_status = "";

    if (parseJsonResponse(char_response, doc)) {
        
        moves_temp = doc["state"]["moves"].as<String>();
        game_status = doc["state"]["status"].as<String>();

        if (moves_temp == "null" || moves_temp == ""){
          moves_temp = doc["moves"].as<String>();
          game_status = doc["status"].as<String>();
        }

        if (moves_temp == "null" || moves_temp == ""){
          return;
        }

        moves = moves_temp;
        DEBUG_SERIAL.println(moves);

        latestMove = moves.substring(moves.length() - 4);

        if (latestMove == myLastMove){ 
          myturn = false;
        }
        else{
          myturn = true;
          oppLastMove = latestMove;
        }

        if (game_status == "started")
        {
          is_game_running = true;
        }
        
        if (moves.length() > 3 && game_status != "started"){
          is_game_running = false;
          is_seeking = false;
          myturn = false;
        }
          
    }

    
  
}


/* ---------------------------------------
    Function to set up interupt service routine.
    Sets blinking frequency by isr interval.
    @params[in] void
    @return void
*/
void isr_setup(void) {
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 100000, true);
  timerAlarmEnable(timer);
}

void disableGameTimer() {
  timerAlarmDisable(timer);
}

void enableGameTimer() {
  timerAlarmEnable(timer);
}
void moveStreamHandler() {
  // Two reasons to do work: the 100ms ISR set the flag (periodic
  // heartbeat / WiFi liveness check), or there's already stream data
  // waiting. The second case used to wait for the next ISR tick even
  // when bytes were already queued, which gated AI-move-to-LED
  // latency to ~100ms in the best case (and much worse when stacked
  // with delay(100) calls in getMoveInput's wait-for-end loop).
  if (timerFlag) {
    gameTimerHandler();
    timerFlag = false;
  } else if (StreamClient.available()) {
    gameTimerHandler();
  }
}