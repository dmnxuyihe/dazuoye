# Electra Qt desktop

Native C++17 / Qt 6.5.3 Widgets user and administrator applications.

See [Qt migration and delivery](../docs/QT_MIGRATION.md) for scope, API/cache design and launch/build instructions.

- `src/client.*`: asynchronous REST, in-memory authentication, WebSocket recovery and SQLite cache.
- `src/ui.*`: common native widgets, theme, QPainter visuals and dialogs.
- `src/user_window.*`: user charging flow, account, wallet and history.
- `src/admin_window.*`: administration and Qt Charts, including load forecasting.
- `scripts/build-linux.sh`, `scripts/build-windows.ps1`: native builds.
- `tests`: optional Qt Test sources; not enabled by packaged launchers.

Only the map uses Qt WebEngine + Leaflet + online OSM tiles; other pages and business actions remain native C++ / Qt. Linux dependencies and runtime configuration are in [the integration guide](../docs/INTEGRATION_LINUX.md). Web clients and FastAPI remain in the parent project.

## Native page reconstruction

See [current page mapping](../docs/QT_MIGRATION.md). `src/visuals.*` implements the native segmented controls and data graphics. `assets/web-ui.json` contains existing Web icon paths/labels, rendered by Qt SVG.

Build `test-visuals` and `test-map` with the existing CMake test configuration, then run them with `QT_QPA_PLATFORM=offscreen` and the same Qt library/plugin environment as the applications. `test-visuals` uses the committed `tests/fixtures` data and an isolated API double; it does not require a server or change production accounts. This targeted suite is separate from full business acceptance.
