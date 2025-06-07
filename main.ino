#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <FS.h>
#include <ArduinoJson.h>

#define FIRMWARE_VERSION "1.0.0"
#define HOSTNAME "WEMOS-OTA-AP"
#define AP_SSID "WEMOS_OTA_" FIRMWARE_VERSION
#define AP_PASSWORD "update123"

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

// Struktur untuk konfigurasi
struct Config {
  char wifi_ssid[32];
  char wifi_password[32];
  bool ota_enabled;
  bool ap_mode;
};

Config config;

void setup() {
  Serial.begin(115200);
  Serial.println("\nBooting...");
  
  // Inisialisasi filesystem
  if (!SPIFFS.begin()) {
    Serial.println("Gagal memount SPIFFS");
    return;
  }

  // Load konfigurasi
  loadConfig();

  // Setup WiFi
  if (config.ap_mode) {
    setupAPMode();
  } else {
    setupStationMode();
  }

  // Setup mDNS
  if (!MDNS.begin(HOSTNAME)) {
    Serial.println("Error setting up MDNS responder!");
  }

  // Setup Web Server
  setupWebServer();

  // Setup OTA Update Server
  httpUpdater.setup(&server, "/update", "admin", "admin123");

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  server.handleClient();
  MDNS.update();
}

void setupAPMode() {
  Serial.println("Setting up AP mode...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP SSID: ");
  Serial.println(AP_SSID);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void setupStationMode() {
  Serial.println("Setting up Station mode...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifi_ssid, config.wifi_password);
  
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi, switching to AP mode");
    config.ap_mode = true;
    saveConfig();
    setupAPMode();
  }
}

void setupWebServer() {
  // Serve static files dari SPIFFS
  server.serveStatic("/", SPIFFS, "/index.html");
  server.serveStatic("/css", SPIFFS, "/css");
  server.serveStatic("/js", SPIFFS, "/js");
  server.serveStatic("/img", SPIFFS, "/img");

  // API Endpoints
  server.on("/api/info", HTTP_GET, handleGetInfo);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/restart", HTTP_POST, handleRestart);
  server.on("/api/files", HTTP_GET, handleGetFiles);
  server.on("/api/files", HTTP_POST, handlePostFile);
  server.on("/api/files", HTTP_DELETE, handleDeleteFile);
  
  // Fallback untuk SPA
  server.onNotFound([]() {
    if (!handleFileRead(server.uri())) {
      server.send(404, "text/plain", "404: Not Found");
    }
  });

  server.begin();
}

void loadConfig() {
  if (SPIFFS.exists("/config.json")) {
    File configFile = SPIFFS.open("/config.json", "r");
    if (configFile) {
      size_t size = configFile.size();
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(), size);
      
      DynamicJsonDocument doc(512);
      DeserializationError error = deserializeJson(doc, buf.get());
      
      if (!error) {
        strlcpy(config.wifi_ssid, doc["wifi_ssid"] | "", sizeof(config.wifi_ssid));
        strlcpy(config.wifi_password, doc["wifi_password"] | "", sizeof(config.wifi_password));
        config.ota_enabled = doc["ota_enabled"] | true;
        config.ap_mode = doc["ap_mode"] | true;
      } else {
        Serial.println("Failed to parse config file");
      }
      configFile.close();
    }
  } else {
    // Default config
    strcpy(config.wifi_ssid, "");
    strcpy(config.wifi_password, "");
    config.ota_enabled = true;
    config.ap_mode = true;
    saveConfig();
  }
}

void saveConfig() {
  DynamicJsonDocument doc(512);
  doc["wifi_ssid"] = config.wifi_ssid;
  doc["wifi_password"] = config.wifi_password;
  doc["ota_enabled"] = config.ota_enabled;
  doc["ap_mode"] = config.ap_mode;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return;
  }
  
  serializeJson(doc, configFile);
  configFile.close();
}

bool handleFileRead(String path) {
  if (path.endsWith("/")) path += "index.html";
  String contentType = getContentType(path);
  if (SPIFFS.exists(path)) {
    File file = SPIFFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".jpg")) return "image/jpeg";
  else if (filename.endsWith(".json")) return "application/json";
  return "text/plain";
}

// Handler untuk API
void handleGetInfo() {
  DynamicJsonDocument doc(256);
  doc["firmware"] = FIRMWARE_VERSION;
  doc["sdk"] = ESP.getSdkVersion();
  doc["chip_id"] = ESP.getChipId();
  doc["flash_size"] = ESP.getFlashChipSize();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["wifi_mode"] = WiFi.getMode();
  doc["ip_address"] = WiFi.localIP().toString();
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleGetConfig() {
  DynamicJsonDocument doc(256);
  doc["wifi_ssid"] = config.wifi_ssid;
  doc["wifi_password"] = config.wifi_password;
  doc["ota_enabled"] = config.ota_enabled;
  doc["ap_mode"] = config.ap_mode;
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handlePostConfig() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (error) {
      server.send(400, "text/plain", "Bad Request");
      return;
    }
    
    if (doc.containsKey("wifi_ssid")) strlcpy(config.wifi_ssid, doc["wifi_ssid"], sizeof(config.wifi_ssid));
    if (doc.containsKey("wifi_password")) strlcpy(config.wifi_password, doc["wifi_password"], sizeof(config.wifi_password));
    if (doc.containsKey("ota_enabled")) config.ota_enabled = doc["ota_enabled"];
    if (doc.containsKey("ap_mode")) config.ap_mode = doc["ap_mode"];
    
    saveConfig();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleRestart() {
  server.send(200, "application/json", "{\"status\":\"restarting\"}");
  delay(1000);
  ESP.restart();
}

void handleGetFiles() {
  Dir dir = SPIFFS.openDir("/");
  DynamicJsonDocument doc(1024);
  JsonArray files = doc.createNestedArray("files");
  
  while (dir.next()) {
    JsonObject file = files.createNestedObject();
    file["name"] = dir.fileName();
    file["size"] = dir.fileSize();
  }
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handlePostFile() {
  HTTPUpload& upload = server.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    Serial.print("handleFileUpload Name: "); Serial.println(filename);
    upload.file = SPIFFS.open(filename, "w");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (upload.file) {
      upload.file.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (upload.file) {
      upload.file.close();
      Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
      server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      server.send(500, "text/plain", "500: couldn't create file");
    }
  }
}

void handleDeleteFile() {
  if (server.hasArg("name")) {
    String filename = server.arg("name");
    if (SPIFFS.exists(filename)) {
      SPIFFS.remove(filename);
      server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      server.send(404, "application/json", "{\"error\":\"file not found\"}");
    }
  } else {
    server.send(400, "application/json", "{\"error\":\"name parameter missing\"}");
  }
}
