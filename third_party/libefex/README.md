# libefex protocol core

Source: https://github.com/YuzukiTsuru/libefex
Revision: `3752e38ff8e69190c53cd43290a8102beab55e73` (MIT, see LICENSE).

Only the FEL/FES protocol core and headers are vendored. UpgradeTool supplies
USB discovery, full physical-port matching, interface ownership and bulk I/O
in `backends/allwinner/`, using the official usbUDisc driver on Windows and
the existing libusb target on Linux/macOS.
The upstream USB backends, bundled libusb, applications and Rust bindings are excluded.
