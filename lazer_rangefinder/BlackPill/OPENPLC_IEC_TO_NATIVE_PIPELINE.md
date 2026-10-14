# OpenPLC Editor + Runtime: IEC 61131-3 → native code pipeline

This note summarizes how [Autonomy-Logic/openplc-editor](https://github.com/Autonomy-Logic/openplc-editor) and [Autonomy-Logic/openplc-runtime](https://github.com/Autonomy-Logic/openplc-runtime) turn PLC programs (ladder, FBD, ST, etc.) into compiled native code, and how a standard function like **AND** ends up in C.

Sources: `openplc-runtime/docs/COMPILATION_FLOW.md`, `openplc-runtime/docs/EDITOR_INTEGRATION.md`, `openplc-editor/src/main/modules/compiler/compiler-module.ts`, `openplc-editor/docs/external-binaries-strategy.md`, and MatIEC’s `lib/iec_std_functions.h` from [Autonomy-Logic/matiec](https://github.com/Autonomy-Logic/matiec) (`openplc-master` branch).

---

## 1. End-to-end flow (two deployment targets)

### A. OpenPLC Runtime v4 (Linux / Docker)

1. **Editor (on the developer PC)**  
   User draws/writes IEC 61131-3, then **Compile** runs a **local** toolchain:
   - Project **JSON → PLCopen XML** (`plc.xml`).
   - **`xml2st`** ([Autonomy-Logic/xml2st](https://github.com/Autonomy-Logic/xml2st)): XML → **Structured Text** `program.st`, plus steps that produce debug/glue-related artifacts.
   - **`iec2c`** (MatIEC binary from [Autonomy-Logic/matiec](https://github.com/Autonomy-Logic/matiec)): ST → **ANSI C** (`Config0.c`, `Res0.c`, `glueVars.c`, `debug.c`, headers under `lib/`, etc.).
   - **Upload** packages those sources into **`program.zip`**.

2. **Runtime (headless server)**  
   - Validates and extracts the ZIP under `core/generated/`.
   - **`scripts/compile.sh`**: `gcc` / `g++` compiles the generated `.c` / `.cpp` into **`build/new_libplc.so`**, then `compile-clean.sh` renames to `libplc_<timestamp>.so`.
   - PLC core **`dlopen`**s that shared library and resolves symbols such as `ext_config_init__`, `ext_config_run__` (scan cycle).

See: [Runtime COMPILATION_FLOW.md](https://github.com/Autonomy-Logic/openplc-runtime/blob/main/docs/COMPILATION_FLOW.md), [EDITOR_INTEGRATION.md](https://github.com/Autonomy-Logic/openplc-runtime/blob/main/docs/EDITOR_INTEGRATION.md).

### B. Arduino (or other boards via `arduino-cli`)

Same **XML → xml2st → ST → iec2c → C/C++** stage in the Editor. After that, the Editor’s compiler service uses **`arduino-cli`** to build a **board binary** instead of uploading a ZIP to Runtime v4.

Documented pipeline diagram in the Editor repo:

```text
XML Project File  →  xml2st  →  ST File  →  iec2c  →  C/C++ Files  →  arduino-cli  →  Board Binary
```

See: [openplc-editor/docs/external-binaries-strategy.md](https://github.com/Autonomy-Logic/openplc-editor/blob/development/docs/external-binaries-strategy.md).

---

## 2. Where each tool runs

| Step | Tool | Typical invocation (from Editor) | Output |
|------|------|----------------------------------|--------|
| XML → ST | `xml2st` | e.g. `--generate-st <plc.xml>` | `program.st` |
| ST → C | `iec2c` (MatIEC) | `iec2c -f -p -i -l program.st` (cwd = build folder) | `Config0.c`, `Res0.c`, … |
| C → native (Runtime) | `gcc` / `g++` | On server via `scripts/compile.sh` | `libplc_*.so` |
| C → firmware (Arduino) | `arduino-cli` | Editor-driven build | `.hex` / flash image |

The Editor bundles **xml2st** and **iec2c** per OS/arch (see `scripts/download-binaries.ts`, `binary-versions.json`).

---

## 3. How “functions” like AND become native code

### 3.1 Compiler side (`iec2c`)

- MatIEC parses ST (and, via the XML→ST step, the semantics of ladder/FBD exported to ST).
- **Overloaded** IEC operators/functions (e.g. `AND` on `BOOL`) are resolved to **explicitly typed** C symbols in later stages (comments in MatIEC headers state that **stage 3** infers types, **stage 4** emits the C call).
- Generated C calls functions such as **`AND_BOOL`** or **`AND__BOOL__BOOL`** (naming follows IEC explicit typing / overloading rules).

### 3.2 Runtime library side (MatIEC `lib/`)

Standard behavior is **not** hand-written per project; it comes from MatIEC’s **IEC standard library in C**, especially:

- `lib/iec_std_functions.h` — implementations via macros that expand to **`static inline`** functions.
- `lib/iec_std_lib.h`, `iec_types.h` (or equivalents shipped in generated `lib/`) — types such as **`IEC_BOOL`** (`uint8_t` in OpenPLC runtime’s [iec_types.h](https://github.com/Autonomy-Logic/openplc-runtime/blob/main/core/src/lib/iec_types.h)), **`EN`/`ENO`**, etc.

### 3.3 AND for BOOL (concrete mechanism)

In `iec_std_functions.h`, **AND** for boolean uses the **`__arith_expand`** macro with the C operator **`&&`**:

```c
__arith_expand(AND_BOOL, BOOL, && )
__arith_expand(AND__BOOL__BOOL, BOOL, && )
```

`__arith_expand` expands to a **variadic** `static inline` function that:

- Honors **`EN` / `ENO`** via `TEST_EN`.
- Loops over **`param_count`** operands and combines them with the given **C operator** (`&&` for BOOL `AND`).

Core pattern (abbreviated from MatIEC):

```c
#define __arith_expand(fname, TYPENAME, OP)                                 \
static inline TYPENAME fname(EN_ENO_PARAMS, UINT param_count, TYPENAME op1, ...) { \
  va_list ap;                                                               \
  UINT i;                                                                   \
  TEST_EN(TYPENAME)                                                        \
  va_start(ap, op1);                                                        \
  for (i = 0; i < param_count - 1; i++) {                                   \
    op1 = op1 OP (TYPENAME)va_arg(ap, VA_ARGS_##TYPENAME);                  \
  }                                                                         \
  va_end(ap);                                                               \
  return op1;                                                               \
}
```

For **bitstring** types (`BYTE`, `WORD`, …), the same file uses **`&`** (bitwise AND) via `__ANY_NBIT` expansion, e.g. `AND_BYTE`, etc.

So at **native** level, **BOOL AND** is **short-circuiting logical AND** on the mapped C type; **bitstring AND** is **bitwise `&`** over integers.

---

## 4. Extra Editor glue (C++ / ST helpers)

The Editor also generates supporting code (e.g. **glue variables**, **debug.c**, custom **C++ blocks**). For some targets, helpers account for MatIEC’s **wrapped IEC values** (`__IEC_TYPE_t`: value + flags), e.g. in `src/utils/cpp/generateSTCode.ts` (flat arrays for `iec2c` output).

That is **adjacent** to standard functions like AND but explains why generated C sometimes mixes **raw `IEC_*` types** and **wrapped** structs.

---

## 5. Quick reference links

| Resource | URL |
|----------|-----|
| OpenPLC Editor | https://github.com/Autonomy-Logic/openplc-editor |
| OpenPLC Runtime v4 | https://github.com/Autonomy-Logic/openplc-runtime |
| xml2st | https://github.com/Autonomy-Logic/xml2st |
| MatIEC (`iec2c`) | https://github.com/Autonomy-Logic/matiec |
| Runtime: compilation flow | `openplc-runtime/docs/COMPILATION_FLOW.md` |
| Runtime: editor integration | `openplc-runtime/docs/EDITOR_INTEGRATION.md` |

---

## 6. Summary one-liner

**PLC logic → PLCopen XML → `xml2st` → Structured Text → `iec2c` (MatIEC) → C + standard lib headers → `gcc` (Runtime `.so`) or `arduino-cli` (firmware).**  
**AND (BOOL)** becomes calls into **`AND_BOOL` / `AND__BOOL__BOOL`**, implemented in **`iec_std_functions.h`** as **`static inline`** variadic functions using the **`&&`** operator.
