# LiteDDC

A tiny Windows tray application that adjusts your external monitor's
**brightness**, **contrast**, and **saturation** over DDC/CI — plus a few
pragmatic extras: monitor speaker volume and R/G/B gain sliders, and
NVIDIA Digital Vibrance where an NVIDIA display is present. The lite
alternative to ClickMonitorDDC7.2.

## What it does

- Lives in the system tray. Scroll the mouse wheel while hovering the icon
  to adjust brightness (right-click for the full menu).
- One settings window: per-parameter sliders (brightness, contrast,
  saturation, volume, R/G/B gains, GPU vibrance), monitor scope,
  scroll-step size, start-with-Windows toggle.
- Settings persist to `%LOCALAPPDATA%\LiteDDC\settings.json`.
- Handles hot-plug and sleep/wake re-enumeration; no background polling.
- Laptop internal panels are detected via their output technology and
  excluded automatically — only external monitors are listed.

## What it deliberately does NOT do (v1 non-goals)

- Input source switching, power state, monitor color presets/temperature.
- Per-application profiles or hotkeys.
- Monitor capability/quirk databases beyond basic capability parsing.
- macOS/Linux support.
- Cloud sync, telemetry, or auto-updates.
- Any UI beyond the tray icon + context menu + one settings window.

## Requirements

- Windows 10 1809+ (x64) or Windows 11 (x64 / ARM64).
- External monitor(s) with **DDC/CI enabled** — no runtime dependencies,
  static CRT build, single `.exe`.

**Laptop internal panels are not supported** — they use a different
backlight interface than DDC/CI. External monitors only.

## Troubleshooting

If no monitor is detected or the controls do nothing: enable DDC/CI in
your monitor's on-screen display menu. It is often called "DDC/CI" and
some monitors ship with it disabled by default.

## Usage

1. Download `LiteDDC-x.y.z-x64.zip` (or `-ARM64`), extract, run
   `LiteDDC.exe`.
2. A tray icon appears; scroll over it to adjust brightness.
3. Right-click → *Settings…* for everything else; *Exit* to quit.

## Building from source

```
tools\build.cmd Debug x64     # daily driver
tools\ship.cmd                # Release x64 + ARM64 zips into releases\
```

Requires VS 2022 Build Tools with the C++ workload. Native C++17 / raw
Win32, W4 warnings-as-errors, zero third-party dependencies.

See `docs/ROADMAP.md` for the design history and `docs/` for PRD,
architecture, UI spec, and QA protocols.

## License

MIT — see `LICENSE`.
