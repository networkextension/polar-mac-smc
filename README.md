# polar-mac-smc

macOS launchd service that forces SMC fan speed to a fixed RPM, for machines
whose SMC cannot manage fan speed automatically (in auto mode the SMC pins the
fans at max RPM permanently).

Default target: **2900 RPM**.

## Components

| File | Purpose |
|---|---|
| `smcfan.c` | CLI tool talking to AppleSMC via IOKit. Intel (`fpe2`) and Apple Silicon (`flt`) fan keys both supported. |
| `com.local.smcfan.plist` | LaunchDaemon: runs `smcfan set 2900` at boot and re-asserts it every 60 s (survives sleep/wake and SMC resets). |
| `install.sh` | Build + install + load, in one step. |

## Install

```sh
sudo ./install.sh          # default 2900 rpm
sudo ./install.sh 3200     # custom rpm
```

Installs `/usr/local/bin/smcfan`, `/Library/LaunchDaemons/com.local.smcfan.plist`,
logs to `/var/log/smcfan.log`. Requires clang (Command Line Tools); on machines
without a compiler, copy a prebuilt `smcfan` binary from another Mac of the
same architecture into `/usr/local/bin/` and install the plist manually.

## Usage

```sh
smcfan status        # fan count, actual/target rpm, mode
sudo smcfan set 2900 # force all fans to 2900 rpm (clamped to fan min/max)
sudo smcfan auto     # restore SMC automatic mode (on broken machines: max rpm)
```

## Uninstall

```sh
sudo launchctl bootout system/com.local.smcfan
sudo rm /Library/LaunchDaemons/com.local.smcfan.plist /usr/local/bin/smcfan
```

## Notes

- `set` writes `F<n>Md = 1` (forced) and `F<n>Tg = rpm` for every fan; it
  re-writes unconditionally each cycle so competing fan controllers cannot
  silently take over.
- Conflicts with Macs Fan Control and similar tools — remove their privileged
  helper (`/Library/LaunchDaemons/com.crystalidea.macsfancontrol.smcwrite.plist`)
  or they will fight over the target keys.
- Tested on Mac15,8 (arm64, macOS 14.x); legacy Intel `FS! ` mode-bitmask SMCs
  are supported as a fallback.
