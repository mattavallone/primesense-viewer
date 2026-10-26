# PrimeSense Viewer

A compact live viewer for OpenNI2 PrimeSense depth, infrared, and color
streams. It includes mode buttons, depth range controls, and a toolbar showing
the current stream resolution and measured frame rate.

## Requirements

- Windows x64
- CMake 3.15 or later
- OpenNI2 for Windows x64
- OpenCV development files
- The `primesense-openni2-winusb` repository alongside this repository
- The camera's sensor USB interface using the Windows WinUSB driver

## Build

With the runtime repository checked out next to this one:

```powershell
cmake -S . -B build -DOpenCV_DIR=C:\opencv\build
cmake --build build --config Debug
```

Set `OPENNI2_ROOT` if OpenNI2 is installed in a different location. To use a
runtime repository at another path, set `PRIMESENSE_RUNTIME_DIR` during CMake
configuration.

Build the patched sensor plugin from the runtime repository before running the
viewer. The viewer stages the OpenNI2 redistributable runtime beside its
executable after build; the custom `PS1080.dll` should already be present in
that output directory.

## Controls

- Click **Depth**, **IR**, or **Color** to switch streams.
- Drag the minimum and maximum sliders in Depth mode to adjust the display
  range.
- Press **S** to save the current displayed frame as a PNG.
- Press **Esc** or **Q** to quit. The title-bar close button also exits.

Supported stream modes are selected from the camera's advertised modes.
Color prefers 320x240 on the tested WinUSB setup; depth and IR prefer
640x480. Camera model, firmware, available properties, and frame metadata are
reported in the console.

## License

The code in this repository is licensed under the MIT License. OpenNI2 and
OpenCV are separate dependencies and remain under their respective licenses.
