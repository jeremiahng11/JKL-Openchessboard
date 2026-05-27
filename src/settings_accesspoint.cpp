#include "openchessboard.h"
#include "html_content.h"

// Create a web server on port 80 (default HTTP port)

// Define Wi-Fi AP (Access Point) credentials
const char* ssidAP = "OPENCHESSBOARD"; // SSID for the ESP32 access point
const char* passwordAP = "";
const char* domainName = "OPENCHESSBOARD";
// Create a web server on port 80 (default HTTP port)
WiFiServer APserver(80);

Preferences preferences;


// HTML-escape a string before embedding in the confirmation page —
// previously the form values (SSID, password, token) were echoed back
// to the browser raw, so a SSID like '<script>alert(1)</script>' would
// execute. Reflected XSS at provisioning time isn't catastrophic but
// it's a free fix.
String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += c;        break;
    }
  }
  return out;
}

// Function to extract form data from the POST request
String getFormData(String request, String key) {
  int startIndex = request.indexOf(key + "=");
  if (startIndex == -1) return "";

  startIndex += key.length() + 1;  // Skip the key part
  int endIndex = request.indexOf("&", startIndex);
  if (endIndex == -1) endIndex = request.length();

  return request.substring(startIndex, endIndex);
}

void AP_setup(WiFiServer &APserver) {
  // Start ESP32 in Access Point mode
  if (WiFi.softAP(ssidAP, passwordAP)) {
    DEBUG_SERIAL.println("Access Point started");
    DEBUG_SERIAL.print("SSID: ");
    DEBUG_SERIAL.println(ssidAP);

    // Display IP address
    IPAddress IP = WiFi.softAPIP();
    DEBUG_SERIAL.print("AP IP address: ");
    DEBUG_SERIAL.println(IP);

    // Start the server
    APserver.begin();
    DEBUG_SERIAL.println("AP Server started");

    // Initialize mDNS
    if (MDNS.begin(domainName)) {
      DEBUG_SERIAL.print("mDNS responder started. Access your device at http://");
      Serial.print(domainName);
      DEBUG_SERIAL.println(".local");
    } else {
      DEBUG_SERIAL.println("Error setting up mDNS responder!");
    }
  } else {
    DEBUG_SERIAL.println("Failed to start Access Point");
  }
}

bool handleAPClientRequest(WiFiClient &client) {
  String currentLine = "";
  bool is_submitted = false;
  String ssid = "";
  String password = "";
  String token = "";
  String gameMode = "";
  String startupType = "";

  // Wait for data from the client
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      // The HTTP request body for /submit includes the form fields
      // password=... and token=... in plaintext URL-encoded form.
      // Echoing every byte to serial leaked the credentials on every
      // settings submission. Removed; re-enable locally for protocol
      // debugging if needed.
      // Serial.write(c);

      // Read the HTTP request line
      if (c == '\n') {
        // Check for the end of the request header
        if (currentLine.length() == 0) {
          // Send HTTP headers
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connection: close");
          client.println("");
          // Serve the HTML content — don't println the full template
          // body to serial; it's hundreds of bytes of noise.
          client.print(htmlContent);

          break;
        } else {
          // Reset current line after reading the header line
          currentLine = "";
        }
      } else if (c != '\r') {
        // Add to current line
        currentLine += c;
      }

      // If form is submitted
      if (currentLine.endsWith("POST /submit")) {
        String requestBody = client.readString();
        // Parse the form data
        ssid = getFormData(requestBody, "ssid");
        password = getFormData(requestBody, "password");
        token = getFormData(requestBody, "token");
        gameMode = getFormData(requestBody, "gameMode");
        startupType = getFormData(requestBody, "startupType");

        // Reject obviously-bad submissions before touching NVS so a
        // malformed POST can't leave the board with an empty SSID
        // and an unrecoverable boot loop. SSID is the load-bearing
        // field; everything else can in principle be empty.
        if (ssid.length() == 0 || ssid.length() > 64 ||
            password.length() > 64 || token.length() > 128 ||
            gameMode.length() > 32 || startupType.length() > 16) {
          DEBUG_SERIAL.println("Rejected settings submission: missing/oversized fields");
          client.println("HTTP/1.1 400 Bad Request");
          client.println("Content-Type: text/html");
          client.println("Connection: close");
          client.println();
          client.print("<html><body><h2>Invalid form submission</h2>"
                       "<p>SSID is required; field lengths must be reasonable.</p>"
                       "<p><a href=\"/\">Back</a></p></body></html>");
          // Do not set is_submitted; loop again for another attempt.
          break;
        }

        // Save to flash
        preferences.begin("settings", false); // read/write
        preferences.putString("ssid", ssid);
        preferences.putString("password", password);
        preferences.putString("token", token);
        preferences.putString("gameMode", gameMode);
        preferences.putString("startupType", startupType);
        preferences.end();

        // Send a confirmation page — htmlEscape every interpolated
        // user-supplied value so a hostile SSID/token can't inject
        // markup or scripts into the page rendered to the
        // configuring user's browser.
        String confirmation = "<html><head><style>\n";
        confirmation += "body { font-family: Arial, sans-serif; background-color: #5c5d5e; color: #ec8703; text-align: center; padding: 20px; }\n";
        confirmation += "button { background-color: #ec8703; color: white; border: none; padding: 15px; font-size: 16px; border-radius: 5px; cursor: not-allowed; }\n";
        confirmation += "</style></head><body>";
        confirmation += "<h2>Settings Submitted!</h2>";
        // Confirm only the non-sensitive fields verbatim. Echo password
        // and token as length-only so a passer-by glancing at the screen
        // or a screenshot shared for support doesn't expose credentials.
        confirmation += "<p>SSID: " + htmlEscape(ssid) + "</p>";
        confirmation += "<p>Password: <em>(" + String(password.length()) + " chars saved)</em></p>";
        confirmation += "<p>Token: <em>(" + String(token.length()) + " chars saved)</em></p>";
        confirmation += "<p>Game Mode: " + htmlEscape(urlDecode(gameMode)) + "</p>";
        confirmation += "<p>Startup Type: " + htmlEscape(startupType) + "</p>";
        confirmation += "<p>Restarting the device... Please wait.</p>";
        confirmation += "<button disabled>Restarting...</button>";
        confirmation += "</body></html>";

        client.print(confirmation);
        is_submitted = true;
        break;
      }
    }
  }

  // Close the connection
  client.stop();
  return is_submitted;
}


void run_APsettings(void){
  AP_setup(APserver);
  bool settings_updated = false;
  while(!settings_updated){
    WiFiClient client = APserver.available();

    if (client) {
      settings_updated = handleAPClientRequest(client);
      DEBUG_SERIAL.println("\nClient handling...");
    }
  }
  // Tear down the AP cleanly so the broadcast SSID stops immediately
  // rather than continuing to beacon for the ~2s delay before reboot.
  WiFi.softAPdisconnect(true);
  delay(500);
  DEBUG_SERIAL.println("\nRestarting");
  ESP.restart();
}