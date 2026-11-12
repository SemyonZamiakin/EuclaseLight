# Changelog

## 1.1.0 (2026-11-12)

- **Colour follows system load.** New SYSTEM LOAD section: the lights blend from the Primary to the Secondary colour by CPU load, GPU load, GPU temperature (30-90 C) or RAM use, 10 updates per second. Needs Euclase Monitor 1.1.
- Metrics come from Euclase Monitor over a Corundum HFT shared-memory ring (vendored in `core/third_party/corundum`): no HTTP polling, no system call per read. Light re-attaches by itself when Monitor restarts.
- OpenRGB: new live-colour path (`ql_orgb_set_color`): the device is switched to its per-LED Direct mode once, then only LED colours are sent, and only when they change.
- Core C ABI version 2 (`ql_orgb_set_color`, `ql_monitor_read`, `ql_load_unit`, `ql_lerp_color`). 68 core checks (was 46).

## 0.1.0

- First release: detection of GPU, RAM and motherboard; six effects; OpenRGB SDK client; SPD write protection; WPF UI with live preview and tray icon.
