# clab

C/C++ project scaffold. No source code yet.

## Build & Toolchain

- **Build system:** CMake (in-source build dir at `build/`)
- **Formatter:** `.clang-format` (Google-based, 4-space indent, format-on-save via clangd)
- **LSP:** clangd (`--compile-commands-dir=${workspaceFolder}`)
- **Compiler artifacts** are gitignored (`.o`, `.so`, `.a`, `build/`, `compile_commands.json`, etc.)

## Headers

`.clang-format` sorts includes into 3 groups (in order):

1. `<>` with `.h` or `.hpp` – C system / third-party headers
2. `<>` without `.h` or `.hpp` – C++ stdlib / other third-party
3. `""` – project headers

Each group sorted alphabetically.
