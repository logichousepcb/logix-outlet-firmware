# Logix Outlet HTTP and MQTT API

All endpoints are on port 80 of the device (e.g. `http://192.168.22.80` or `http://<hostname>.local`).

- **Requests:** `POST` parameters are sent form-encoded (`application/x-www-form-urlencoded`) or as a query string.
- **Responses:** JSON.
- **Authentication:** none by default. Once an admin password is set, every endpoint except `GET /api/info` requires HTTP Basic auth with user `admin` (for curl: `-u admin:<password>`).

Actions return `{"ok":true,"message":"..."}` with HTTP 200. Errors return `{"ok":false,"message":"..."}` with HTTP 400. Actions that restart the device also return `"restart":true`.

**Outlets** are numbered 1 (top) and 2 (bottom). Anywhere an outlet is given, `1`, `2`, `top`, `bottom` or `all` is accepted. The device name is set by the user. The outlets are always called `<name> Top` and `<name> Bottom`.

# Identifying the device

The device follows the same Logix contract as the Logix 3 and Logix Vent, so the LogixDIY integration can discover it.

## mDNS / zeroconf

The device advertises `_logix._tcp.local.` (and `_http._tcp`) on port 80, with these TXT records:

| Key | Example | Meaning |
|---|---|---|
| `model` | `logixoutlet` | machine-readable model; stays the same across firmware versions |
| `id` | `c4dd573121fb` | lowercase MAC without separators; stable unique ID |
| `fw` | `1.0.0` | firmware version |
| `api` | `1` | API contract version; increases only on breaking changes |
| `manufacturer` | `Logix` | |

## GET /api/info

Never requires a password.

```json
{
  "manufacturer": "Logix", "model": "logixoutlet", "name": "Family Room Outlet",
  "id": "c4dd573121fb", "mac": "C4:DD:57:31:21:FB", "hostname": "family-room-outlet", "ip": "192.168.22.80",
  "fw": "1.0.0", "api": 1, "authRequired": false,
  "capabilities": {
    "outlets": 2, "buttons": 2,
    "outletKeys": ["top", "bottom"],
    "outletNames": ["Family Room Outlet Top", "Family Room Outlet Bottom"],
    "actions": ["single"],
    "energy": true, "energyMeters": 1
  },
  "mqtt": {"enabled": true, "connected": true, "topic": "logixoutlet/c4dd573121fb"}
}
```

`energyMeters: 1` means one meter measures both outlets combined.

## MQTT discovery

When MQTT is enabled, the same JSON is published, retained, to `logix/discovery/<id>`.

# Outlets

## Read everything

`GET /api/controller`

```json
{
  "uptimeMs": 81234,
  "name": "Family Room Outlet",
  "outlets": [
    {"outlet": 1, "key": "top", "name": "Family Room Outlet Top", "state": "on"},
    {"outlet": 2, "key": "bottom", "name": "Family Room Outlet Bottom", "state": "off"}
  ],
  "buttons": [
    {"button": 1, "key": "top", "pressed": false, "pressCount": 3, "lastAction": "single", "actionCount": 4, "lastActionMsAgo": 5120},
    {"button": 2, "key": "bottom", "pressed": false, "pressCount": 0, "lastAction": "none", "actionCount": 0, "lastActionMsAgo": null}
  ],
  "energy": {
    "voltage": 120.3, "current": 0.512, "power": 60.1, "apparentPower": 61.6, "reactivePower": 13.5, "factor": 0.98,
    "today": 0.123, "yesterday": 0.456, "total": 32.345, "totalStartTime": "2022-06-26T07:24:20"
  }
}
```

- `buttons[n].pressCount`: physical presses since boot.
- `buttons[n].actionCount`: physical and simulated presses since boot. When polling, a change means a new press.
- `energy`: the same quantities as Tasmota's `ENERGY` block. Volts, amps, watts, VA, var. `today`, `yesterday` and `total` are in kWh. Today rolls over at local midnight (US Eastern).

## Switch an outlet

`POST /api/outlet`

| Parameter | Value |
|---|---|
| `outlet` | `1`, `2`, `top`, `bottom` or `all` |
| `state` | `on`, `off` or `toggle` (default `toggle`) |

```sh
curl -X POST -d "outlet=top&state=on" http://192.168.22.80/api/outlet
# {"ok":true,"message":"Outlet top on."}
```

The new state is saved to flash immediately and restored after a power cut.

## Simulate a button press

`POST /api/controller/press` with `button=1|2|top|bottom`. This behaves exactly like the physical button: it toggles that outlet and counts as an action.

# Energy

## GET /api/energy

```json
{"energy": {...same as above...}, "powerCal": 12530, "voltageCal": 1592, "currentCal": 3500, "totalStartEpoch": 1656242660}
```

## POST /api/energy

Every parameter is optional.

| Parameter | Value |
|---|---|
| `total`, `today`, `yesterday` | set the counter, in kWh |
| `totalStartEpoch` | when the total started counting (Unix time) |
| `powerCal`, `voltageCal`, `currentCal` | raw calibration values (same meaning as Tasmota's PowerCal / VoltageCal / CurrentCal) |
| `reset` | `1` sets all counters to zero and starts the total now |

## POST /api/energy/calibrate

Turn an outlet on with a known load, then send what a reference meter shows. The device scales its calibration so its own reading matches. Leave out a value to skip it.

| Parameter | Value |
|---|---|
| `voltage` | actual volts |
| `power` | actual watts |
| `current` | actual amps |

```sh
curl -X POST -d "voltage=121.4&power=60" http://192.168.22.80/api/energy/calibrate
```

# Device

| Endpoint | Purpose |
|---|---|
| `GET /api/info` | identity and capabilities (no password required) |
| `GET /api/status` | firmware, network, uptime, heap, raw button input, meter diagnostics |
| `POST /api/device` | `name` (1-32 chars; restarts if the hostname changes), `admin` (4-64 chars), `adminClear=1` |
| `GET /api/scan` | WiFi scan; returns `{"scanning":true}` until the results are ready; `?fresh=1` starts a new scan |
| `POST /api/wifi` | `ssid`, `pass` (blank for an open network); saves and restarts |
| `POST /api/forget` | erase the saved network and restart into the setup AP |
| `GET /api/mqtt` | MQTT settings and connection status (the password is never returned) |
| `POST /api/mqtt` | `host` (blank turns MQTT off), `port`, `user`, `pass` (blank keeps the saved one), `passClear=1`, `topic` (blank = default) |
| `GET /api/log` | recent log lines (plain text) |
| `POST /api/reboot` | restart |
| `POST /update` | firmware upload, multipart field `firmware`; `.bin` or `.bin.gz` |

# MQTT

Set the broker on the Settings page. The device connects with its hostname as the client ID and retries every 10 s. Every topic sits under the base topic `T`. The default is `logixoutlet/<id>`. All messages are QoS 0.

## Published by the device

| Topic | Retained | Payload |
|---|---|---|
| `T/status` | yes | `online`, or `offline` (last will) |
| `T/state` | yes | same JSON as `GET /api/controller`, without `uptimeMs` and `lastActionMsAgo`. Sent on every outlet change, every 10 s while the energy values change, and at least every 60 s |
| `T/button/N/action` | no | `single`: one message per button press, physical or simulated |
| `T/result` | no | reply to each command: `{"command":"outlet","ok":true,"message":"Outlet top on."}` |

## Commands

| Topic | Payload | HTTP equivalent |
|---|---|---|
| `T/outlet/N/set` | `ON`, `OFF` or `TOGGLE` (N = `1`, `2`, `top`, `bottom`, `all`) | `POST /api/outlet` |
| `T/set` | JSON: `{"top":"on","bottom":"toggle"}`, `{"outlet1":false}` or `{"all":"off"}` | `POST /api/outlet` |
| `T/press` | `1`, `2`, or `{"button":1}` | `POST /api/controller/press` |
| `T/get` | anything | republishes `T/state` |

```sh
mosquitto_pub -h <broker> -t logixoutlet/c4dd573121fb/outlet/top/set -m ON
mosquitto_pub -h <broker> -t logixoutlet/c4dd573121fb/set -m '{"all":"off"}'
mosquitto_sub -h <broker> -t 'logixoutlet/c4dd573121fb/#' -v
```
