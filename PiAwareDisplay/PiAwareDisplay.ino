// ============================================================
//  PiAware Display
//  A live ADS-B aircraft display for the LilyGO T-Display S3
//
//  Reads aircraft data from a local PiAware / dump1090 receiver
//  and looks up flight routes via the FlightAware AeroAPI.
//
//  Two views:
//    List view   — up to 7 nearest aircraft sorted by distance
//    Detail view — full info on the selected aircraft
//
//  Button 14 = scroll forward / next aircraft
//  Button 0  = scroll back / previous aircraft
//  Both together = toggle between list and detail view
//
//  See config.h for all settings.
//  See README.md for full setup instructions.
// ============================================================

#include <TFT_eSPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "config.h"

// --- Display ---
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

bool lastButtonNextState = HIGH;
bool lastButtonPrevState = HIGH;
unsigned long lastUpdate  = 0;
int selectedIndex         = 0;
bool detailView           = false;

// --- Route cache ---
const int ROUTE_CACHE_SIZE = 40;

struct RouteEntry {
  String callsign;
  String origin;   // IATA code e.g. "LHR"
  String dest;     // IATA code e.g. "JFK"
  String airline;
  bool   found;    // false = looked up, no data — don't retry
};

RouteEntry routeCache[ROUTE_CACHE_SIZE];
int routeCacheCount = 0;

// --- Aircraft store ---
const int MAX_AC = 20;

struct Aircraft {
  String hex;
  String flight;
  float  lat;
  float  lon;
  int    altFt;
  float  distKm;
  bool   hasPos;
  bool   onGround;
  int    seen;
};

Aircraft aircraft[MAX_AC];
int aircraftCount = 0;
int totalSeen     = 0;

// =====================
// --- HELPERS ---
// =====================

float distanceKm(float lat1, float lon1, float lat2, float lon2) {
  const float R = 6371.0f;
  float dLat = radians(lat2 - lat1);
  float dLon = radians(lon2 - lon1);
  float a = sin(dLat/2)*sin(dLat/2) +
            cos(radians(lat1))*cos(radians(lat2))*
            sin(dLon/2)*sin(dLon/2);
  return R * 2 * atan2(sqrt(a), sqrt(1-a));
}

void displayMessage(String l1, String l2 = "") {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10); tft.println(l1);
  if (l2 != "") { tft.setCursor(10, 40); tft.println(l2); }
}

// =====================
// --- ROUTE CACHE ---
// =====================

RouteEntry* findInCache(String callsign) {
  for (int i = 0; i < routeCacheCount; i++) {
    if (routeCache[i].callsign == callsign) return &routeCache[i];
  }
  return nullptr;
}

void addToCache(RouteEntry entry) {
  if (routeCacheCount < ROUTE_CACHE_SIZE) {
    routeCache[routeCacheCount++] = entry;
  } else {
    // Drop oldest entry, shift down
    for (int i = 0; i < ROUTE_CACHE_SIZE - 1; i++) {
      routeCache[i] = routeCache[i + 1];
    }
    routeCache[ROUTE_CACHE_SIZE - 1] = entry;
  }
}

RouteEntry fetchRoute(String callsign) {
  RouteEntry result;
  result.callsign = callsign;
  result.found    = false;

  if (callsign == "") {
    addToCache(result);
    return result;
  }

  String url = "https://aeroapi.flightaware.com/aeroapi/flights/";
  url += callsign;
  url += "?max_pages=1";

  USBSerial.println("AeroAPI: " + url);

  HTTPClient http;
  http.begin(url);
  http.addHeader("x-apikey", AEROAPI_KEY);
  http.setTimeout(10000);

  int httpCode = http.GET();
  USBSerial.println("AeroAPI HTTP: " + String(httpCode));

  if (httpCode != 200) {
    http.end();
    addToCache(result);
    return result;
  }

  String payload = http.getString();
  http.end();

  USBSerial.println("AeroAPI: " + payload.substring(0, 200));

  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, payload)) {
    addToCache(result);
    return result;
  }

  JsonArray flights = doc["flights"].as<JsonArray>();
  if (flights.isNull() || flights.size() == 0) {
    addToCache(result);
    return result;
  }

  // Most recent flight is first
  JsonObject flight = flights[0];

  String origin  = "";
  String dest    = "";
  String airline = "";

  if (!flight["origin"].isNull()) {
    origin = flight["origin"]["code_iata"] | "";
    if (origin == "") origin = flight["origin"]["code"] | "";
  }

  if (!flight["destination"].isNull()) {
    dest = flight["destination"]["code_iata"] | "";
    if (dest == "") dest = flight["destination"]["code"] | "";
  }

  airline = flight["operator"] | "";
  if (airline == "") airline = flight["airline"] | "";

  if (origin != "" || dest != "") {
    result.origin  = origin;
    result.dest    = dest;
    result.airline = airline;
    result.found   = true;
  }

  addToCache(result);
  USBSerial.println("Route: " + origin + " -> " + dest);
  return result;
}

RouteEntry getRoute(String callsign) {
  callsign.trim();
  if (callsign == "") {
    RouteEntry empty; empty.found = false; return empty;
  }

  RouteEntry* cached = findInCache(callsign);
  if (cached != nullptr) {
    USBSerial.println("Cache hit: " + callsign);
    return *cached;
  }

  USBSerial.println("Cache miss: " + callsign);
  return fetchRoute(callsign);
}

// =====================
// --- FETCH AIRCRAFT ---
// =====================

void fetchAircraft() {
  HTTPClient http;
  http.begin(PIAWARE_URL);
  http.setTimeout(5000);
  int httpCode = http.GET();

  if (httpCode != 200) {
    USBSerial.println("PiAware HTTP: " + String(httpCode));
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(32768);
  if (deserializeJson(doc, payload)) {
    USBSerial.println("PiAware JSON error");
    return;
  }

  JsonArray acArray = doc["aircraft"].as<JsonArray>();
  totalSeen = acArray.size();

  Aircraft temp[MAX_AC];
  int tempCount = 0;

  for (JsonObject ac : acArray) {
    if (tempCount >= MAX_AC) break;

    float seen = ac["seen"] | 999.0f;
    if (seen > 30) continue;

    String hex    = ac["hex"]    | "";
    String flight = ac["flight"] | "";
    flight.trim();

    float lat = 0, lon = 0;
    bool hasPos = false;
    if (!ac["lat"].isNull() && !ac["lon"].isNull()) {
      lat    = ac["lat"].as<float>();
      lon    = ac["lon"].as<float>();
      hasPos = true;
    }

    int  altFt    = 0;
    bool onGround = false;
    if (ac["alt_baro"].is<int>()) {
      altFt = ac["alt_baro"].as<int>();
    } else {
      String altStr = ac["alt_baro"] | "";
      if (altStr == "ground") onGround = true;
    }

    float dist = hasPos ?
      distanceKm(HOME_LAT, HOME_LON, lat, lon) : 9999.0f;

    temp[tempCount].hex      = hex;
    temp[tempCount].flight   = flight;
    temp[tempCount].lat      = lat;
    temp[tempCount].lon      = lon;
    temp[tempCount].altFt    = altFt;
    temp[tempCount].distKm   = dist;
    temp[tempCount].hasPos   = hasPos;
    temp[tempCount].onGround = onGround;
    temp[tempCount].seen     = (int)seen;
    tempCount++;
  }

  // Sort by distance (bubble sort — small dataset)
  for (int i = 0; i < tempCount - 1; i++) {
    for (int j = 0; j < tempCount - i - 1; j++) {
      if (temp[j].distKm > temp[j+1].distKm) {
        Aircraft swap = temp[j];
        temp[j]       = temp[j+1];
        temp[j+1]     = swap;
      }
    }
  }

  aircraftCount = tempCount;
  for (int i = 0; i < tempCount; i++) aircraft[i] = temp[i];

  if (selectedIndex >= aircraftCount)
    selectedIndex = max(0, aircraftCount - 1);
}

// =====================
// --- DRAW LIST ---
// =====================

void drawList() {
  sprite.fillSprite(TFT_BLACK);

  // Header
  sprite.setTextColor(TFT_CYAN);
  sprite.setTextSize(2);
  sprite.setCursor(10, 5);
  sprite.print("ADS-B  ");
  sprite.setTextColor(TFT_DARKGREY);
  sprite.setTextSize(1);
  sprite.setCursor(115, 10);
  sprite.print(String(aircraftCount) + " shown / " +
               String(totalSeen) + " tracked");

  sprite.drawLine(0, 26, 320, 26, TFT_DARKGREY);

  // Column headers
  sprite.setTextColor(TFT_DARKGREY);
  sprite.setTextSize(1);
  sprite.setCursor(5,   30); sprite.print("KM");
  sprite.setCursor(38,  30); sprite.print("FLIGHT");
  sprite.setCursor(108, 30); sprite.print("ALT");
  sprite.setCursor(165, 30); sprite.print("FROM");
  sprite.setCursor(210, 30); sprite.print("TO");
  sprite.setCursor(248, 30); sprite.print("AIRLINE");
  sprite.drawLine(0, 39, 320, 39, TFT_DARKGREY);

  if (aircraftCount == 0) {
    sprite.setTextColor(TFT_DARKGREY);
    sprite.setTextSize(2);
    sprite.setCursor(10, 80);
    sprite.println("No aircraft");
    sprite.setCursor(10, 105);
    sprite.println("in range");
    sprite.pushSprite(0, 0);
    return;
  }

  int y     = 44;
  int shown = 0;

  for (int i = 0; i < aircraftCount && shown < 7; i++) {
    Aircraft& ac = aircraft[i];

    bool isSelected = (i == selectedIndex);
    if (isSelected)
      sprite.fillRect(0, y - 1, 320, 19, 0x1082);

    uint16_t col = isSelected ? TFT_YELLOW : TFT_WHITE;

    sprite.setTextColor(col);
    sprite.setTextSize(1);

    // Distance
    sprite.setCursor(5, y + 4);
    if (ac.distKm < 9990) {
      if (ac.distKm < 10.0f)
        sprite.print(String(ac.distKm, 1));
      else
        sprite.print(String((int)round(ac.distKm)));
    } else {
      sprite.print("--");
    }

    // Flight number
    sprite.setCursor(38, y + 4);
    String fl = ac.flight != "" ? ac.flight : ac.hex;
    if (fl.length() > 8) fl = fl.substring(0, 8);
    sprite.print(fl);

    // Altitude
    sprite.setCursor(108, y + 4);
    if (ac.onGround) {
      sprite.setTextColor(TFT_DARKGREY);
      sprite.print("GND");
    } else {
      sprite.setTextColor(col);
      sprite.print(String(ac.altFt) + "ft");
    }

    // Route from cache
    sprite.setTextColor(col);
    RouteEntry* cached = findInCache(ac.flight);
    if (cached != nullptr && cached->found) {
      sprite.setCursor(165, y + 4);
      sprite.print(cached->origin != "" ? cached->origin : "--");
      sprite.setCursor(210, y + 4);
      sprite.print(cached->dest   != "" ? cached->dest   : "--");
      sprite.setTextColor(TFT_DARKGREY);
      sprite.setCursor(248, y + 4);
      String al = cached->airline;
      if (al.length() > 8) al = al.substring(0, 8);
      sprite.print(al);
    } else {
      sprite.setTextColor(TFT_DARKGREY);
      sprite.setCursor(165, y + 4);
      // "..." = not yet looked up, "---" = looked up but not found
      sprite.print(cached != nullptr ? "---" : "...");
      sprite.setCursor(210, y + 4);
      sprite.print(cached != nullptr ? "---" : "...");
    }

    y += 19;
    sprite.drawLine(0, y - 1, 320, y - 1, 0x2104);
    shown++;
  }

  // Footer
  sprite.setTextColor(TFT_DARKGREY);
  sprite.setTextSize(1);
  sprite.setCursor(10, 172);
  sprite.print("14=next  0=prev  both=detail  " +
               String(routeCacheCount) + " cached");

  sprite.pushSprite(0, 0);
}

// =====================
// --- DRAW DETAIL ---
// =====================

void drawDetail() {
  if (aircraftCount == 0) { drawList(); return; }

  Aircraft& ac = aircraft[selectedIndex];

  // Trigger route lookup if not already cached
  RouteEntry route = getRoute(ac.flight);

  sprite.fillSprite(TFT_BLACK);

  // Flight number — large
  String fl = ac.flight != "" ? ac.flight : ac.hex;
  sprite.setTextColor(TFT_YELLOW);
  sprite.setTextSize(3);
  sprite.setCursor(10, 5);
  sprite.print(fl);

  // ICAO hex + index
  sprite.setTextColor(TFT_DARKGREY);
  sprite.setTextSize(1);
  sprite.setCursor(210, 8);
  sprite.print("ICAO: " + ac.hex);
  sprite.setCursor(210, 18);
  sprite.print(String(selectedIndex + 1) + "/" + String(aircraftCount));

  sprite.drawLine(0, 35, 320, 35, TFT_DARKGREY);

  // Route
  if (route.found) {
    sprite.setTextColor(TFT_WHITE);
    sprite.setTextSize(3);
    sprite.setCursor(10, 42);
    String org = route.origin != "" ? route.origin : "???";
    String dst = route.dest   != "" ? route.dest   : "???";
    sprite.print(org);
    sprite.setTextColor(TFT_DARKGREY);
    sprite.setTextSize(2);
    sprite.setCursor(10 + (org.length() * 18) + 4, 48);
    sprite.print("->");
    sprite.setTextColor(TFT_WHITE);
    sprite.setTextSize(3);
    sprite.setCursor(10 + (org.length() * 18) + 36, 42);
    sprite.print(dst);

    if (route.airline != "") {
      sprite.setTextColor(TFT_CYAN);
      sprite.setTextSize(1);
      sprite.setCursor(10, 72);
      String al = route.airline;
      if (al.length() > 30) al = al.substring(0, 30);
      sprite.print(al);
    }
  } else {
    sprite.setTextColor(TFT_DARKGREY);
    sprite.setTextSize(2);
    sprite.setCursor(10, 45);
    sprite.print(ac.flight != "" ? "Route unknown" : "No callsign");
  }

  sprite.drawLine(0, 84, 320, 84, TFT_DARKGREY);

  // Data row 1
  sprite.setTextColor(TFT_DARKGREY);
  sprite.setTextSize(1);
  sprite.setCursor(10,  90); sprite.print("ALTITUDE");
  sprite.setCursor(130, 90); sprite.print("DISTANCE");
  sprite.setCursor(240, 90); sprite.print("LAST SEEN");

  sprite.setTextColor(TFT_WHITE);
  sprite.setTextSize(2);
  sprite.setCursor(10, 100);
  if (ac.onGround)
    sprite.print("GROUND");
  else
    sprite.print(String(ac.altFt) + "ft");

  sprite.setCursor(130, 100);
  if (ac.distKm < 9990)
    sprite.print(String(ac.distKm, 1) + "km");
  else
    sprite.print("No pos");

  sprite.setCursor(240, 100);
  sprite.print(String(ac.seen) + "s");

  sprite.drawLine(0, 124, 320, 124, TFT_DARKGREY);

  // Position
  sprite.setTextColor(TFT_DARKGREY);
  sprite.setTextSize(1);
  sprite.setCursor(10, 129);
  if (ac.hasPos) {
    char posBuf[40];
    snprintf(posBuf, sizeof(posBuf), "Pos: %.4f, %.4f", ac.lat, ac.lon);
    sprite.print(posBuf);
  } else {
    sprite.print("Position not available");
  }

  // Cache status
  sprite.setCursor(10, 142);
  RouteEntry* cached = findInCache(ac.flight);
  if (cached == nullptr)
    sprite.print("Route: fetching...");
  else if (cached->found)
    sprite.print("Route: cached  (" + String(routeCacheCount) + " total)");
  else
    sprite.print("Route: not found in AeroAPI");

  sprite.drawLine(0, 155, 320, 155, TFT_DARKGREY);

  sprite.setTextColor(TFT_DARKGREY);
  sprite.setTextSize(1);
  sprite.setCursor(10, 171);
  sprite.print("14/0=browse  both=back to list");

  sprite.pushSprite(0, 0);
}

// =====================
// --- SETUP ---
// =====================

void setup() {
  USBSerial.begin(115200);
  delay(2000);
  USBSerial.println("PiAware Display starting...");

  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);
  pinMode(BUTTON_NEXT, INPUT_PULLUP);
  pinMode(BUTTON_PREV, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  sprite.createSprite(320, 180);

  displayMessage("PiAware Display", "Connecting...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    displayMessage("WiFi failed!", "Check config.h");
    return;
  }

  displayMessage("Connected!", WiFi.localIP().toString());
  delay(1000);

  displayMessage("Fetching", "aircraft...");
  fetchAircraft();

  // Pre-fetch routes for first 7 visible aircraft
  displayMessage("Looking up", "routes...");
  for (int i = 0; i < min(7, aircraftCount); i++) {
    if (aircraft[i].flight != "") {
      getRoute(aircraft[i].flight);
    }
  }

  drawList();
  lastUpdate = millis();
}

// =====================
// --- LOOP ---
// =====================

void loop() {
  bool btnNext = digitalRead(BUTTON_NEXT);
  bool btnPrev = digitalRead(BUTTON_PREV);

  // Both buttons held — toggle list/detail view
  if (btnNext == LOW && btnPrev == LOW) {
    delay(100);
    if (digitalRead(BUTTON_NEXT) == LOW &&
        digitalRead(BUTTON_PREV) == LOW) {
      detailView = !detailView;
      if (detailView) drawDetail();
      else            drawList();
      while (digitalRead(BUTTON_NEXT) == LOW ||
             digitalRead(BUTTON_PREV) == LOW) delay(10);
      lastButtonNextState = HIGH;
      lastButtonPrevState = HIGH;
      return;
    }
  }

  // Next button
  if (btnNext == LOW && lastButtonNextState == HIGH) {
    delay(50);
    if (digitalRead(BUTTON_NEXT) == LOW) {
      selectedIndex = (selectedIndex + 1) % max(1, aircraftCount);
      if (detailView && aircraft[selectedIndex].flight != "")
        getRoute(aircraft[selectedIndex].flight);
      if (detailView) drawDetail();
      else            drawList();
    }
  }
  lastButtonNextState = btnNext;

  // Prev button
  if (btnPrev == LOW && lastButtonPrevState == HIGH) {
    delay(50);
    if (digitalRead(BUTTON_PREV) == LOW) {
      selectedIndex = (selectedIndex - 1 + max(1, aircraftCount)) %
                      max(1, aircraftCount);
      if (detailView && aircraft[selectedIndex].flight != "")
        getRoute(aircraft[selectedIndex].flight);
      if (detailView) drawDetail();
      else            drawList();
    }
  }
  lastButtonPrevState = btnPrev;

  // Auto refresh every REFRESH_MS
  if (millis() - lastUpdate >= REFRESH_MS) {
    fetchAircraft();

    // Look up one uncached route per refresh cycle
    for (int i = 0; i < min(7, aircraftCount); i++) {
      if (aircraft[i].flight != "" &&
          findInCache(aircraft[i].flight) == nullptr) {
        getRoute(aircraft[i].flight);
        break;
      }
    }

    if (detailView) drawDetail();
    else            drawList();

    lastUpdate = millis();
  }
}
