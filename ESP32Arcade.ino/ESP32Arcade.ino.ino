// ESP32 Arcade Login Sketch
// Features: AP mode, default Admin account, forced password change, credential hashing, embedded emulator

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "mbedtls/sha256.h"

// Wi-Fi Access Point credentials
const char* AP_SSID     = "ESP32_Arcade_AP";
const char* AP_PASSWORD = "ChangeMe1!";

// Default user credentials
const char* DEFAULT_USER = "Admin";
const char* DEFAULT_PASS = "ChangeMe1!";

// Web server instance on port 80
AsyncWebServer server(80);

// Session structure to hold username and token
struct Session {
  String user;
  String token;
};
#define MAX_SESSIONS 5
Session sessions[MAX_SESSIONS];

//==============================================================================
// Utility Functions
//==============================================================================

// Generate a random alphanumeric token of given length
String generateToken(size_t length = 32) {
  const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  String token;
  for (size_t i = 0; i < length; i++) {
    token += charset[random(0, sizeof(charset) - 1)];
  }
  return token;
}

// Compute SHA-256 hash of input string and return as hex
String sha256Hash(const String &input) {
  byte hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, (const unsigned char*)input.c_str(), input.length());
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);

  char buf[65];
  for (int i = 0; i < 32; i++) {
    sprintf(buf + i * 2, "%02x", hash[i]);
  }
  buf[64] = '\0';
  return String(buf);
}

//==============================================================================
// Credentials Storage (LittleFS)
//==============================================================================

// Load users JSON from LittleFS; create default if missing
bool loadUsers(JsonDocument &doc) {
  if (!LittleFS.exists("/users.json")) {
    StaticJsonDocument<1024> initDoc;
    JsonObject obj = initDoc.as<JsonObject>();
    obj[DEFAULT_USER] = sha256Hash(DEFAULT_PASS);
    File f = LittleFS.open("/users.json", FILE_WRITE);
    if (!f) return false;
    serializeJson(initDoc, f);
    f.close();
  }
  File file = LittleFS.open("/users.json", FILE_READ);
  if (!file) return false;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  return !err;
}

// Save users JSON to LittleFS
bool saveUsers(JsonDocument &doc) {
  File file = LittleFS.open("/users.json", FILE_WRITE);
  if (!file) return false;
  if (serializeJson(doc, file) == 0) {
    file.close();
    return false;
  }
  file.close();
  return true;
}

//==============================================================================
// Session Validation
//==============================================================================

// Validate session token extracted from Cookie header
bool validateSession(AsyncWebServerRequest *request) {
  if (!request->hasHeader("Cookie")) return false;
  String cookie = request->header("Cookie");
  int idx = cookie.indexOf("token=");
  if (idx < 0) return false;
  String token = cookie.substring(idx + 6);
  int sep = token.indexOf(';');
  if (sep > 0) token = token.substring(0, sep);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].token == token) return true;
  }
  return false;
}

// Retrieve username associated with valid session token
String getSessionUser(AsyncWebServerRequest *request) {
  if (!request->hasHeader("Cookie")) return "";
  String cookie = request->header("Cookie");
  int idx = cookie.indexOf("token=");
  if (idx < 0) return "";
  String token = cookie.substring(idx + 6);
  int sep = token.indexOf(';');
  if (sep > 0) token = token.substring(0, sep);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].token == token) return sessions[i].user;
  }
  return "";
}

//==============================================================================
// Setup and Route Handlers
//==============================================================================

void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(0));

  // Initialize LittleFS
  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed");
    return;
  }

  // Load or create default users
  StaticJsonDocument<1024> userDoc;
  if (!loadUsers(userDoc)) {
    Serial.println("Failed to initialize users.json");
    return;
  }

  // Clear any existing sessions
  for (int i = 0; i < MAX_SESSIONS; i++) {
    sessions[i].user = "";
    sessions[i].token = "";
  }

  // Start ESP32 as Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP started: ");
  Serial.println(AP_SSID);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  //--------------------------------------------------------------------------
  // Redirect root to login page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/login");
  });

  //--------------------------------------------------------------------------
  // Serve login page (GET)
  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
    const char* html =
      "<!DOCTYPE html>"
      "<html>"
      "<head><title>Login</title></head>"
      "<body>"
      "  <h2>Login</h2>"
      "  <form method=\"POST\" action=\"/login\">"
      "    Username: <input type=\"text\" name=\"username\" required><br>"
      "    Password: <input type=\"password\" name=\"password\" required><br>"
      "    <input type=\"submit\" value=\"Login\">"
      "  </form>"
      "</body>"
      "</html>";
    request->send(200, "text/html", html);
  });

  //--------------------------------------------------------------------------
  // Handle login submission (POST)
  server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("username", true) || !request->hasParam("password", true)) {
      request->send(400, "text/plain", "Missing credentials");
      return;
    }

    String user = request->getParam("username", true)->value();
    String pass = request->getParam("password", true)->value();

    StaticJsonDocument<1024> doc;
    if (!loadUsers(doc)) {
      request->send(500, "text/plain", "User load error");
      return;
    }

    JsonObject obj = doc.as<JsonObject>();
    if (!obj.containsKey(user)) {
      request->send(401, "text/plain", "Invalid credentials");
      return;
    }

    String storedHash = obj[user].as<String>();
    if (sha256Hash(pass) != storedHash) {
      request->send(401, "text/plain", "Invalid credentials");
      return;
    }

    // Create a new session
    String token = generateToken();
    for (int i = 0; i < MAX_SESSIONS; i++) {
      if (sessions[i].token.isEmpty()) {
        sessions[i].user  = user;
        sessions[i].token = token;
        break;
      }
    }

    AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "");
    response->addHeader("Set-Cookie", "token=" + token + "; HttpOnly; Path=/");

    // If default admin logging in with default password, force password change
    if (user == String(DEFAULT_USER) && sha256Hash(pass) == sha256Hash(DEFAULT_PASS)) {
      response->addHeader("Location", "/change_password");
    } else {
      response->addHeader("Location", "/emulator");
    }

    request->send(response);
  });

  //--------------------------------------------------------------------------
  // Serve change password form (GET)
  server.on("/change_password", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!validateSession(request)) {
      request->redirect("/login");
      return;
    }
    String user = getSessionUser(request);
    if (user != String(DEFAULT_USER)) {
      request->send(403, "text/plain", "Forbidden");
      return;
    }
    const char* html =
      "<!DOCTYPE html>"
      "<html>"
      "<head><title>Change Password</title></head>"
      "<body>"
      "  <h2>Change Default Password</h2>"
      "  <form method=\"POST\" action=\"/change_password\">"
      "    New Password: <input type=\"password\" name=\"newpass\" required><br>"
      "    Confirm: <input type=\"password\" name=\"confirmpass\" required><br>"
      "    <input type=\"submit\" value=\"Update\">"
      "  </form>"
      "</body>"
      "</html>";
    request->send(200, "text/html", html);
  });

  //--------------------------------------------------------------------------
  // Handle password update (POST)
  server.on("/change_password", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!validateSession(request)) {
      request->redirect("/login");
      return;
    }
    String user = getSessionUser(request);
    if (user != String(DEFAULT_USER)) {
      request->send(403, "text/plain", "Forbidden");
      return;
    }
    if (!request->hasParam("newpass", true) || !request->hasParam("confirmpass", true)) {
      request->send(400, "text/plain", "Missing parameters");
      return;
    }
    String newp = request->getParam("newpass", true)->value();
    String conf = request->getParam("confirmpass", true)->value();
    if (newp != conf) {
      request->send(400, "text/plain", "Passwords mismatch");
      return;
    }
    StaticJsonDocument<1024> doc;
    loadUsers(doc);
    JsonObject obj = doc.as<JsonObject>();
    obj[user] = sha256Hash(newp);
    saveUsers(doc);
    AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "");
    response->addHeader("Location", "/emulator");
    request->send(response);
  });

  //--------------------------------------------------------------------------
  // Disable registration endpoint
  server.on("/register", HTTP_ANY, [](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Registration disabled");
  });

  //--------------------------------------------------------------------------
  // Serve emulator page (protected)
  server.on("/emulator", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!validateSession(request)) {
      request->redirect("/login");
      return;
    }
    const char* html =
      "<!DOCTYPE html>"
      "<html>"
      "<head><title>ESP32 Arcade Emulator</title></head>"
      "<body>"
      "  <h2>ESP32 Arcade</h2>"
      "  <p>Choose a game:</p>"
      "  <iframe src=\"https://jsnes.app\" width=\"800\" height=\"600\"></iframe>"
      "</body>"
      "</html>";
    request->send(200, "text/html", html);
  });

  // Start the server
  server.begin();
}

void loop() {
  // AsyncWebServer runs in background; no code needed here
}
