# Shack Libraries

[Русская версия](README.ru.md)

- `tuner_core` defines a hardware-independent asynchronous tuner interface. `ldg::TunerBackend` adapts the LDG controller and its existing operation journal.
- `ldg_core` provides the LDG serial protocol, controller and automation. The ESP-IDF component and standard CMake target have the same name and use the consumer's `arduinojson` target.
- `rotator_core/src` provides the common rotator motion, calibration, input and logger protocol. Its translation units compile with the consumer's `control.hpp` and `build_config.hpp`. The controller facade, board wiring and network adapters live in the firmware.
- `rotator_core/ruckig` is the motion planner. `sources.cmake` lists the CMake sources. G5500 builds the planner from its recursive Arduino sketch source directory.
- [Shack Protocol](protocol/README.md) defines shared HTTP, USB, binary UDP and MQTT messages and provides a read-only reference implementation.

## Applications

| Project | Integration |
|---|---|
| [G5500 Controller](https://github.com/ra3vld/G5500-ESP32S3-Controller) | Rotator firmware with pinned C++ sources and Shack HTTP discovery. |
| [LDGControl-IDF](https://github.com/ra3vld/LDGControl-IDF) | Tuner firmware with pinned C++ sources and Shack tuner reads. |
| [Universal Shack Controller](https://github.com/ra3vld/universal-shack-controller) | ESP32-P4 station controller and HTTP gateway to configured LDG peers. |
| `shack-panel` | Standalone Rust touchscreen application for Raspberry Pi. It uses the USC HTTP API and the [`shack-protocol` Cargo crate](rust/shack-protocol) for Shack tuner state. |

Pin the Cargo dependency to a released repository revision in `Cargo.toml` and retain `Cargo.lock`. The Rust crate checks its wire constants and reference vector against the same protocol contract as the C++ consumers.

Consumers build from the pinned export under `vendor/shack-libs`. Update both components together in USC. Select just the relevant component for each standalone firmware. The export is deterministic and supports source-only and offline builds.

Export `tuner_core` together with `ldg_core`.

```sh
python3 tools/vendor.py /path/to/consumer ldg_core tuner_core rotator_core --revision COMMIT
python3 tools/vendor.py /path/to/consumer ldg_core tuner_core rotator_core --revision COMMIT --check
python3 tools/vendor.py /path/to/g5500 rotator_core --revision COMMIT --destination G5500_Fork/src/shack-libs
```

Components retain their source notices. See [NOTICE](NOTICE) and the component licenses.
