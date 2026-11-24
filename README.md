# Temperature Orbs

An ESP32 starter project for five 240x240 GC9A01 displays, LVGL 8 and
Blynk. Orb 1 displays the hub sensor; Orbs 2-5 display Nodes 1-4.
Temperatures arrive in Celsius and are displayed in Fahrenheit. The segmented
scale fills clockwise from 30 to 110 F and clamps at either end; the numeric
reading still shows the actual temperature outside that range. Readings start
as placeholders. Orb 1's status line shows this device's Wi-Fi/Blynk
connection state; `Online` means the Orbs are connected to Blynk. The most
recent readings remain visible when disconnected and may be stale.

| Screen | Source | Temperature (C, double) | Humidity (%, double) | Elapsed since report (string) | Battery (%) |
| --- | --- | --- | --- | --- | --- |
| Orb 1 | Hub | V8 | V9 | V14 | - |
| Orb 2 | Node 1 | V0 | V4 | V10 | V30 |
| Orb 3 | Node 2 | V1 | V5 | V11 | V31 |
| Orb 4 | Node 3 | V2 | V6 | V12 | V32 |
| Orb 5 | Node 4 | V3 | V7 | V13 | V33 |

V10-V14 contain elapsed time in hours and minutes since the last report,
updated by the hub every minute and reset to `00:00` on a report. Node
datastreams show `--:--` before first contact. The hub changes V10-V13 to
`OFFLINE HH:MM` after 90 minutes without a node report (three missed 30-minute
wake cycles), and sends one `node_offline` event per offline period. Elapsed
time is capped at `99:59`. The Orbs show `Online` for node `HH:MM` values
and `Offline` for `OFFLINE HH:MM`, displaying the elapsed time separately
as `Updated HH:MM`.
The hub determines the timeout; the Orbs do not calculate it. A missing,
`--:--`, or unrecognized value shows `Unknown`.
Node statuses also become `Unknown` if the Orbs lose Blynk connectivity, and
are restored as contact values are synchronized on reconnect. While connected,
statuses reflect the most recently received values and can be stale if the hub
stops updating them. `Online` is green, `Offline` orange, and `Unknown` red;
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
pre-created device token.

1. In Blynk.Console, create an ESP32/Wi-Fi template and add the datastreams
   listed above with their indicated types. Battery percentages use integer
   datastreams.
2. Copy `firmware/config/secrets.example.h` to `firmware/config/secrets.h` and
   set `BLYNK_TEMPLATE_ID` and `BLYNK_TEMPLATE_NAME` there. The local file
   takes precedence and is ignored by Git. If upgrading from the earlier
   manual setup, remove `BLYNK_AUTH_TOKEN` from `secrets.h`; the app supplies
   it automatically.
   Do not put a Wi-Fi password or device token in the example file.
3. Build and upload with PlatformIO:

   ```text
   pio run -e TempOrbs
   pio run -e TempOrbs -t upload
   pio device monitor -b 115200
   ```

4. On first startup, Orb 1 shows `Add in Blynk app`; the node statuses show
   `Unknown` until their contact values are received. Enable Bluetooth
   and any permissions requested by the Blynk app on your phone. Sign into the
   account that owns the template, choose **Add New Device → Find Devices
   Nearby**, and follow the prompts to connect it to your 2.4 GHz Wi-Fi.
   The device name and connection progress are printed to the serial monitor.
5. Configure Blynk widgets, automations or a data source to update the Orbs
   device's datastreams. Datastream values belong to individual Blynk devices:
   if the readings originate on another device, they must be forwarded to the
   Orbs device. The firmware requests its last saved values on connection; a
   datastream without a saved value remains at its placeholder.

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

The pin defaults and SPI clock are in `firmware/config/config.system.h`.
LVGL settings are in `platformio.ini`. Display initialization, the LVGL UI,
and Blynk handlers are in `firmware/src/main.cpp`.
