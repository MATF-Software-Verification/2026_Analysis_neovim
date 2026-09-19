// CBMC's C front-end cannot parse the Apple Blocks syntax
// (`int (^)(const struct dirent *)`) that macOS's real <dirent.h> declares
// for scandir_b() under `#ifdef __BLOCKS__`. Nothing this project compiles
// actually calls scandir_b() -- it's only pulled in transitively via
// <uv.h> -- so this shim undefines the compiler-predefined __BLOCKS__ macro
// before deferring to the real system header, which then skips that
// declaration entirely. Only used for the CBMC build (see ../run_cbmc.sh);
// the clang-based fuzzing/unit-test builds use the real header directly and
// never hit this parser limitation.
#undef __BLOCKS__
#include_next <dirent.h>
