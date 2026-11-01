# Gosund WO1 — Family Room In-Wall Duplex Receptacle

Device survey taken **2026-11-01** from the running Tasmota firmware at `192.168.22.80`,
using the Tasmota HTTP command API (`/cm?cmnd=...`). Nothing on the device was changed.

## 1. Summary

| Item | Value |
|---|---|
| Product | Gosund WO1: in-wall smart duplex receptacle (2 independently switched outlets) with energy monitoring |
| Location | Family room |
| MCU | **ESP8285** (ESP8266 core with 1 MB flash built in), running at 80 MHz |
| Flash | **1 MB** (chip ID `0x144051`), flash mode DOUT (3), 40 MHz |
| Energy meter IC | **BL0937** (HLW8012-compatible pulse-output meter) |
| Outlets / relays | 2 relays: Relay1 = GPIO15, Relay2 = GPIO14 |
| Buttons | No GPIO buttons. Buttons appear to be on the **ADC (A0) as a resistor ladder** (see §4) |
| LEDs | 2 status LEDs, active-low (GPIO0, GPIO13), plus 1 link LED (GPIO2, active-low) |
| Current firmware | Tasmota **12.0.2** (build 2022-06-20), core 2.7.4.9, SDK 2.2.2-dev |

## 2. Network identity

| Item | Value |
|---|---|
| IP address | 192.168.22.80 / 255.255.255.0 (DHCP or static; config says `WifiConfig 4` = retry) |
| Gateway / DNS | 192.168.22.1 / 192.168.22.1 |
| MAC | `C4:DD:57:31:21:FB` |
| Hostname | `tasmota-3121FB-0507` (pattern `%s-%04d`) |
| Wi-Fi SSID | `ESP` (SSID2 empty), channel 1, 802.11n, BSSID `22:E8:29:14:3D:18` |
| Signal | −58 dBm (84%), Wi-Fi TX power 17.0 dBm |
| Web server | Enabled, **Admin mode, no password set** |
| HTTP API | Enabled |

## 3. GPIO map (from the active template)

Template: `{"NAME":"Gosund WO1","GPIO":[320,0,576,0,2656,2720,0,0,2624,321,225,224,0,4704],"FLAG":0,"BASE":18}`

| GPIO | Tasmota code | Function | Notes |
|---|---|---|---|
| GPIO0 | 320 | Led_i 1 | Status LED for outlet 1, active-low. Also a boot strap pin, so it must stay high at boot |
| GPIO1 | 0 | — | UART TX (free) |
| GPIO2 | 576 | LedLink_i | Wi-Fi/link LED, active-low. Boot strap pin, must stay high at boot |
| GPIO3 | 0 | — | UART RX (free) |
| GPIO4 | 2656 | HLWBL CF1 | BL0937 CF1: voltage *or* current pulse train, depending on SEL |
| GPIO5 | 2720 | BL0937 CF | BL0937 CF: active-power pulse train |
| GPIO9 | 0 | — | Free (ESP8285 only, because flash is internal) |
| GPIO10 | 0 | — | Free (ESP8285 only) |
| GPIO12 | 2624 | HLWBL SEL_i | BL0937 SEL, inverted: selects whether CF1 outputs current or voltage |
| GPIO13 | 321 | Led_i 2 | Status LED for outlet 2, active-low |
| GPIO14 | 225 | Relay 2 | Outlet 2 relay (active-high) |
| GPIO15 | 224 | Relay 1 | Outlet 1 relay (active-high). Boot strap pin, must be low at boot (fine for a relay driver) |
| GPIO16 | 0 | — | Free |
| A0 | 4704 | ADC Input | Raw analog input, `AdcParam1 [1,0,0,0]`. Idle reading is **1024** (pulled high) |

## 4. Buttons (ADC resistor ladder)

The template has no Button GPIOs. The buttons are read through A0 instead. The device
holds this rule:

```
Rule1: on analog#a0<400 do power2 toggle break
       on analog#a0<800 do power1 toggle break
endon
```

**Rule1 is OFF (disabled)**, so under Tasmota the physical buttons do nothing.

**Measured 2026-11-01:** A0 was polled through Tasmota while each button was pressed 3 times.

| State | A0 reading | Meaning |
|---|---|---|
| No button | 1024 | idle (pulled high) |
| **Top** button | **754** (steady) | toggles the top outlet (relay 1, GPIO15) |
| **Bottom** button | **314** (steady) | toggles the bottom outlet (relay 2, GPIO14) |
| Edges | 824, 846 | seen for a moment while a button goes down or comes up |

The edge values fall inside the top button's band. That is why the firmware needs a
reading to stay stable for 75 ms, and ignores everything after a press until it sees idle
again.

Confirmed on site: **relay 1 (GPIO15) powers the TOP outlet**, and its LED (GPIO0) is the
top LED. Only the two outlet LEDs are visible on the faceplate. The GPIO2 link LED is
inside the box.

## 5. Energy monitoring (BL0937)

| Parameter | Value |
|---|---|
| PowerCal | 12530 |
| VoltageCal | 1950 |
| CurrentCal | 3500 |
| Lifetime total | **32.223 kWh** since 2022-06-26 |
| Today / Yesterday | 0.000 / 0.000 kWh |
| Resolution | Volt 0 dp, Watt 0 dp, Amp 3 dp, Energy 3 dp |
| Thresholds | PowerDelta / PowerLow / PowerHigh / Voltage / Current limits all 0 (disabled); MaxPower 0 |

- Tasmota zeroes the readings while all relays are off (`SetOption21 OFF`).
- With a relay on, it read **147 V** and 0 W. Nothing was plugged in. The line is
  nominally 120 V, and the three calibration values are exactly Tasmota's defaults, so
  this device has **never been calibrated**.
- There is one meter for the whole device, so the energy figure is **combined for both
  outlets**. There is no per-outlet metering.
- The calibration values above are Tasmota's BL0937 *pulse-period* scalars. They will be
  carried over as starting constants for the new firmware.

## 6. Behaviour and configuration

| Setting | Value | Meaning |
|---|---|---|
| PowerOnState | 3 | Restore the last saved relay state after a power cycle |
| SaveState / SaveData | 1 / 1 | Relay state and settings are saved to flash |
| Interlock | OFF | The two outlets are fully independent |
| PulseTime | all 0 | No auto-off |
| LedState | 1 | LEDs follow relay power state |
| LedMask | FFFF | — |
| LedPower1 | OFF | — |
| BlinkCount | 10 | — |
| ButtonDebounce | 50 ms | — |
| SetOption65 | ON | Fast power-cycle factory reset **disabled** |
| Sleep | 50 ms (dynamic) | — |
| TelePeriod | 300 s | — |
| Timers | All 16 disabled, master OFF | — |
| Rules 2/3 | Empty | — |
| Friendly names | `Outlet5_1`, (blank) | Device name `Outlet5` |
| Boot count / saves | 68 boots / 2019 flash saves | Since 2022-06-26 |
| Last restart reason | Software/System restart | — |
| Free memory | Heap ≈ 23 KB; program 619 KB, free sketch space 384 KB | — |
| Serial | 115200 8N1, SerialLog 2 | — |

## 7. MQTT

| Item | Value |
|---|---|
| Broker | `192.168.1.130:1883` (different subnet from the device) |
| User | `esp` (password not readable) |
| Client ID | `DVES_3121FB` |
| Topic | `tasmota_3121FB` (pattern `tasmota_%06X`), group topic `tasmotas` |
| FullTopic | `%prefix%/%topic%/` → e.g. `cmnd/tasmota_3121FB/POWER1` |
| Connection | Connected (MqttCount 1) |
| Keepalive | 30 s, max packet 1200 |
| Retain flags | All off (Power, Button, Switch, Sensor, Info, State) |

## 8. Time and location

| Item | Value |
|---|---|
| NTP | `pool.ntp.org` |
| Timezone | `+01:00` with EU DST rules (last Sun Mar 02:00 → +2h, last Sun Oct 03:00 → +1h) |
| Lat / Long | 48.858360, 2.294442 (**Tasmota defaults: Eiffel Tower**) |

The time and location are Tasmota's out-of-box defaults and almost certainly wrong for
this house. The WO1 is a North American receptacle.

## 9. Flash and OTA constraints for the replacement firmware

- **1 MB total flash.** Tasmota's layout reserves the last 16 KB for SDK/RF data and
  uses about 0xFB000 for settings.
- Over-the-air updates must fit in the free half of flash, which is roughly **≤ 480 KB
  per image** for a safe single-step OTA. The alternative is a two-step OTA with a minimal
  loader, the way Tasmota does it with `tasmota-minimal`.
- The current Tasmota OTA URL points to the full `tasmota.bin.gz`. Tasmota 12 can take
  `.bin.gz` and does its own two-stage update.
- **Moving off Tasmota:** the first install of the new firmware can be done by OTA from
  Tasmota's web UI (Firmware Upgrade → upload `.bin`), provided the image is a standard
  ESP8266 Arduino/SDK binary and small enough. Otherwise it needs serial flashing, which
  means opening the receptacle (TX/RX/GND/3V3 + GPIO0 to GND).
- Keep a working rollback path. Never ship an image that cannot accept the next OTA.

## 10. Raw data

The full `Status 0` response from the survey is kept here for reference:

```json
{"Status":{"Module":0,"DeviceName":"Outlet5","FriendlyName":["Outlet5_1",""],"Topic":"tasmota_3121FB","ButtonTopic":"0","Power":0,"PowerOnState":3,"LedState":1,"LedMask":"FFFF","SaveData":1,"SaveState":1,"SwitchTopic":"0","SwitchMode":[0,0,0,0,0,0,0,0]},
 "StatusFWR":{"Version":"12.0.2(tasmota)","BuildDateTime":"2022-06-20T12:41:03","Boot":31,"Core":"2_7_4_9","SDK":"2.2.2-dev(38a443e)","CpuFrequency":80,"Hardware":"ESP8285","CR":"350/699"},
 "StatusMEM":{"ProgramSize":619,"Free":384,"Heap":23,"ProgramFlashSize":1024,"FlashSize":1024,"FlashChipId":"144051","FlashFrequency":40,"FlashMode":3},
 "StatusNET":{"Hostname":"tasmota-3121FB-0507","IPAddress":"192.168.22.80","Gateway":"192.168.22.1","Subnetmask":"255.255.255.0","DNSServer1":"192.168.22.1","Mac":"C4:DD:57:31:21:FB","Webserver":2,"HTTP_API":1,"WifiConfig":4,"WifiPower":17.0},
 "StatusMQT":{"MqttHost":"192.168.1.130","MqttPort":1883,"MqttClient":"DVES_3121FB","MqttUser":"esp","MqttCount":1,"KEEPALIVE":30},
 "StatusSNS":{"ANALOG":{"A0":1024},"ENERGY":{"TotalStartTime":"2022-06-26T11:24:20","Total":32.223,"Power":0,"Voltage":0,"Current":0.000}},
 "StatusSTS":{"POWER1":"OFF","POWER2":"OFF","Wifi":{"SSId":"ESP","Channel":1,"Mode":"11n","RSSI":84,"Signal":-58}}}
```

## 11. Tasmota settings sector (for the first-boot import)

Tasmota 12.0.2 stores its `Settings` struct in plain form at flash `0xFB000`. The config
download `/dl` is the same 4096 bytes, XOR-ed with `0x5A + offset` from byte 2 onwards.
The offsets below were confirmed against this device's own dump:

| Offset | Type | Field | This device |
|---|---|---|---|
| 0x002 | u16 | cfg_size | 4096 |
| 0x008 | u32 | version | 0x0C000200 (12.0.2) |
| 0x017 | char[699] | text pool, NUL-separated: 0 OTA URL, 1-3 prefixes, **4 SSID1**, 5 SSID2, **6 password 1**, 7 password 2, 8 hostname, 9 syslog, 10 web password, 11 CORS, **12 MQTT host**, 13 client, **14 MQTT user**, **15 MQTT password**, … | SSID `ESP`, MQTT `192.168.1.130` user `esp` |
| 0x32C | u32 | energy total, 0.01 Wh | 3222323 (32.223 kWh) |
| 0x364 / 0x368 / 0x36C | u32 | PowerCal / VoltageCal / CurrentCal | 12530 / 1950 / 3500 |
| 0x7B4 | u32 | energy TotalStartTime (epoch) | 1656242660 (2022-06-26 11:24:20 UTC) |
| 0xEFC | u16 | MQTT port | 1883 |

The SDK's RF calibration sector is at `0xFC000` (flash size − 4 sectors), so erasing
`0xFB000` after the import is safe.

## 12. Open items

- Calibration. The voltage reads ~22% high. Power and current need a known load to
  calibrate.
- Whether the device can be reached for serial flashing if an OTA ever fails. That would
  mean pulling it from the wall.
