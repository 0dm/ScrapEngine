# ~~SlopEngine~~ ScrapEngine

<table align="center">
  <tr>
    <td align="center">
      <img src="https://github.com/user-attachments/assets/8cb16793-0478-4e30-a66a-24699cd412b5" height="300"/>
    </td>
    <td align="center">
      <img src="https://github.com/user-attachments/assets/a926b738-9a6c-4e2a-89ec-7c97f5adaf94" height="300"/>
    </td>
  </tr>
</table>


Apple-only C++23 graphics and physics engine. Main app uses Metal. Scene/editor code is still mixed between newer Metal work and older Vulkan-era code.

features:
- ECS-style scene and components
- forward PBR rendering
- image-based lighting and skybox
- XPBD rigid body and cloth physics
- Dear ImGui debug UI
- glTF scene loading

# Platforms
- macOS
- iOS

# Build

Needs:
- CMake 3.30+
- `vcpkg`
- `slangc` on `PATH`

Example:

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_INSTALLED_DIR="$PWD/build/vcpkg_installed" \
  -DVCPKG_TARGET_TRIPLET=arm64-osx
cmake --build build --target ScrapEngine
```

# Run

```bash
./build/ScrapEngine.app/Contents/MacOS/ScrapEngine \
  --ibl path/to/sky.hdr \
  path/to/scene.gltf
```

Run `--help` for the rest of the flags.

# Repo layout
- `src/app/` app, scene, components, platform code
- `src/gpu/metal/` Metal renderer, IBL, presentation
- `src/physics/` XPBD physics
- `src/launcher/` launcher and CLI
- `shaders/` shader sources
- `assets/` bundled assets

# Notes
- This project started from [SauceEngine](https://github.com/P0k3rf4ce/SauceEngine) but has been rewritten a lot.
- `src/editor/` and some Vulkan-era files are still in the repo, but they are not the main path right now.

# License

See `LICENSE` if present.
