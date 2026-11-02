# AGENTS.md — Workbench

Workbench is a small desktop application built on **Raven** (a C GUI library):
a menu bar, a toolbar, a side bar, a status bar, four panes (Application,
Instrument panel, User guide, Preferences), a JSONC configuration file with a
JSON Schema, an embedded documentation browser and right-click menus.

The same application is written **three times** — C++, Rust and Python — over
one `workbench.jsonc`, one `workbench.schema.json` and one `help/` folder. A
change to behaviour, layout or settings belongs in all three.

It is also meant to be **copied**: this folder is self-contained apart from
Raven itself, so it can become its own project or the template of the next one.

---

## What is here

| Path | What it is |
|---|---|
| `cpp/main.cpp` | the C++ shell: window, menus, toolbar, side bar, panes, status bar, actions, frame callback, context menus, tests |
| `cpp/panel/panel.h`, `panel.cpp` | the instrument pane as a reusable module (gauges, lamps, chart, plant) |
| `cpp/config.h`, `config.cpp` | the `Config` struct, JSONC loading, per-key get/set, comment-preserving save |
| `cpp/json.h`, `json.cpp` | a small JSONC reader that records value spans |
| `cpp/schema.h`, `schema.cpp` | reads `workbench.schema.json` into groups of settings |
| `cpp/preferences.h`, `preferences.cpp` | the Preferences form: search, rows, controls, save/reset |
| `rust/src/main.rs` | the Rust shell and its tests |
| `rust/src/panel.rs` | the instrument pane in Rust |
| `rust/src/json.rs`, `config.rs`, `schema.rs`, `preferences.rs` | the Rust JSONC reader, settings, schema reader and form |
| `python/workbench.py` | the Python shell and its tests |
| `python/panel.py` | the instrument pane in Python |
| `python/jsonc.py`, `config.py`, `schema.py`, `preferences.py` | the Python JSONC reader, settings, schema reader and form |
| `python/web.py` | the `raven_web` calls the Python binding does not generate |
| `workbench.jsonc` | the shipped configuration (comments included), shared by all three |
| `workbench.schema.json` | the JSON Schema that describes it and drives the form |
| `help/` | the user guide: `index.html`, `configuration.html`, `style.css` |
| `README.md` | what it does, screenshots, how to build and run |
| `DESIGN.md` | architecture, decisions, extension recipes |

The instrument panel exists in three files (`cpp/panel/`, `rust/src/panel.rs`,
`python/panel.py`) and also in Raven's `Examples/InstrumentPanel`. They are
ports of one module: a change to the plant, a paint callback or an accessor
belongs in all of them.

## Prerequisites

* **Raven**, not vendored:
  * C++: installed (`raven-dev`, or `cmake --install <build>` of a checkout) or
    a checkout with `-DRAVEN_SOURCE_DIR=/path/to/Raven`;
  * Rust: the `raven-gui` crate from a checkout next to this repository — the
    path is in `rust/Cargo.toml` (`../../Raven/bindings/rust`); change that one
    line if Raven is elsewhere;
  * Python: the binding (`bindings/python`) and the shared library
    (`build/libraven.so`), found through `PYTHONPATH` and `RAVEN_LIBRARY`.
* CMake 3.20+ and a C++17 compiler; `cargo`; `python3`.
* Optional: a Chromium-family browser (Chrome, Chromium, Edge, Brave) for the
  embedded user guide. Without one the pane says so and everything else works.

## Build and test

Build Raven's shared library once from the sibling checkout:

```bash
cmake -S ../Raven -B ../Raven/build -DRAVEN_BUILD_SHARED=ON
cmake --build ../Raven/build -j"$(nproc)"
```

Then, from this folder:

```bash
# C++
cmake -S cpp -B build && cmake --build build
ctest --test-dir build --output-on-failure      # or ./build/workbench --test

# Rust
cd rust && cargo run --release -- --test

# Python
cd python
PYTHONPATH=../../Raven/bindings/python RAVEN_LIBRARY=../../Raven/build/libraven.so \
    python3 workbench.py --test
```

Every check is in `--test`: the null backend, synthetic clicks and keys, and
assertions on state (pane index, labels, parsed file, clipboard, schema
counts). There is no test framework to install. All three print the same
summary and end with `PASS`.

## Rules

1. **Actions are the interface.** Menu items, toolbar buttons and context menu
   entries name an action (`Rwidget_set_action` / `OnClicked`); one handler per
   name. Reuse an existing action before writing a new one.
2. **A setting lives in five places**: the `Config` field and `config_get` /
   `config_set` of **each** implementation, `workbench.schema.json`,
   `workbench.jsonc`, and the live apply in the shell. Miss one and the
   languages disagree.
3. **Every feature gets a check** in the `--test` path, in every implementation.
4. **Write only to `/tmp` from tests.** The save tests point the config path at
   a temporary file; never touch the shipped `workbench.jsonc`.
5. **Do not commit build outputs.** `build/`, `rust/target/`, `__pycache__/` and
   captured `.ppm` files are temporary.
6. **Docs move with code.** A visible change means a new screenshot; a new
   setting means README + `help/` + schema.

## Pitfalls (each one cost a debugging session)

| Symptom | Cause |
|---|---|
| A widget in a `VBox` has zero height | `Rslot_set_fill(child, 0)` in a vertical box is a *height* share, not "auto". Leave the fill unset; use `halign` for width. |
| Right-clicking a pane shows no menu | Panels are pointer-transparent by default (`NO_HIT`). Call `Rwidget_set_hit_testable(area, R_TRUE)` on the area that owns the menu. |
| A context menu does not close after a click | Menus open on right-button **release**; inject press and release. They walk **up** the parent chain, so attach one menu per pane. |
| Saving settings deleted the comments | The writer patches value spans; a wholesale rewrite loses them. Every `config_save` re-parses its own result and refuses an invalid file. |
| The user guide pane stays blank | The embedded browser is headless and needs a few seconds (and a Chromium-family browser). Captures should wait ~9 s of frames. |
| `cannot start Raven (is a Vulkan driver installed?)` | A windowed run on a machine without a driver. Use `--test`. |
| Rust: `failed to load source for dependency raven-gui` | The path in `rust/Cargo.toml` expects Raven next to this repository. Point it at the checkout. |
| Rust: `cannot open shared object libraven.so.0` | `rust/.cargo/config.toml` sets `RAVEN_LIB_DIR` and the rpath for a sibling checkout; elsewhere, set both. |
| Python: `No module named 'raven'` / library not found | Set both `PYTHONPATH=<Raven>/bindings/python` and `RAVEN_LIBRARY=<Raven>/build/libraven.so`. |
| Python: the guide pane reports an error | `python/web.py` needs `raven_web` symbols in the library (they are part of `libraven`). An older library without them can still run everything else. |

## Using this repository as a template

This is already its own repository; the same steps start the next project from
it:

1. Copy the whole folder — it needs nothing from outside except Raven.
2. Rename in each build: `cpp/CMakeLists.txt` (`project`, executable,
   `WORKBENCH_VERSION`), `rust/Cargo.toml` (package, binary) and the program
   name in the three sources (window title, About text).
3. Replace `workbench.jsonc`, `workbench.schema.json` and `help/` with your own
   content; all three implementations read them from the repository root.
4. Keep the instrument panel in all three implementations if you want it;
   remove it, the `PANE_PANEL` page and its entries if you do not.
5. `--test` should still pass in all three; add checks for your own panes and
   settings.
6. For distribution, add CPack rules — the Raven tutorials' `09-packaging`
   step has a working DEB/TGZ recipe; Rust and Python ship the binary and the
   `help/` folder the same way.

## Where to change what

| Task | Files |
|---|---|
| Add a pane | each shell: `Pane`/pane constants, `set_pane`, the `Switcher`, the side bar, the View menu, `attach_context_menus`, the test |
| Add a setting | each `config` module, `workbench.schema.json`, `workbench.jsonc`, the live apply in each shell, the tests |
| Add a menu or context entry | each shell (`build_window` / `attach_context_menus`) plus an action binding |
| Add a help page | `help/`, link it from `index.html`; add a Help entry if it deserves one |
| Add an instrument | `cpp/panel/`, `rust/src/panel.rs`, `python/panel.py`: state, paint callback, accessor, setter |
| Change the look | theme values come from Raven; the panes draw with `Rwidget_get_color` |

## Definition of done

* Clean builds, **zero warnings** — C++, `cargo build`, and `py_compile`.
* `--test` passes in all three; new behaviour has a check in all three.
* README / `help/` / schema updated together.
* A visual change comes with a fresh `--capture` screenshot.
* Nothing built or temporary is left in the tree.
