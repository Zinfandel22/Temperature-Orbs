# Temperature Orbs

This project is the remote display for the companion Temperature Nodes
system. Battery-powered nodes report to a Wi-Fi-connected Hub over ESP-NOW;
the Hub publishes to Blynk; the Orbs fetch the Hub's saved Blynk values over
the internet. See [System Architecture](SYSTEM_ARCHITECTURE.md) for the
complete system, data flows, datastreams, and status limitations.

An ESP32 starter project for five 240x240 GC9A01 displays, LVGL 8 and
Blynk. Orb 1 displays the hub sensor; Orbs 2-5 display Nodes 1-4.
Temperatures arrive in Fahrenheit and are displayed without conversion. The segmented
scale fills clockwise from 30 to 110 F and clamps at either end; the numeric
reading still shows the actual temperature outside that range. Readings start
as placeholders. Orb 1's status line shows this device's Wi-Fi/Blynk
connection state; `Online` means the Orbs are connected to Blynk. The most
recent readings remain visible when disconnected and may be stale.
Each received temperature value is logged to the serial monitor with its
virtual pin. The Orbs read the Hub device's saved datastream values over
HTTPS, starting when Wi-Fi and internet time are available and repeating
10 seconds after each request.

The Orb writes its Hub polling status to the Orb device's V99 string
datastream: `Online` after a successful request containing usable data, and
`Offline` after a failed request or while the Hub token is missing or being
changed. It writes on changes and after a Blynk reconnect. This is not the
Hub's actual Blynk connection state: saved data may be returned while the
Hub is offline. If the Orb disconnects from Blynk, the dashboard can show
the last published status until the Orb reconnects.

Orb 1 shows the local receipt time of the latest successful Hub data request
in Los Angeles time (PST/PDT). It is not necessarily the time the source
originally reported. Failed requests leave the last readings visible and show
`Hub data stale`; missing or invalid values show `Hub data incomplete`.
The time remains `--:--` until data is received and internet time is
synchronized. V14 is not displayed on Orb 1.

| Screen | Source | Temperature (F, double) | Humidity (%, double) | Node contact (string) | Battery (%) |
| --- | --- | --- | --- | --- | --- |
| Orb 1 | Hub | V8 | V9 | - | - |
| Orb 2 | Node 1 | V0 | V4 | V10 | V30 |
| Orb 3 | Node 2 | V1 | V5 | V11 | V31 |
| Orb 4 | Node 3 | V2 | V6 | V12 | V32 |
| Orb 5 | Node 4 | V3 | V7 | V13 | V33 |

V10-V13 provide node status and contact time. For example, `ONLINE 10:06 PM`
shows `Online` and `Updated 10:06 PM`; `OFFLINE 10:06 PM` shows `Offline`
with the same time. The Orbs also accept the legacy elapsed-time formats
`HH:MM` (online) and `OFFLINE HH:MM`, and display the supplied time as
`Updated HH:MM`. The Hub determines node status; the Orbs do not calculate it.
A missing,
`--:--`, or unrecognized value shows `Unknown`. Unrecognized contact values
are logged to the serial monitor.
The current Hub firmware sends unprefixed 12-hour times such as `10:06 PM`
for real online nodes; the Orb does not currently recognize that format and
shows `Unknown` for those values. Demo mode's `ONLINE --:--` is also
unrecognized until the Hub clock is synchronized.
Node statuses become `Unknown` if a Hub request fails and are restored by
subsequent contact values. A successful request can still return old values if
the Hub stops updating Blynk; the HTTPS response contains no source timestamps.
`Online` is green, `Offline` orange, and `Unknown` red;
other connection messages are white. Node screens show battery percentage in
red below 10%, and white at 10% or above. Battery voltage is not displayed;
V20-V23 are no longer read or synchronized by the Orbs.

## Hardware

The five displays share SPI data, clock, DC and reset, but have separate
chip-select lines. All displays must use the same controller and orientation.

| Display connection | ESP32 GPIO |
| --- | --- |
| SDA / MOSI | 17 |
| SCLK | 23 |
| DC | 19 |
| RST | 18 |
| Display 1 CS | 13 |
| Display 2 CS | 33 |
| Display 3 CS | 32 |
| Display 4 CS | 25 |
| Display 5 CS | 21 |

Connect grounds together and supply each display according to its module's
specifications; do not assume every GC9A01 module accepts 5 V. Connect
backlights as required by the module. The GPIO signals are 3.3 V.

The existing button definitions use GPIO 26, 27 and 14, but this starter does
not yet use those buttons. **Do not connect ESP32 GPIO inputs directly to 5 V.**
If using the configured internal pulldowns, wire buttons to 3.3 V instead.
The existing diagram illustrates the signal connections but uses ILI9341
displays and shows 5 V buttons; it is not a GC9A01 simulation or a safe
button-voltage reference.

## Blynk setup

This uses Blynk 1.4.0 and the separate Blynk.Edgent library with Blynk's
ESP32 example interaction files. Provisioning uses Bluetooth, rather than
the Wi-Fi hotspot used by the earlier Edgent example. It does not use a
pre-created token for the Orbs device; the separate Hub token is configured
in the Orb device's V101 datastream for HTTPS polling.

1. In Blynk.Console, create an ESP32/Wi-Fi template and add the datastreams
   listed above with their indicated types. Battery percentages use integer
   datastreams.
2. Copy `firmware/config/secrets.example.h` to `firmware/config/secrets.h` and
   set `BLYNK_TEMPLATE_ID` and `BLYNK_TEMPLATE_NAME` there. The local file
   takes precedence. If upgrading from the earlier
   manual setup, remove `BLYNK_AUTH_TOKEN` from `secrets.h`; the app supplies
   it automatically.
   Do not put a Wi-Fi password or device token in the example file. Check your
   local Git ignore rules before adding any private information to `secrets.h`.
3. Hub polling defaults to `blynk.cloud`. If a request returns HTTP 308,
   set `HUB_BLYNK_SERVER` in the PlatformIO build flags to the Hub's regional
   hostname (shown at the bottom right of Blynk.Console), for example
   `-D HUB_BLYNK_SERVER=\"ny3.blynk.cloud\"`. The API requires HTTPS;
   verify that your Blynk plan permits the request rate.
4. Build and upload with PlatformIO:

   ```text
   pio run -e TempOrbs
   pio run -e TempOrbs -t upload
   pio device monitor -b 115200
   ```

5. On first startup, Orb 1 shows `Add in Blynk app`; the node statuses show
   `Unknown` until their contact values are received. Enable Bluetooth
   and any permissions requested by the Blynk app on your phone. Sign into the
   account that owns the template, choose **Add New Device → Find Devices
   Nearby**, and follow the prompts to connect it to your 2.4 GHz Wi-Fi.
   The device name and connection progress are printed to the serial monitor.
6. Add a **String** datastream on V101 to the Orb device's template and a
   dashboard input for it. In the **Orb device's** dashboard, enter the
   32-character auth token of the Hub device (not the Orb device). V101 must
   accept the full token. The Orb syncs V101 on each Blynk connection and
   also accepts changes while connected; it does not write V101. Orb 1
   shows `Set Hub token` until a valid value arrives. The token is stored
   in Blynk and transmitted to the Orb over its Blynk connection; anyone with
   access to this datastream can read it. Restrict dashboard access, never
   share the token, and rotate it if exposed. The Orb keeps the token in
   memory, not in its own application storage or firmware configuration.
7. Add a **String** datastream on V99 to the Orb device's template and a
   dashboard display widget to show the Hub polling status.
8. The Hub must update its own datastreams listed above. The Orbs read those
   Hub values directly; no cross-device forwarding automation is required.
   An absent or invalid Hub datastream leaves its last reading in place.

Provisioning stores Wi-Fi credentials and the device token in ESP32 NVS, so
they persist across restarts. To provision again, hold the ESP32's **BOOT**
button (GPIO 0, active low) for more than 10 but less than 20 seconds, then
release it. This erases the stored Blynk configuration; it does not delete
the device from Blynk. If provisioning times out after 10 minutes, restart the
device to try again. Edgent attempts to migrate valid credentials from the
previous Edgent version; if it does not reconnect, reset and provision again.
The stock example's GPIO 2 status LED is disabled because this project
reserves that pin as `BUSY_PIN`; connection status is shown on the displays.

The cloud connection uses TLS. Although Edgent includes OTA support, the
current partition table has only one app slot and does not support OTA
updates. If the template ID is missing or still a placeholder, Orb 1 shows
`Set template ID` and provisioning does not start. Edgent may print
credentials during migration, so do not share unredacted serial logs.
Hub polling uses a separate certificate-verified TLS 1.2 connection with a
shared send/receive buffer to reduce memory usage alongside Blynk's connection.
If the Hub HTTPS connection fails, serial output includes free internal heap
and largest free block sizes before and after the connection attempt.

The pin defaults and SPI clock are in `firmware/config/config.system.h`.
LVGL settings are in `platformio.ini`. Display initialization, the LVGL UI,
and Blynk handlers are in `firmware/src/main.cpp`.
