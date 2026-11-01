<h1 align="center">Logix Outlet Firmware</h1>

<p align="center">
  Replacement firmware for the Gosund WO1 in-wall smart duplex receptacle (ESP8285, 1 MB, BL0937 energy meter).<br>
  Two independently switched outlets with energy monitoring, controlled from its own web page, an HTTP API or MQTT.<br>
  Installs over the Tasmota web OTA form, so the receptacle never leaves the wall.
</p>

## Features

- **Works on its own:** the two buttons toggle the two outlets at all times. That includes with no WiFi, no MQTT, and while the network code is busy, because the buttons are scanned from a timer and not from the main loop.
- **Survives power cuts:** each outlet's state is saved to flash the moment it changes and restored first thing at power-up, before WiFi starts.
- **Energy monitoring like Tasmota:** voltage, current, active, apparent and reactive power, power factor, plus energy Today, Yesterday and Total. Calibrate it from the web page against a known load or meter.
- **Names:** one standard device name (default `Logix Outlet XXXX`). The outlets are always `<name> Top` and `<name> Bottom`. The hostname follows the name (e.g. `family-room-outlet.local`).
- **Settings page:** device name and admin password, WiFi (with scan), MQTT broker, energy calibration, firmware update.
- **Setup access point built in:** when there is no saved network, or the network is gone for a minute, an open AP `Logix-Outlet-XXXX` comes up at `192.168.4.1` with a captive portal. The saved network keeps being retried, and the AP goes away once it is back.
- **Comes back from Tasmota with its settings:** on the first boot it imports Tasmota's WiFi network, MQTT broker, energy total and calibration, then erases Tasmota's settings sector.
- **LogixDIY ready:** mDNS `_logix._tcp`, a public `GET /api/info` and retained MQTT discovery on `logix/discovery/<id>`, the same contract as the other Logix devices.

See [API.md](API.md) for the HTTP and MQTT API and [DEVICE.md](DEVICE.md) for the hardware survey.

## Hardware

| GPIO | Function |
|---|---|
| 15 | Relay, top outlet |
| 14 | Relay, bottom outlet |
| 0 | LED, top outlet (active low) |
| 13 | LED, bottom outlet (active low) |
| 2 | WiFi LED (active low, inside the box) |
| 5 | BL0937 CF (power) |
| 4 | BL0937 CF1 (voltage / current) |
| 12 | BL0937 SEL (low = voltage) |
| A0 | Both buttons, resistor ladder: idle ≈ 1024, top ≈ 754, bottom ≈ 314 |

The pin map is hard-coded in [src/config.h](src/config.h).

## Building

[PlatformIO](https://platformio.org/):

```sh
pio run
```

This produces `.pio/build/logixoutlet/firmware.bin` and `firmware.bin.gz`.

## Installing over Tasmota

Tasmota only has about 384 KB free for an upload, so **use `firmware.bin.gz`** (about 310 KB). The full `.bin` is too big for this step.

1. Open the Tasmota web page → **Firmware Upgrade** → **Upgrade by file upload**.
2. Choose `firmware.bin.gz` and click **Start upgrade**.
3. The device restarts into Logix Outlet. On this first boot it imports the WiFi network and MQTT broker from Tasmota and rejoins the network. Look for it at the same IP address (if DHCP hands it out again) or at `logix-outlet-xxxx.local`.
4. If it can't join the network, connect to the WiFi network `Logix-Outlet-XXXX` and set it up at `http://192.168.4.1`.

From the command line:

```sh
curl -s http://<tasmota-ip>/up > /dev/null
curl -F "u2=@.pio/build/logixoutlet/firmware.bin.gz" http://<tasmota-ip>/u2
```

The outlets start **off** after the switch from Tasmota. From then on they keep their state through power cuts and updates.

## Updating

Settings → **Firmware** accepts `firmware.bin` or `firmware.bin.gz`. Or:

```sh
curl -F "firmware=@.pio/build/logixoutlet/firmware.bin.gz" http://<device>/update
```

### Going back to Tasmota

The full Tasmota image is larger than the free update space. Upload `tasmota-minimal.bin.gz` first, then the full Tasmota from its own upgrade page.

## Energy

- One BL0937 meter measures **both outlets together**.
- Readings use Tasmota's BL0937 formulas and calibration values, so a calibration from Tasmota carries over.
- **Today** rolls over to **Yesterday** at local midnight (US Eastern, from NTP). Until the clock is set after a boot, energy is still counted.
- The totals are kept in RTC memory every 10 s, which survives restarts and updates, and in flash every 15 minutes and at midnight, which survives power cuts. A power cut loses at most the last 15 minutes of counting.

To calibrate, switch an outlet on with a load, then enter the true voltage, power and/or current on the Settings page. The load could be a resistive heater, or an incandescent bulb with a reference plug-in meter.

## License

[MIT](LICENSE)
