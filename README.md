# PipeWrench

A testing tool for evaluating Linux desktop screen capture capabilities, specifically focusing on the xdg-desktop-portal interface and PipeWire-based screen sharing on Linux.

## Overview

PipeWrench is a diagnostic utility designed to test screen capture capabilities on Linux using xdg-desktop-portal and PipeWire. It provides a simple interface to exercise the portal's ScreenCast API and helps diagnose compatibility issues with different implementations.

The application attempts to:
1. Connect to the xdg-desktop-portal service via D-Bus
2. Create a screen capture session using the ScreenCast interface
3. Request access to screen content
4. Provide diagnostic information about the process

Additionally, PipeWrench includes a fallback mechanism using direct X11 capture when the portal service fails.

## Known Issues with Ubuntu 24.04's xdg-desktop-portal

**Critical Bug: Segmentation Fault in xdg-desktop-portal**

The default xdg-desktop-portal package (version 1.18.4) in Ubuntu 24.04 contains a critical bug that causes the service to crash with a segmentation fault when applications attempt to use the ScreenCast interface. Specifically:

- When an application calls the `CreateSession` method on the `org.freedesktop.portal.ScreenCast` interface, the portal service crashes
- The crash manifests as a core dump (`Result: core-dump`) in the systemd service logs
- The error appears related to signal subscription handling in the portal implementation
- Both the GTK and GNOME backends exhibit this issue

This makes it impossible to use standard screen sharing functionality in applications that rely on the portal, such as web browsers for video conferencing, many screen recording tools, and remote desktop applications.

### Error Details

When the portal crashes, the following can be observed in logs:

```
Active: failed (Result: core-dump) 
Process: ExecStart=/usr/libexec/xdg-desktop-portal (code=dumped, signal=SEGV)
```

The application receives:

```
GDBus.Error:org.freedesktop.DBus.Error.NoReply: Message recipient disconnected from message bus without replying
```

### Workarounds

PipeWrench implements two workarounds:

1. **Direct X11 Capture**: Falls back to using X11's screen capture capabilities using XCopyArea and pixmap methods
2. **Manual Portal Configuration**: The user can configure their system to use alternative portal backends by creating a configuration file at `~/.config/xdg-desktop-portal/portals.conf` (though this doesn't fully resolve the issue with the current Ubuntu packages)

### Alternative Solutions

1. Use applications like OBS Studio that have their own screen capture implementations
2. Wait for a fixed version of xdg-desktop-portal in Ubuntu updates

## Building and Running

### Prerequisites

- CMake 3.10 or higher
- GTK3 development libraries
- GLib/GObject development libraries
- X11 development libraries
- UUID library

### Build Instructions

```bash
mkdir -p build
cd build
cmake ..
make
```

### Running

```bash
cd build
./pipewrench
```

## License

[Insert your license information here]