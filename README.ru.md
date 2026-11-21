# Общие библиотеки шека

[English](README.md)

- `ldg_core` содержит последовательный протокол LDG, контроллер и автоматику. Компонент ESP-IDF и обычная цель CMake имеют одинаковое имя и используют цель `arduinojson` приложения.
- `rotator_core/src` содержит общий код движения, калибровки, ввода и протоколов логгеров. Исходники компилируются с `control.hpp` и `build_config.hpp` приложения. Контроллер, распиновка и сетевые адаптеры входят в прошивку.
- `rotator_core/ruckig` содержит планировщик движения. `sources.cmake` перечисляет исходники для CMake, `library.json` подключает планировщик в PlatformIO.

Приложения собираются из снимка фиксированной ревизии в `vendor/shack-libs`. В USC обновляются оба компонента вместе. В каждой отдельной прошивке выбирается нужный компонент. Снимок воспроизводим и пригоден для офлайн-сборки.

```sh
python3 tools/vendor.py /path/to/consumer ldg_core rotator_core --revision COMMIT
python3 tools/vendor.py /path/to/consumer ldg_core rotator_core --revision COMMIT --check
```

Уведомления авторов и лицензии сохранены. См. [NOTICE](NOTICE) и лицензии компонентов.
