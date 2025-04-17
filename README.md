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

## Sauron Eye - MQTT Integration

The `sauron-eye` branch includes MQTT client functionality that allows PipeWrench to publish captured images to an MQTT broker. This feature enables integration with remote monitoring systems, automation, or any MQTT-compatible application.

### MQTT Features

- Connect to any MQTT broker with optional username/password authentication
- Publish captured window and screen images to configurable topics
- Include metadata with each image (filename, timestamp, etc.)
- Auto-publish option to send all captures automatically
- Manual publishing of previous captures
- Real-time connection status feedback

### Using MQTT Functionality

1. **Connect to an MQTT Broker**:
   - Enter the broker hostname/IP and port (default: localhost:1883)
   - Provide optional username and password if your broker requires authentication
   - Click "Connect"

2. **Configure Topics**:
   - Set the topic prefix for your images (default: pipewrench/captures)
   - Images will be published to `<prefix>/image`
   - Metadata will be published to `<prefix>/metadata`

3. **Publishing Images**:
   - Enable "Auto-publish captures" to automatically send all new captures to the broker
   - Use the "Publish Last Capture" button to manually publish the most recent capture

### Required Dependencies

- libmosquitto-dev (MQTT client library)

## Important: Wayland Compatibility Notice

**PipeWrench requires X11 to function properly for screen and window captures.**

If you're running under Wayland (the default display server in many recent Linux distributions), some screen/window captures will fail with error messages like:
```
❌ Failed to get screen image
❌ Failed to capture screen image
```

### Disabling Wayland in GDM

To disable Wayland and use X11 instead:

1. Edit the GDM configuration file:
   ```bash
   sudo nano /etc/gdm3/custom.conf
   ```

2. Find the line `#WaylandEnable=false` and uncomment it:
   ```bash
   WaylandEnable=false
   ```

3. Save the file and restart GDM:
   ```bash
   sudo systemctl restart gdm
   ```

4. Log back in. You should now be using an X11 session where PipeWrench will work correctly.

You can verify you're using X11 by running:
```bash
echo $XDG_SESSION_TYPE
```
It should output `x11` rather than `wayland`.

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