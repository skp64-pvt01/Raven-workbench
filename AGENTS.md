# AGENTS.md — Workbench

Workbench is a small desktop application built on **Raven** (a C GUI library):
a menu bar, a toolbar, a side bar, a status bar, four panes (Application,
Instrument panel, User guide, Preferences), a JSONC configuration file with a
JSON Schema, an embedded documentation browser and right-click menus.

It is also meant to be **copied**: this folder is self-contained apart from
Raven itself, so it can become its own project or the template of the next one.

---

## What is here

| Path | What it is |
|---|---|
| `cpp/main.cpp` | the shell: window, menus, toolbar, side bar, panes, status bar, actions, frame callback, context menus, tests |
| `cpp/panel/panel.h`, `panel.cpp` | the instrument pane as a reusable module (gauges, lamps, chart, simulated plant) |
| `cpp/config.h`, `config.cpp` | the `Config` struct, JSONC loading, per-key get/set, comment-preserving save |
| `cpp/json.h`, `json.cpp` | a small JSONC reader that records value spans |
| `cpp/schema.h`, `schema.cpp` | reads `workbench.schema.json` into groups of settings |
| `cpp/preferences.h`, `preferences.cpp` | the Preferences form: search, rows, controls, save/reset |
| `workbench.jsonc` | the shipped configuration (comments included) |
| `workbench.schema.json` | the JSON Schema that describes it and drives the form |
| `help/` | the user guide: `index.html`, `configuration.html`, `style.css` |
| `README.md` | what it does, screenshots, how to build and run |
| `DESIGN.md` | architecture, decisions, extension recipes |

`cpp/panel/` is a copy of the instrument panel from the Raven examples, kept
here so the folder stands alone. If the panel changes, change both copies (or
treat this one as canonical once this folder is its own project).

## Prerequisites

* **Raven**, one of two ways (it is not vendored):
  * installed: the `raven-dev` package, or `cmake --install <build>` of a
    checkout — `find_package(raven)` provides `raven::raven`;
  * from a checkout, without installing:
    `cmake -S cpp -B build -DRAVEN_SOURCE_DIR=/path/to/Raven` — CMake adds it
    with `add_subdirectory`.
  A static or shared library is fine for a C++ program; `raven::raven` also
  contains the `raven_web` functions used by the guide.
* CMake 3.20 or newer, a C++17 compiler.
* Optional: a Chromium-family browser (Chrome, Chromium, Edge, Brave) for the
  embedded user guide. Without one the pane says so and everything else works.

## Build and test

From this folder:

```bash
cmake -S cpp -B build
cmake --build build

./build/workbench                     # the application
./build/workbench --test              # no window, no GPU: checks only
./build/workbench --capture shot.ppm  # one frame, saved as a PPM
./build/workbench --capture menu.ppm --menu --pane application
./build/workbench --config mine.jsonc # another settings file
```

If Raven lives outside the default prefix, configure with
`-DCMAKE_PREFIX_PATH=/path/to/prefix`.

Every check is in `--test`: the null backend, synthetic clicks and keys, and
assertions on state (pane index, labels, parsed file, clipboard, schema
counts). There is no test framework to install.

## Rules

1. **Actions are the interface.** Menu items, toolbar buttons and context menu
   entries name an action (`Rwidget_set_action`); one `Raction_bind` handles it.
   Reuse an existing action before writing a new one.
2. **A setting lives in four places**: the `Config` field, `config_get` /
   `config_set`, `workbench.schema.json`, and `workbench.jsonc`.
3. **Every feature gets a check** in the `--test` path.
4. **Write only to `/tmp` from tests.** The save tests point `g.config.path` at a
   temporary file; never touch the shipped `workbench.jsonc`.
5. **Do not commit build outputs.** `build/` and captured `.ppm` files are
   temporary.
6. **Docs move with code.** A visible change means a new screenshot; a new
   setting means README + `help/` + schema.

## Pitfalls (each one cost a debugging session)

| Symptom | Cause |
|---|---|
| A widget in a `VBox` has zero height | `Rslot_set_fill(child, 0)` in a vertical box is a *height* share, not "auto". Leave the fill unset; use `halign` for width. |
| Right-clicking a pane shows no menu | Panels are pointer-transparent by default (`NO_HIT`). Call `Rwidget_set_hit_testable(area, R_TRUE)` on the area that owns the menu. |
| A context menu does not close after a click | Menus open on right-button **release**; inject press and release. They walk **up** the parent chain, so attach one menu per pane. |
| Saving settings deleted the comments | The writer patches value spans; a wholesale rewrite loses them. `config_save` re-parses its own result before writing and refuses an invalid file. |
| The user guide pane stays blank | The embedded browser is headless and needs a few seconds (and a Chromium-family browser). Captures should wait ~9 s of frames. |
| `cannot start Raven (is a Vulkan driver installed?)` | A windowed run on a machine without a driver. Use `--test`. |

## Using this repository as a template

This is already its own repository; the same steps start the next project from
it:

1. Copy the whole folder — it needs nothing from outside except Raven.
2. Rename in `cpp/CMakeLists.txt`: `project(workbench ...)`, the executable, and
   `WORKBENCH_VERSION`; rename the program in `main.cpp` (`WORKBENCH_*` defaults,
   the window title, the About text) if you want a different identity.
3. Replace `workbench.jsonc`, `workbench.schema.json` and `help/` with your own
   content; the application reads them by path compiled at configure time.
4. Keep `cpp/panel/` if you want the instrument pane; delete it, the
   `PANE_PANEL` page and its entries if you do not.
5. `--test` should still pass; add checks for your own panes and settings.
6. For distribution, add CPack rules — the Raven tutorials' `09-packaging`
   step has a working DEB/TGZ recipe for this kind of CMake project.

## Where to change what

| Task | Files |
|---|---|
| Add a pane | `main.cpp`: `Pane`, `set_pane`, the `Switcher`, the side bar, the View menu, `attach_context_menus`, the test |
| Add a setting | `config.h`, `config.cpp`, `workbench.schema.json`, `workbench.jsonc`, the live apply in `main.cpp`, the test |
| Add a menu or context entry | `main.cpp`: `build_window` / `attach_context_menus`, plus an action binding |
| Add a help page | `help/`, link it from `index.html` |
| Add an instrument | `cpp/panel/panel.h` and `panel.cpp`: state, paint callback, accessor, `panel_set_*` |
| Change the look | theme values come from Raven; the panes draw with `Rwidget_get_color` |

## Definition of done

* Clean build, **zero warnings**.
* `--test` passes; new behaviour has a check.
* README / `help/` / schema updated together.
* A visual change comes with a fresh `--capture` screenshot.
* Nothing built or temporary is left in the tree.
