# Logging: drop the `aegis-log` dependency

Formerly D3. Decided 2026-10-17.

Replace it with the simple `aegis::log`
in the foundation module (stderr with a severity tag, no setup, one `setSink` for tests). It can
come back later as an app-installed sink if file or async output is wanted. → 3.1 ([#58](https://github.com/chFleschutz/aegis-engine/issues/58)).
