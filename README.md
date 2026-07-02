# RTBase

## Overview

This project provides a foundational ray tracing renderer for students studying Advanced Computer Graphics. The codebase is structured to facilitate learning by including sections where students need to complete missing implementations.

## Project Structure

```
Renderer/
│── Core.h               # Core mathematical and utility functions
│── Renderer.h           # Main ray tracing logic
│── Sampling.h           # Monte Carlo sampling utilities
│── Scene.h              # Scene representation and camera logic
│── SceneLoader.h        # Loads scenes and configurations
│── Lights.h             # Light source definitions
│── Geometry.h           # Geometric structures and operations
│── Imaging.h            # Image generation and storage
│── Materials.h          # Materials and shading models
│── GEMLoader.h          # External loader for scene assets
│── GamesEngineeringBase.h  # Base utilities for integration
```

## Scenes

Scenes can be found on the Moodle page. Please download them and place them in the working directory.

## Tasks for Students

Several functions and algorithms are left incomplete and require implementation. Look for comments such as:

```cpp
// Add code here
```

## Notes

- Ensure your implementations are efficient and well-commented.
- Test incremental changes using appropriate scenes.
- Working on Mac for development/testing.

## Volumetric subsurface path tracing

The homogeneous-medium implementation and its formulas are explained in
[`VOLUMETRIC_SUBSURFACE_PATH_TRACING.md`](VOLUMETRIC_SUBSURFACE_PATH_TRACING.md).
Render the included small test scene with:

```bash
./build/RTAssignment.app/Contents/MacOS/RTAssignment \
  -scene volumetric-cornell -SPP 64 -outputFilename volumetric.hdr
```

Run the sampling checks with:

```bash
ctest --test-dir build --output-on-failure
```

## macOS / Xcode

The source now builds on both Windows and macOS. Windows can continue using the existing Visual Studio solution, while macOS uses CMake to generate an Xcode project with a Metal-backed `GamesEngineeringBase` window.

Install CMake if you do not already have it:

```bash
brew install cmake
```

Generate and open the Xcode project:

```bash
cmake -S . -B build-xcode -G Xcode
open build-xcode/RTAssignment.xcodeproj
```

In Xcode, select the `RTAssignment` scheme and run. The generated scheme uses the project folder as its working directory so scene folders such as `cornell-box` are found correctly.
  
