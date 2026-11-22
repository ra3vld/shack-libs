# Общие библиотеки шека

[English](README.md)

- `tuner_core` задаёт аппаратно-независимый асинхронный интерфейс тюнера. `ldg::TunerBackend` подключает LDG через действующий контроллер и его журнал операций.
- `ldg_core` содержит последовательный протокол LDG, контроллер и автоматику. Компонент ESP-IDF и обычная цель CMake имеют одинаковое имя и используют цель `arduinojson` приложения.
- `rotator_core/src` содержит общий код движения, калибровки, ввода и протоколов логгеров. Исходники компилируются с `control.hpp` и `build_config.hpp` приложения. Контроллер, распиновка и сетевые адаптеры входят в прошивку.
- `rotator_core/ruckig` содержит планировщик движения. `sources.cmake` перечисляет исходники для CMake. G5500 собирает планировщик из вложенного каталога исходников Arduino.
- [Shack Protocol](protocol/README.ru.md) задаёт общие сообщения для HTTP, USB, бинарного UDP и MQTT. Включает эталонную реализацию чтения состояния.

## Приложения

| Проект | Интеграция |
|---|---|
| [G5500 Controller](https://github.com/ra3vld/G5500-ESP32S3-Controller) | Прошивка поворотки с закреплённым снимком исходников C++ и обнаружением устройств по Shack HTTP. |
| [LDGControl-IDF](https://github.com/ra3vld/LDGControl-IDF) | Прошивка тюнера с закреплённым снимком исходников C++ и чтением состояния по Shack Protocol. |
| [Universal Shack Controller](https://github.com/ra3vld/universal-shack-controller) | Контроллер станции на ESP32-P4 и HTTP-шлюз к настроенным узлам LDG. |
| `shack-panel` | Самостоятельное сенсорное приложение на Rust для Raspberry Pi. Использует HTTP API USC и [Cargo-крейт `shack-protocol`](rust/shack-protocol) для чтения состояния тюнера. |

Закрепляйте зависимость Cargo на ревизии опубликованного выпуска в `Cargo.toml` и сохраняйте `Cargo.lock`. Тесты Rust-крейта сверяют константы и контрольное сообщение с общим контрактом протокола, который используют и прошивки C++.

Приложения собираются из снимка фиксированной ревизии в `vendor/shack-libs`. В USC обновляются оба компонента вместе. В каждой отдельной прошивке выбирается нужный компонент. Снимок воспроизводим и пригоден для офлайн-сборки.

Экспортируйте `tuner_core` вместе с `ldg_core`.

```sh
python3 tools/vendor.py /path/to/consumer ldg_core tuner_core rotator_core --revision COMMIT
python3 tools/vendor.py /path/to/consumer ldg_core tuner_core rotator_core --revision COMMIT --check
python3 tools/vendor.py /path/to/g5500 rotator_core --revision COMMIT --destination G5500_Fork/src/shack-libs
```

Уведомления авторов и лицензии сохранены. См. [NOTICE](NOTICE) и лицензии компонентов.
