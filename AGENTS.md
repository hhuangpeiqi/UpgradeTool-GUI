# AGENTS.md

## README Content

Keep `README.md` limited to essential instructions and explanations. Do not add nonessential content.

## Version Maintenance (Automatic After Each Change)

Use semantic versioning (`MAJOR.MINOR.PATCH`). After each change, bump the version once according to the scope of the change, without waiting for a user reminder.

- **Single source of truth**: `TOOL_VERSION` in `core/Version.h`. Do not write the version number elsewhere.
- **Rules** (use the highest applicable level):
  - MAJOR `x.0.0`: Breaking changes (CLI arguments or exit codes, protocol or device behavior, or changes requiring users to update configurations or scripts).
  - MINOR `0.x.0`: New backward-compatible features (new commands, pages, or configuration options).
  - PATCH `0.0.x`: Fixes, refactoring, optimizations, or internal implementation changes.
- **No version bump**: Changes limited to documentation, comments, tests, build scripts, CI, or similar areas with no behavioral changes, as well as changes under `third_party/`.
- After bumping the version, include it in the commit message, for example: `feat: add batch erase (v2.2.0)`.
