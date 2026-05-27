#include "openchessboard.h"


/* ---------------------------------------
 *  setup function for wifi module. 
 *  Checks for wifi firmware  update 
 *  and connects wifi module to network.
 *  @params[in] void
 *  @return void
*/
void wifi_setup(void){

  WiFi.begin(wifi_ssid, wifi_password);

  DEBUG_SERIAL.print("Connecting to Wifi");
  // Bound the connect attempt. Previously this loop ran forever on
  // bad credentials or out-of-range AP — the board would blink a4
  // indefinitely with no escape but a power cycle. After ~30s give
  // up and reboot so the user gets a chance to enter settings mode
  // at the next boot to fix credentials.
  const unsigned long connectStartMs = millis();
  const unsigned long connectTimeoutMs = 30000;
  while (WiFi.status() != WL_CONNECTED) {
      delay(300);
      DEBUG_SERIAL.print(".");
      displayConnectWait();
      if (millis() - connectStartMs > connectTimeoutMs) {
        DEBUG_SERIAL.println("");
        DEBUG_SERIAL.print("WiFi connect timed out after ");
        DEBUG_SERIAL.print(connectTimeoutMs / 1000);
        DEBUG_SERIAL.println("s; rebooting so settings-mode can be chosen on next boot");
        delay(500);
        ESP.restart();
      }
  }
  DEBUG_SERIAL.println("");

  printWiFiStatus();
}

/* ---------------------------------------
 *  function to print wifi status.
 *  @params[in] void
 *  @return void
 *  
*/
void printWiFiStatus(void) {
  DEBUG_SERIAL.print("SSID: ");
  DEBUG_SERIAL.println(WiFi.SSID());

  IPAddress ip = WiFi.localIP();
  DEBUG_SERIAL.print("IP Address: ");
  DEBUG_SERIAL.println(ip);

  long rssi = WiFi.RSSI();
  DEBUG_SERIAL.print("signal strength (RSSI):");
  DEBUG_SERIAL.print(rssi);
  DEBUG_SERIAL.println(" dBm");
}

String fetchMetaData(const char* metadata_url) {
  WiFiClientSecure client;
  client.setInsecure();  

  if (!client.connect("raw.githubusercontent.com", 443)) {
    DEBUG_SERIAL.println("Connection to server failed!");
    return "";
  }

  client.print(String("GET ") + metadata_url + " HTTP/1.1\r\n" +
               "Host: raw.githubusercontent.com\r\n" +
               "User-Agent: ESP32\r\n" +
               "Connection: close\r\n\r\n");

  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {  
      DEBUG_SERIAL.println("Timeout waiting for response headers");
      client.stop();
      return "";
    }
  }

  while (client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  String response = client.readString();
  client.stop();
  //delay(1000);

  return response;
}

bool isNewVersionAvailable(String latest_version) {

    preferences.begin("settings", true);
    String current_version = preferences.getString("firmware", "0.0.0");
    preferences.end();
    DEBUG_SERIAL.printf("Current Version: %s, Latest Version: %s\n", current_version.c_str(), latest_version.c_str());

  return (latest_version != current_version); 
}




bool fetchLatestVersion(String& latest_version) {   
    const char* url = "/jeremiahng11/JKL-Openchessboard/main/release/version.json";
    const int maxRetries = 3;  
    const unsigned long retryDelay = 3000;  
    int fetchRetries = 0;

    String metadata;

    while (fetchRetries < maxRetries) {
        metadata = fetchMetaData(url);
        if (!metadata.isEmpty()) {
            break;  
        } else {
            DEBUG_SERIAL.println("Failed to fetch metadata, retrying...");
            fetchRetries++;
            delay(retryDelay);
        }
    }

    if (metadata.isEmpty()) {
        DEBUG_SERIAL.println("Failed to fetch metadata after retries");
        return false;
    }

    int versionIndex = metadata.indexOf("\"latest_version\":");
    if (versionIndex == -1) {
      DEBUG_SERIAL.println("Failed to find latest_version in JSON.");
      return false;
    }

    versionIndex = metadata.indexOf("\"", versionIndex + 17);  
    int endIndex = metadata.indexOf("\"", versionIndex + 1);
    
    if (versionIndex == -1 || endIndex == -1) {
      DEBUG_SERIAL.println("Failed to extract version.");
      return false;
    }

    latest_version = metadata.substring(versionIndex + 1, endIndex);

    return latest_version;
}

bool downloadFirmware(String latest_version) {
    const int maxRetries = 3;  // Maximum number of retries for both fetching and writing
    size_t chunkSize = 5120;
    const int retryDelay = 3000;

    if (WiFi.status() != WL_CONNECTED) {
        DEBUG_SERIAL.println("WiFi not connected");
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();

    int downloadRetries = 0;
    size_t totalWritten = 0;

    size_t currentByte = 0;
    int contentLength = -1;

    while (downloadRetries < maxRetries) {
        if (client.connect("raw.githubusercontent.com", 443)) {
            DEBUG_SERIAL.println("Connected to server for downloading firmware version: " +  latest_version);

            client.print(String("GET /jeremiahng11/JKL-Openchessboard/main/release/") + "firmware_v" + latest_version +".bin"+ " HTTP/1.1\r\n" +
                         "Host: raw.githubusercontent.com\r\n" +
                         "User-Agent: ESP32\r\n" +
                         "Connection: keep-alive\r\n\r\n");

            unsigned long timeout = millis();
            while (client.available() == 0) {
                if (millis() - timeout > 5000) {
                    DEBUG_SERIAL.println("Timeout waiting for response body");
                    client.stop();
                    break;
                }
            }

            String headers;
            while (client.available()) {
                String line = client.readStringUntil('\n');
                headers += line + "\n";
                if (line.startsWith("Content-Length: ")) {
                    contentLength = line.substring(15).toInt();
                }
                if (line == "\r") {
                    break;
                }
            }

            if (contentLength <= 0) {
                DEBUG_SERIAL.println("Invalid or missing Content-Length header. Aborting.");
                return false;
            } else {
                DEBUG_SERIAL.printf("Total Content Length: %d bytes\n", contentLength);
            }

            if (Update.begin(contentLength)) {
                uint8_t buffer[chunkSize];
                while (client.connected() && totalWritten < contentLength) {
                    String rangeHeader = "Range: bytes=" + String(currentByte) + "-" + String(currentByte + chunkSize - 1);
                    client.print(String("GET /jeremiahng11/JKL-Openchessboard/main/release/") + "firmware_v" + latest_version + " HTTP/1.1\r\n" +
                                 "Host: raw.githubusercontent.com\r\n" +
                                 "User-Agent: ESP32\r\n" +
                                 "Connection: keep-alive\r\n" +
                                 rangeHeader + "\r\n\r\n");

                    unsigned long timeout = millis();
                    while (client.available() == 0) {
                        if (millis() - timeout > 5000) {
                            DEBUG_SERIAL.println("Timeout waiting for response body");
                            client.stop();
                            break;
                        }
                    }

                    size_t len = client.read(buffer, sizeof(buffer));
                    if (len > 0) {
                        size_t written = Update.write(buffer, len);
                        if (written != len) {
                            DEBUG_SERIAL.println("Error: Mismatch in written and read data");
                            break;
                        }
                        totalWritten += written;
                        currentByte += written;
                        DEBUG_SERIAL.printf("Written %d/%d bytes\n", totalWritten, contentLength);

                    } else {
                        DEBUG_SERIAL.println("No data available from client. Retrying...");
                        break;
                    }
                }

                if (Update.end() && Update.isFinished()) {

                    preferences.begin("settings", false);
                    preferences.putString("firmware", latest_version);
                    preferences.end();
                    DEBUG_SERIAL.println("Update finished. Rebooting...");
                    delay(3000);
                    ESP.restart();
                    return true;
                } else {
                    DEBUG_SERIAL.printf("Update failed: %s\n", Update.getError());
                }
            } else {
                DEBUG_SERIAL.println("Failed to begin update.");
            }
        } else {
            DEBUG_SERIAL.println("Failed to connect to server, retrying...");
        }

        downloadRetries++;
        delay(retryDelay);
    }

    return false;
}

void wifi_firmwareUpdate() {
    // Cache the last successful version-check timestamp so we don't
    // hit GitHub on every cold boot — previously every reboot paid
    // a 1-5s fetch just to confirm version equality. We re-check at
    // most once per hour; user can still force a check via settings
    // AP if needed.
    const unsigned long checkIntervalMs = 60UL * 60UL * 1000UL; // 1h
    preferences.begin("settings", true);
    unsigned long lastCheckEpoch = preferences.getULong("ota_check_ms", 0);
    preferences.end();
    const unsigned long nowMs = millis();
    // Note: millis() resets across boots; we treat lastCheckEpoch as
    // a millis-since-boot snapshot at the last *successful* check.
    // After a reboot lastCheckEpoch (if non-zero) is always >= nowMs's
    // valid range, so we skip the OTA check until the next 1h tick —
    // i.e. one fetch per hour of uptime rather than per cold boot.
    if (lastCheckEpoch != 0 && nowMs < lastCheckEpoch + checkIntervalMs) {
        DEBUG_SERIAL.println("Skipping OTA check (last check < 1h ago)");
        return;
    }

    String latest_version;
    if (fetchLatestVersion(latest_version)){

      if (!isNewVersionAvailable(latest_version)) {
          DEBUG_SERIAL.println("Firmware is up to date!");
          preferences.begin("settings", false);
          preferences.putULong("ota_check_ms", millis());
          preferences.end();
          return;
      }
      else{
        DEBUG_SERIAL.println("Download firmware:" + latest_version);
        setStateUpdating();
        update_flipstate = true;
        displayUpdateWait();
        if (!downloadFirmware(latest_version)) {
            DEBUG_SERIAL.println("Firmware update failed");
            return;
        }
      }

    }
    else{
      DEBUG_SERIAL.println("No new Version available");
      return;
    }
}

void validateFirmware() {
    // Inspect-only: log the current OTA partition state but do NOT
    // call esp_ota_mark_app_valid_cancel_rollback() here. Marking the
    // partition valid before we've proven connectivity defeats the
    // rollback safety net — if a freshly-OTA'd build can't reach
    // Lichess (broken WiFi, expired token, bad endpoint hard-code,
    // etc.) the bootloader would never roll back to the previous
    // known-good firmware. confirmFirmwareWorking() does the actual
    // mark after the first successful Lichess request.
    const esp_partition_t* running = esp_ota_get_running_partition();
    DEBUG_SERIAL.printf("Running partition: %s\n", running->label);

    esp_ota_img_states_t ota_state;
    esp_err_t result = esp_ota_get_state_partition(running, &ota_state);

    if (result == ESP_OK) {
        DEBUG_SERIAL.printf("OTA state: %d\n", ota_state);
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            DEBUG_SERIAL.println("Firmware pending verify (mark-valid deferred until connectivity proven).");
        } else if (ota_state == ESP_OTA_IMG_VALID) {
            DEBUG_SERIAL.println("Firmware already valid.");
        } else if (ota_state == ESP_OTA_IMG_INVALID) {
            DEBUG_SERIAL.println("Firmware marked as invalid.");
        } else if (ota_state == ESP_OTA_IMG_ABORTED) {
            DEBUG_SERIAL.println("Firmware update was aborted.");
        } else {
            DEBUG_SERIAL.printf("Unknown OTA state: %d\n", ota_state);
        }
    } else {
        DEBUG_SERIAL.printf("Failed to get OTA state: %s\n", esp_err_to_name(result));
    }
}

/// Call after the first successful Lichess request to confirm the
/// freshly-OTA'd image actually works end-to-end. Safe to call
/// multiple times — only acts when the partition is still in the
/// PENDING_VERIFY state.
void confirmFirmwareWorking() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) != ESP_OK) return;
    if (ota_state != ESP_OTA_IMG_PENDING_VERIFY) return;
    esp_err_t res = esp_ota_mark_app_valid_cancel_rollback();
    if (res == ESP_OK) {
        DEBUG_SERIAL.println("Firmware marked valid after first successful Lichess request.");
    } else {
        DEBUG_SERIAL.printf("Failed to mark firmware valid: %s\n", esp_err_to_name(res));
    }
}
