# LiteDDC

Tray app for monitor brightness/contrast/saturation over DDC/CI, plus
volume, R/G/B gain, and NVIDIA vibrance. The small alternative to
ClickMonitorDDC7.2.

## Use

Scroll the tray icon to change brightness. Right-click for sliders and
settings.

## Doesn't do

Input switching, power control, presets, hotkeys, per-app profiles,
telemetry, auto-update, macOS/Linux.

## Requirements

Windows 10 1809+ or 11, x64/ARM64. Monitor with DDC/CI enabled.

## Not working?

Turn on DDC/CI in the monitor's OSD menu (sometimes called "OSD
lockout/Control"). Most monitors ship with it off.

## Build

```
tools\build.cmd Debug x64
tools\ship.cmd
```

VS 2022 Build Tools, C++ workload. No dependencies. Run `tests/`
before shipping.

## License

MIT