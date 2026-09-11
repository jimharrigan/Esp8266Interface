# Esp8266Interface

A WiFi bridge between a dry-contact (or opto-isolated) signal and MQTT / HTTP, built
around an ESP-01 module. It reports the state of an external input to an MQTT topic
and/or a pair of webhook URLs, and drives a relay output in response to MQTT messages.

Typical use is retrofitting network connectivity onto existing wired equipment —
a doorbell transformer, an alarm panel output, a gate contact — without modifying it.

The repository contains both halves of the project:

| Path | Contents |
| --- | --- |
| `Esp8266Interface.ino` | The Arduino firmware (single sketch) |
| `Esp8266Interface/` | KiCad schematic and PCB for the carrier board |
| `Esp8266Interface/production/` | Fabrication output: gerbers, BOM, positions, IPC netlist |
| `Esp8266Interface.sln`, `*.vcxproj` | Visual Studio + [Visual Micro](https://www.visualmicro.com/) project |
| `Esp8266Interface/test/` | Scratch KiCad project, not part of the build |

## How it works

**Input → network.** An external signal pulls `GPIO3` low, either through the on-board
PC817 optocoupler (from the `HV In` header) or by a plain contact closure on the `In3`
header. A pin-change interrupt records the new level; the main loop notices the change
and:

- publishes `Trigger` (pin low) or `Reset` (pin high) to the configured MQTT topic, and
- issues an HTTP `GET` to the configured trigger URL or reset URL.

**Network → output.** The firmware subscribes to a control topic. A payload of `1` or
`trigger` drives `GPIO1` high; anything else drives it low. `GPIO1` gates a 2N7000
low-side switch on the `Rly` header, so this is intended to energise a relay coil. The
new state is echoed back to the status topic as `Output 1` / `Output 0`.

## Hardware

### Connectors

Silkscreen labels come from the BOM; the descriptions below are derived from the netlist.

| Ref | Label | Function |
| --- | --- | --- |
| J1 | `ESP01` | 2x4 socket for the ESP-01 module |
| J4 | `Pin` | **Power in.** Pin 1 GND, pin 2 raw DC into the LM1084-3.3 regulator |
| J5 | `Pout` | **Power out.** Pin 1 raw input rail, pin 2 regulated +3.3 V |
| J7 | `HV In` | **Isolated sense input.** Pin 1 is the common return; pin 2 feeds the optocoupler through R2 (1 k), pin 3 through R3 (68 k) + R2 for a higher-voltage source. D2 (1N4004) rectifies, so an AC source works; D1 (zener) and C5 clamp and smooth what reaches the PC817 LED via R1 (2.2 k) |
| J2 | `In3` | **Non-isolated input.** Shorting the two pins pulls `GPIO3` to GND, in parallel with the optocoupler |
| J3 | `In` | **Config-portal button.** Shorts `GPIO0` to `GPIO2`, which the firmware holds low after boot |
| J6 | `Rly` | **Relay output.** Pin 1 is the raw input rail, pin 2 the 2N7000 drain; D3 (1N4001) is the flyback diode across the coil |
| SW1 | — | Momentary **reset** for the ESP-01 (`RST` to GND) |

The zener D1 has no value in the BOM, so the usable voltage range of each `HV In` tap
depends on the part actually fitted. Work out the range for your source before relying
on it, and treat anything on `HV In` as hazardous unless you know otherwise — nothing on
the board isolates `HV In` from its own source, only from the ESP side.

### ESP-01 pin usage

| Pin | Direction | Use |
| --- | --- | --- |
| `GPIO0` | input, 3.3 k pull-up (R6) | Config-portal button, read in `loop()`; also the ESP-01 flash-mode strap |
| `GPIO1` | output | Relay drive, through R4 (47 k) into the 2N7000 gate. Also the UART TX pin |
| `GPIO2` | output, driven low | Return path for the `GPIO0` button, so the button cannot hold the board in flash mode at boot. 3.3 k pull-up (R5) |
| `GPIO3` | input, internal pull-up | Sense input, from the optocoupler or `In3`. Also the UART RX pin |

Two pins do double duty as the serial port, which shapes the firmware:

- `Serial.begin()` is called with `SERIAL_TX_ONLY` so `GPIO3` stays usable as an input.
- `GPIO1` cannot be the UART TX pin and the relay drive at the same time. The
  `DEBUG_SERIAL` switch at the top of the sketch picks one:

  | `DEBUG_SERIAL` | Behaviour |
  | --- | --- |
  | `0` (default) | Boot messages print, then `setup()` calls `Serial.end()` and claims `GPIO1` as an output. Logging stops; the relay works |
  | `1` | Serial logging stays on for the whole run; the relay output does nothing |

  Use `DBG_PRINT()` / `DBG_PRINTLN()` for any log line added after boot — plain
  `Serial` calls past the end of `setup()` go to a closed port in the default build.
- `GPIO1` toggles as TX during boot. C1 (33 µF) on the MOSFET gate with R4 gives roughly
  a 1.5 s time constant, which stops the relay chattering while the module starts up —
  at the cost of the output switching slowly.

## Building and flashing

The sketch targets the Arduino core for ESP8266 and needs these libraries:

- [WiFiManager](https://github.com/tzapu/WiFiManager) (tzapu)
- [ArduinoJson](https://arduinojson.org/) v6 — the sketch uses `DynamicJsonDocument`
- [PubSubClient](https://github.com/knolleary/pubsubclient)
- `LittleFS`, `ESP8266HTTPClient`, `WiFiClientSecure` — all part of the ESP8266 core

Build with the Arduino IDE, `arduino-cli`, or the supplied Visual Studio / Visual Micro
solution. Choose a flash layout that includes a LittleFS partition — the configuration
is stored in `/config.json` and there is nowhere to save settings without it.

The board has no USB interface and no auto-reset circuit. To flash, use an external
3.3 V USB-to-serial adapter on the ESP-01's `TX`/`RX`/`GND`, hold `GPIO0` low, and pulse
reset. Doing this with the module out of the socket avoids back-feeding the `Rly` and
`HV In` circuitry through `GPIO1` and `GPIO3`.

## Configuration

At boot the board tries the WiFi credentials WiFiManager has stored, giving up after
20 s. Failing to connect is not fatal — it keeps running and keeps retrying MQTT — and
it deliberately does **not** open the config portal on its own, so a router outage
cannot leave an access point running unattended in someone's wall.

The portal is opened on demand instead. Press the button on `J3` and the board comes up
as an access point named **`ESP`** with the password **`1234567890`**, which is also how
you configure it on first boot, when nothing is stored yet. The portal offers the
standard WiFi and info pages plus a `param` page with these fields:

| Field | Stored as | Notes |
| --- | --- | --- |
| mqtt server | `mqtt_server` | Leave empty to disable MQTT entirely |
| mqtt port | `mqtt_port` | Defaults to 8883 |
| mqtt username | `mqtt_username` | |
| mqtt password | `mqtt_password` | |
| mqtt topic | `mqtt_topic` | Status topic the board publishes to |
| mqtt control topic | `mqtt_control_topic` | Subscribed topic for output commands; leave empty to skip |
| Trigger Url | `trigger_url` | Fetched when the input goes low |
| Reset Url | `reset_url` | Fetched when the input goes high |

Settings are written to `/config.json` in LittleFS and reloaded at boot. The portal
timeout is 300 s and runs non-blocking, so the rest of the loop keeps running while it
is open. Saving MQTT settings takes effect immediately: the client is re-pointed at the
new broker and any live session is dropped, so the next reconnect picks up the new
server, port, credentials and control topic without a restart.

### Messages

| Direction | Topic | Payload |
| --- | --- | --- |
| Published | `mqtt_topic` | `Trigger` when the input goes low, `Reset` when it goes high |
| Published | `mqtt_topic` | `Output 1` / `Output 0` after a control message |
| Subscribed | `mqtt_control_topic` | `1` or `trigger` turns the output on; any other payload turns it off |

Payload matching is case-insensitive. An HTTP request counts as successful only if the
response body contains `OK`, though the result is currently discarded by the caller.

MQTT runs over `WiFiClientSecure`, but `setInsecure()` is called, so the server
certificate is not validated. Credentials are stored in plaintext in the filesystem.
Treat this as suitable for a trusted LAN, not the open internet.

## Notes

The sketch compiles clean — no warnings — against ESP8266 core 3.1.2 with
`--warnings all`, in both `DEBUG_SERIAL` configurations.

A few behaviours are deliberate rather than oversights:

- The config portal opens on the button's falling edge, not while it is held, and not
  on top of a portal that is already running. It never opens by itself on a failed
  connection.
- MQTT connection attempts are spaced `MQTT_RECONNECT_INTERVAL` (5 s) apart. Saving
  settings in the portal resets that, so a new broker is tried on the next pass.
- `publishMessage()` honours its `retained` argument; all three call sites pass `true`,
  so every status message is retained.

## License

GPL-3.0 — see [LICENSE](LICENSE).
