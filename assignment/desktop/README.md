# Electra Qt desktop

Native C++17 / Qt 6.5.3 Widgets user and administrator applications.

See [Qt migration and delivery](../docs/QT_MIGRATION.md) for scope, API/cache design, launch/build instructions and explicitly skipped acceptance work.

- `src/client.*`: asynchronous REST, in-memory authentication, WebSocket recovery and SQLite cache.
- `src/ui.*`: common native widgets, theme, QPainter visuals and dialogs.
- `src/user_window.*`: user charging flow, account, wallet and history.
- `src/admin_window.*`: administration and Qt Charts, including load forecasting.
- `scripts/build-linux.sh`, `scripts/build-windows.ps1`: native builds.
- `tests`: optional Qt Test sources; not enabled by packaged launchers.

No WebEngine / embedded browser. Web clients and FastAPI remain in the parent project.
