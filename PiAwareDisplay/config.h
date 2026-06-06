// ============================================================
//  PiAware Display — config.h
//  Fill in ALL settings before uploading.
//  See README.md for instructions on obtaining each value.
// ============================================================

#pragma once

// ============================================================
//  WIFI
// ============================================================
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"

// ============================================================
//  HARDWARE BUTTONS
//  T-Display S3 has buttons on GPIO 14 and GPIO 0.
//  GPIO 14 = next / scroll forward
//  GPIO 0  = previous / scroll back
//  Both together = toggle list/detail view
// ============================================================
#define BUTTON_NEXT  14
#define BUTTON_PREV   0

// ============================================================
//  PIAWARE / DUMP1090
//  The URL of your local PiAware receiver's aircraft JSON feed.
//  Common paths — try these in your browser until one works:
//    http://YOUR_IP:8080/data/aircraft.json       ← most common
//    http://YOUR_IP/dump1090-fa/data/aircraft.json
//    http://YOUR_IP/skyaware/data/aircraft.json
//    http://YOUR_IP/tar1090/data/aircraft.json
//  Replace YOUR_IP with your PiAware box's local IP address.
// ============================================================
#define PIAWARE_URL  "http://192.168.4.85:8080/data/aircraft.json"

// ============================================================
//  HOME COORDINATES
//  Used to calculate distance from each aircraft to your location.
//  Find your coordinates at https://www.latlong.net
// ============================================================
#define HOME_LAT  52.9717f
#define HOME_LON  -1.1599f

// ============================================================
//  FLIGHTAWARE AEROAPI
//  Used to look up flight routes (origin → destination).
//
//  As a PiAware feeder you get $10 free credit per month.
//  Sign up at: https://www.flightaware.com/aeroapi/portal
//  Note: AeroAPI is a separate account from your feeder account.
//
//  The API key is labelled "API Key" in the portal.
//  Route lookups are cached on-device so API calls are minimised.
//  At one lookup per 5-second refresh cycle, $10/month is
//  more than sufficient for a home display.
// ============================================================
#define AEROAPI_KEY  "YOUR_AEROAPI_KEY"

// ============================================================
//  REFRESH INTERVAL
//  How often to poll the PiAware receiver for new aircraft data.
//  5000ms (5 seconds) is a good balance — dump1090 updates
//  its JSON once per second but there's no need to hammer it.
// ============================================================
#define REFRESH_MS  5000
