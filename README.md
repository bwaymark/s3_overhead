# PiAware ADS-B Display for LilyGO T-Display S3

A live aircraft display for the [LilyGO T-Display S3](https://lilygo.cc/products/t-display-s3) (ESP32-S3 with built-in 1.9" LCD). Polls a local [PiAware](https://flightaware.com/adsb/piaware/) ADS-B receiver for nearby aircraft and enriches each flight with origin, destination, and airline data from the [FlightAware AeroAPI](https://www.flightaware.com/commercial/aeroapi/).

---

## What it does

- Lists up to 20 aircraft sorted by distance from your location
- Shows callsign, altitude, distance (km), origin airport, destination airport, and airline
- Caches up to 40 route lookups in RAM to avoid hammering the AeroAPI
- Detail view (both buttons) shows a larger per-aircraft readout with position and cache status
- Refreshes every 5 seconds; fetches one uncached route per cycle

---

## Hardware required

| Item | Notes |
|------|-------|
| LilyGO T-Display S3 | The 170×320 px variant. Not the original T-Display. |
| Raspberry Pi running PiAware | Any Pi works; needs to be on the same local network |
| ADS-B antenna + USB dongle | RTL-SDR or similar, connected to the Pi |

---

## Prerequisites

### Arduino IDE

1. Install [Arduino IDE 2.x](https://www.arduino.cc/en/software)
2. Add the ESP32 board package. In **File → Preferences → Additional boards manager URLs**, add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Open **Tools → Board → Boards Manager**, search for `esp32`, install the Espressif package (2.x or later)
4. Select board: **Tools → Board → ESP32S3 Dev Module**

### Libraries

Install all of these via **Sketch → Include Library → Manage Libraries**:

| Library | Author | Notes |
|---------|--------|-------|
| TFT_eSPI | Bodmer | Display driver |
| ArduinoJson | Benoit Blanchon | v6 or v7 |

**TFT_eSPI requires a User_Setup file.** After installing, find the library folder (usually `~/Documents/Arduino/libraries/TFT_eSPI/`) and copy in the T-Display S3 setup file:

```
cp docs/User_Setup.h ~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h
```

A suitable `User_Setup.h` for the T-Display S3 is included in the `docs/` folder of this repo. If you already have a working TFT_eSPI setup for this board, skip this step.

### PiAware

Follow the [FlightAware PiAware installation guide](https://flightaware.com/adsb/piaware/install). Once running, the JSON feed is available at:

```
http://<your-pi-ip>:8080/data/aircraft.json
```

Check this URL in a browser before proceeding — you should see a JSON blob with an `aircraft` array.

### AeroAPI key

Sign up at [flightaware.com/commercial/aeroapi](https://www.flightaware.com/commercial/aeroapi/). The free tier provides a limited monthly allowance which is sufficient for casual use. Copy your API key from the dashboard.

---

## Configuration

Open `src/piaware_lilygo.ino` and edit the block at the top of the file:

```cpp
const char* WIFI_SSID      = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD  = "YOUR_WIFI_PASSWORD";

const char* PIAWARE_URL    = "http://192.168.X.X:8080/data/aircraft.json";

const char* AEROAPI_KEY    = "YOUR_AEROAPI_KEY";

const float HOME_LAT = 0.0000f;  // your latitude, decimal degrees
const float HOME_LON = 0.0000f;  // your longitude, decimal degrees
```

To find your coordinates, right-click your location in Google Maps and copy the decimal values shown at the top of the context menu. Negative longitude = west of Greenwich.

> **Do not commit the file after adding your credentials.** The `.gitignore` won't protect a modified `.ino` from being staged. If you fork this repo, consider moving credentials to a separate `secrets.h` file that is listed in `.gitignore`.

---

## Building and flashing

1. Open `src/piaware_lilygo.ino` in Arduino IDE
2. Connect the T-Display S3 via USB-C
3. Select the correct port under **Tools → Port**
4. Set **Tools → USB CDC On Boot → Enabled** (needed for `USBSerial` debug output)
5. Click **Upload**

If the board doesn't appear in the port list, hold the BOOT button while plugging in.

---

## Button controls

| Action | Result |
|--------|--------|
| Button 14 (right) | Next aircraft |
| Button 0 (left) | Previous aircraft |
| Both buttons together | Toggle list / detail view |

---

## Display layout

**List view** (up to 7 rows):

```
ADS-B       12 shown / 47 tracked
──────────────────────────────────────
KM   FLIGHT   ALT      ORIGIN  DEST  AIRLINE
 4.2  EZY123  32000ft  LTN     BCN   easyJet
12    BAW456  28500ft  LHR     JFK   British ...
...
```

**Detail view** (single aircraft):

```
EZY123                        ICAO: 3c6444
──────────────────────────────────────────
LTN  BCN
->
easyJet
──────────────────────────────────────────
ALTITUDE    DISTANCE    LAST SEEN
32000ft     4.2km       2s
──────────────────────────────────────────
Pos: 52.9841, -1.1732
Route: cached
```

---

## AeroAPI usage and costs

Each cache miss triggers one AeroAPI call. The sketch fetches at most one uncached route per 5-second refresh cycle, so under normal conditions usage stays low. The route cache holds 40 entries in RAM and persists until the device resets.

The [AeroAPI Personal tier](https://www.flightaware.com/commercial/aeroapi/pricing) (free with limits) is generally sufficient for home use. Monitor your usage in the FlightAware dashboard if you're concerned.

---

## Troubleshooting

| Symptom | Likely cause |
|---------|-------------|
| Screen stays blank | TFT_eSPI `User_Setup.h` not configured for T-Display S3 |
| "WiFi failed!" on screen | Wrong SSID/password, or 5 GHz network (ESP32 is 2.4 GHz only) |
| No aircraft shown | Wrong PiAware IP; check the URL in a browser first |
| Routes all show `...` | AeroAPI key missing or invalid; check Serial Monitor output |
| Upload fails | Try holding BOOT button on plug-in; check USB CDC setting |

Serial debug output is available at 115200 baud via the Arduino IDE Serial Monitor.

---

## Licence

MIT. Do what you like with it.
