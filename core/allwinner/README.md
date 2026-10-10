# Allwinner backend

The C++ firmware, flashing and conversion implementations are derived from
OpenixCLI revision `aeee88d610d42e1ca7498dcfe60619e7f15c7860`:
https://github.com/YuzukiTsuru/OpenixCLI (MIT, see LICENSE.OpenixCLI).
Copyright (c) 2025 Yuzuki Tsuru.

Protocol I/O uses the pinned libefex C core. Unlike the reference reconnect
selection, devices are matched by their complete USB physical port chain.
Verification failures are errors rather than successful writes with warnings.
Qt Core supplies portable Unicode file access and atomic output files; no Rust
runtime or command-line subprocess is required.
