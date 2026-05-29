# clab

C/C++ project scaffold. No source code yet.

## Build & Toolchain

- **Build system:** CMake (in-source build dir at `build/`)
- **Formatter:** `.clang-format` (Google-based, 4-space indent, format-on-save via clangd)
- **LSP:** clangd (`--compile-commands-dir=${workspaceFolder}`)
- **Compiler artifacts** are gitignored (`.o`, `.so`, `.a`, `build/`, `compile_commands.json`, etc.)

## Headers

`IncludeBlocks: Preserve` — clang-format preserves user-defined groups
(separated by blank lines). Within each group, `IncludeCategories` define
the sort priority:

| Priority | Pattern | Example headers |
|----------|---------|-----------------|
| 1 | `<` + `.h` | `<fcntl.h>`, `<unistd.h>`, `<gtest/gtest.h>`, `<libaio.h>` |
| 2 | `<` + no `.h` or `.hpp` | `<vector>`, `<cstdlib>`, `<memory>` |
| 3 | `<` + `.hpp` | `<boost/container/small_vector.hpp>` |
| 4 | `""` | `"blk/aio.h"`, `"common/buffer.h"` |

Within each priority, headers are sorted alphabetically. To create or
remove a group, add or remove the blank line between blocks. Includes
never migrate across blank lines.
