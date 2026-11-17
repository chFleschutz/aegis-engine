# Preserve mirrored transforms

Formerly D6. Decided 2026-10-17.

`decomposeTRS` keeps negative scale; check winding of
mirrored instances in the same PR. → 1.8c ([#43](https://github.com/chFleschutz/aegis-engine/issues/43)); the front-face flip is still open ([#171](https://github.com/chFleschutz/aegis-engine/issues/171)).
