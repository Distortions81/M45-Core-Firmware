# Device Usage

This page covers the normal device flow after flashing M45 Core Firmware.

## Setup Mode

On a clean install, the device has no saved Wi-Fi credentials and starts an
open setup access point named like `m-ABCD`. If the ESP32 MAC cannot be read,
the fallback network name is `m-idf`.

Join that Wi-Fi network and open the address shown on the display. The address
is normally generated from the ESP32 MAC in the `10.x.x.1` range; the fallback
address is `10.45.0.1`.

The setup page:

- Scans nearby Wi-Fi networks.
- Lets you choose a scanned network or enter an SSID manually.
- Tests credentials before saving them.
- Saves Wi-Fi credentials and reboots after a successful test.

## Normal Operation

After reboot, the ESP32 joins the saved Wi-Fi network and shows its station IP
on the display. Open that IP in a browser.

| Route | Purpose |
| --- | --- |
| `/` or `/stats` | Live hashrate, pool, share, payout, memory, and build stats. |
| `/settings` | Display, performance, pool, wallet, reboot, and factory reset controls. |
| `/wifi` | Change Wi-Fi credentials after setup. |
| `/licenses` | Project and third-party license text. |
| `/health` | Plain `ok` health check. |

Captive-portal probe paths redirect to the setup page while setup mode is
active.

## Mining Settings

Default pool settings are compiled into the firmware:

| Setting | Default |
| --- | --- |
| Main pool | `tinyminer.m45core.com` |
| Backup pool | `solobtc.nmminer.com` |
| Port | `3333` |
| Worker name | `0M45` |

Configure your wallet and pool settings from `/settings`. The firmware uses
plain TCP Stratum. TLS is not enabled in the current sdkconfig.

The firmware computes a startup benchmark and can store a suggested difficulty
if none has been configured. The stats page reports current difficulty, share
rate, accepted/rejected shares, best share, total hashes, and block-found
alerts.

## Display Controls

The BOOT button advances display pages during normal operation. The same button
also wakes the display when display sleep is active.

Display options are stored in NVS and can be changed from `/settings`:

- Flip screen.
- Screen saver.
- Light or dark theme.
- Brightness.
- Sleep timeout.
- LCD text outline for the ideaspark LCD build.
- Block-found alerts.

Holding BOOT for 5 seconds factory resets the device, so use short presses for
page navigation.

## Factory Reset

Factory reset erases saved Wi-Fi, pool, wallet, display, and system settings.
It does not replace firmware.

Use any of these reset paths:

- `/settings` -> `Factory reset`.
- Hold BOOT for 5 seconds at startup.
- Hold BOOT for 5 seconds while running.
- Flash with the browser flasher and keep `Reset WiFi and pool settings`
  checked.

## Host Benchmarks

Run a lightweight web UI benchmark against a device:

```sh
./scripts/benchmark-web.sh http://192.168.1.42
```

Run a longer hashrate benchmark from `/api/stats`:

```sh
./scripts/benchmark-hashrate.sh --duration 1800 --warmup 300 http://192.168.1.42
```

Both benchmark scripts default to `http://10.45.0.1`, which is useful only for
setup-mode or fallback-address testing.
