# DESIGN.md — Workbench

Workbench is a desktop application shell on top of **Raven**, a C GUI library.
It is written **three times** — C++, Rust and Python — over one configuration
file, one JSON Schema and one user guide. This document describes how it is put
together and how to extend it; it is written to stand on its own, because this
folder is meant to be forked into a separate project or used as the template of
the next one.

The only external dependency is Raven itself (the library and its `raven_web`
functions, both in the same installation). C++ uses it from an installed
package or a checkout; Rust takes the `raven-gui` crate from a checkout; Python
uses `bindings/python` and the shared library. Nothing is vendored.

---

## 1. Goals and non-goals

**Goals**

* a realistic shell: menu bar, toolbar, side bar, status bar, panes;
* configuration a user can edit by hand *and* through a form;
* documentation inside the application;
* every feature checked without a display or a GPU;
* a folder that builds on its own and can be copied to start a new project.

**Non-goals**

* a general-purpose framework — it is one application to copy;
* multi-window, networking, multi-user, persistence beyond the settings file;
* replacing Raven's own documentation;
* translations and packaging (the Raven tutorials show the CPack recipe).

---

## 2. Layout

```
Workbench/
├── AGENTS.md              build/test hand-off, rules, pitfalls, forking
├── DESIGN.md              this document
├── README.md              what it does, screenshots
├── workbench.jsonc        the settings, with comments
├── workbench.schema.json  JSON Schema: the same settings, described
├── help/                  index.html, configuration.html, style.css
├── cpp/
│   ├── CMakeLists.txt
│   ├── main.cpp           the shell
│   ├── config.h/.cpp      settings, JSONC load, get/set, comment-preserving save
│   ├── json.h/.cpp        JSONC reader with value spans
│   ├── schema.h/.cpp      the schema reader
│   ├── preferences.h/.cpp the Preferences form
│   └── panel/panel.h/.cpp the instrument pane (a reusable module)
├── rust/
│   ├── Cargo.toml         the raven-gui path dependency
│   ├── .cargo/config.toml RAVEN_LIB_DIR and the rpath
│   └── src/main.rs        the shell, in Rust
│       src/{json,config,schema,preferences,panel}.rs
└── python/
    ├── workbench.py       the shell, in Python
    ├── {jsonc,config,schema,preferences,panel}.py
    └── web.py             the raven_web calls the binding does not generate
```

There is no shared source: each language has its own shell, JSONC reader,
settings, schema reader, form and panel, because the point is to show the whole
application in each. The *contract* is shared — `workbench.jsonc`,
`workbench.schema.json`, `help/`, and the same actions, panes, menus and
settings.

The panel also exists in Raven's `Examples/InstrumentPanel`; it is the same
module and the copies are kept in step.

---

## 3. Architecture

```
                         main.cpp (shell)
     window · menu bar · toolbar · side bar · status bar
     actions: view.* plant.* edit.* help.* clipboard.* preferences.*
     frame callback · context menus · test
        │                 │                  │                 │
        ▼                 ▼                  ▼                 ▼
  preferences        schema            panel/panel.*      help/ (WebView)
  (form, search) ◄── (schema.cpp)      (instruments,       (local HTML,
        │                 ▲              plant)              headless browser)
        ▼                 │
     config ◄──── workbench.schema.json
  (get/set/save)
        │
        ▼
   json (JSONC reader with spans) ◄──── workbench.jsonc
```

| Module | Responsibility |
|---|---|
| `main.cpp` | everything the user sees around the panes, the action bindings, the frame loop, the tests |
| `panel/*` | the instrument pane as an opaque module: `panel_create/build/simulate/reset`, switches, alarm limit, trend window, accessors |
| `config/*` | `Config`, defaults, loading JSONC, `config_get`/`config_set`, `config_save` |
| `json/*` | a JSONC reader that records the byte span of every value (used for comment-preserving saves) |
| `schema/*` | reads the JSON Schema into groups of settings (title, description, type, choices, range, default) |
| `preferences/*` | the generated form: search, rows, controls, Save, Reset |

The same six modules exist in all three languages: `cpp/*`, `rust/src/*`,
`python/*.py`. The Rust and Python versions follow the C++ one file by file, so
a reader can compare them side by side; only the naming follows each language
(`Config` struct/class, `config.load` / `config_load`, and so on).

---

## 4. Data flow

1. **Startup** — `config_load(path)` fills `Config`; `schema_load` reads
   `workbench.schema.json`; the window is built with the theme, toolbar and
   first pane from `Config`; the Preferences page is built from the schema.
2. **Actions** — every menu, toolbar and context menu entry names an action;
   `Raction_bind` maps the name to one function. Checkable menu items toggle
   themselves before the handler runs, so the handler reads
   `Rmenu_item_is_checked`.
3. **Panes** — a `Switcher` holds four pages. `set_pane()` changes the page and
   synchronises the menu checks, the side bar highlight and the status bar.
4. **Settings** — the form edits `Config` through `config_get`/`config_set`,
   applies live settings through a callback, and writes the file only on Save.
5. **Guide** — a `raven_web` `WebView` is created on the first Help open and
   loads a local page with the theme in the URL.
6. **Frame** — only the instrument pane costs frames: it simulates, refreshes
   its labels and requests a redraw.

---

## 5. Decisions

**D1 — Panes are hidden, not rebuilt.** A `Switcher` keeps every page alive, so
the panel keeps its trend and switches. *Cost:* memory for all pages — the
browser starts lazily to avoid the worst of it.

**D2 — One action vocabulary.** Menu bar, toolbar and context menus fire the
same names; three entry points cannot disagree. *Cost:* names are strings, so a
typo fails silently — the tests click the real widgets.

**D3 — The instrument pane is a module.** `panel.h` is an opaque API and the
drawing lives in `panel.cpp`. *Why:* a pane is a component; the same file can be
reused or deleted as a unit. *Cost:* new controls need an accessor.

**D4 — The Preferences form is generated from the schema.** *Why:* form, docs
and file cannot drift, and a new key needs no widget code. The editor reads a
documented subset (`properties`, `title`, `description`, `type`, `enum`,
`minimum`, `maximum`, `default`); the rest stays valid for other editors.
*Cost:* the path-to-field mapping still needs `config_get`/`config_set`, and an
unknown key shows "not used by this build".

**D5 — Saving keeps the file as written.** The reader records value spans; the
writer replaces only those spans, inserts missing keys before the object's
brace, and re-parses before writing. *Why:* the configuration is the user's
text. *Cost:* inserted keys get canonical indentation, and a file with a syntax
error is refused rather than overwritten.

**D6 — Documentation is a real web view.** `raven_web` drives an installed
headless Chromium and hands Raven a texture: real HTML and real links inside a
pane. *Cost:* a browser must exist, pages arrive a frame late, captures need
seconds.

**D7 — Context menus belong to panes.** One menu per pane root; Raven walks up
the parent chain, so every child reaches it. Panels are `NO_HIT` by default, so
menu-bearing areas are made hit-testable. *Cost:* the area becomes a hit target
(children still win).

**D8 — Live where possible, next start otherwise.** Theme, alarm threshold,
trend window and switches apply at once; the toolbar and first pane are read
while building the window, so the UI says "next start".

**D9 — Paths are derived from the source location.** C++ compiles in
`WORKBENCH_DEFAULT_CONFIG`, `WORKBENCH_HELP_DIR` and `WORKBENCH_SCHEMA` from
`CMAKE_CURRENT_SOURCE_DIR`; Rust uses `env!("CARGO_MANIFEST_DIR")/../…`; Python
uses `__file__`'s parent. *Why:* a copied folder finds its own files from any
working directory. *Cost:* a packaged application must install those files and
adjust the locations (the Raven tutorials' packaging step shows how).

**D10 — Three implementations, one contract.** The settings file, the schema
and the guide are shared; the actions, panes, menus, labels and checks are the
same. *Why:* the Workbench doubles as the comparison and the template for each
binding, and a difference between them is a bug. *Cost:* a behaviour change is
written three times, and the ports can drift — every change lands in all three
`--test` paths.

---

## 6. Configuration and schema

**File format** — JSONC: `//` and block comments, trailing commas. Every key is
optional; a missing file, key or wrong type keeps the default. Lookup order:
`--config`, `WORKBENCH_CONFIG`, the compiled-in `workbench.jsonc`.

**Schema** — `workbench.schema.json` is JSON Schema 2020-12 with one object per
group (`release`, `ui`, `startup`, `simulation`). Each key carries `title`,
`description`, `type`, `default`, and `enum` or `minimum`/`maximum`. The editor
uses those to build the form and clamp values; `additionalProperties: false` and
`$schema` make the file useful to external validators too.

**Adding a setting** — the `Config` field and its `get`/`set` in each of the
three implementations, the schema entry, the shipped `workbench.jsonc`, and — if
it can apply without a restart — the live apply in each shell. The form picks it
up on the next start.

---

## 7. The instrument pane

`panel.h` exposes:

```
panel_create / panel_destroy        the object
panel_build                         the widget tree (the pane)
panel_simulate / panel_refresh      the plant and the labels
panel_reset                         a cold plant, empty trend
panel_running / panel_set_running   pause
panel_alarm                         interlock state
panel_pressure / panel_flow / panel_samples / panel_paints
panel_set_alarm_limit / panel_alarm_limit
panel_set_window / panel_window
panel_set_switch / panel_switch / panel_button
```

The four paint callbacks (gauge, digital meter, lamp, chart) draw with
`Rdraw_*` and read their colours from `Rwidget_get_color`, so they follow the
theme. Rust and Python expose the same functions (`Panel::build` /
`panel.build`, `set_alarm_limit` / `set_alarm_limit`, and so on). Adding an
instrument means: state in the panel, a paint callback, an accessor, and — if
configurable — a setter applied by the shell; in all three implementations.

---

## 8. Context menus

Attached with `Rwidget_set_context_menu` to the side bar and to each pane root.
Raven walks up from the widget under the pointer, which is why one menu per pane
is enough. Entries are actions; "Copy …" entries use `Rclipboard_set_text`.
Areas that show a menu call `Rwidget_set_hit_testable(..., R_TRUE)` because
panels are pointer-transparent by default.

---

## 9. Extension recipes

* **A new pane** — add a `Pane` value, build its page, add it to the `Switcher`,
  extend `set_pane` (page + menu checks + side bar + status), add a side bar
  button, a View item, a context menu entry, and a test that opens it.
* **A new menu entry** — name an action, bind it, add the item
  (`Rmenu_add_item` + `Rwidget_set_action`). Reuse an existing action where one
  fits.
* **A new setting** — see §6.
* **A new help page** — add HTML under `help/`, link it from `index.html`; add a
  Help menu entry if it deserves one.
* **A new instrument** — see §7.
* **A new theme** — Raven themes; use `Rtheme_apply` or a theme file, and read
  colours with `Rwidget_get_color` so custom painting follows.

---

## 10. Testing

`--test` starts Raven on the null backend and drives the real widgets with
`Rwindow_inject_*`. It checks: the schema loads (groups/settings counts), the
preferences filter and controls, saving (including comments and key insertion)
and reading back, pane switching from the menu, side bar, tool bar and context
menus, the plant's response, the clipboard. The C++, Rust and Python programs
run the same checks and print the same summary.

```bash
./build/workbench --test                      # C++
cd rust && cargo run --release -- --test      # Rust
cd python && python3 workbench.py --test      # Python (see AGENTS.md for the env)
```

The browser is excluded: the test points `Rweb_set_browser_path` at a missing
program, so the pane is tested and Chromium is not started. Writes go to `/tmp`.

Visual changes are reviewed from `--capture` images (PPM), not pixel assertions.

---

## 11. Limits and future work

* Toolbar visibility and the first pane apply at the next start.
* Schema validation is limited to the documented subset; `additionalProperties`
  is not enforced at load.
* One window; docking (`DockArea`) would replace the side bar and switcher.
* The guide is local HTML; no search or live manual.
* No settings for lists/arrays.
* Packaging is out of scope here — see the Raven tutorials' `09-packaging`.
* The three implementations can drift; the shared checks are the guard.
* Python has no generated `raven_web` binding, so `python/web.py` declares the
  calls it needs with ctypes; an older library without those symbols cannot
  show the guide.

---

## 12. File map

| File | Lines (approx.) | Contents |
|---|---|---|
| `cpp/main.cpp` | 1180 | shell, actions, panes, menus, context menus, frame, tests |
| `cpp/config.cpp` | 370 | load, get/set, comment-preserving save |
| `cpp/json.cpp` | 270 | JSONC reader with spans |
| `cpp/schema.cpp` | 110 | schema reader |
| `cpp/preferences.cpp` | 365 | form, controls, search, save/reset |
| `cpp/panel/panel.cpp` | 560 | instruments, plant, paint callbacks |
| `rust/src/main.rs` | 1000 | the shell, actions, panes, menus, context menus, frame, tests |
| `rust/src/config.rs` | 340 | load, get/set, comment-preserving save |
| `rust/src/json.rs` | 300 | JSONC reader with spans |
| `rust/src/schema.rs` | 110 | schema reader |
| `rust/src/preferences.rs` | 300 | form, controls, search, save/reset |
| `rust/src/panel.rs` | 700 | instruments, plant, paint callbacks |
| `python/workbench.py` | 900 | the shell and its tests |
| `python/config.py` | 300 | load, get/set, comment-preserving save |
| `python/jsonc.py` | 230 | JSONC reader with spans |
| `python/schema.py` | 100 | schema reader |
| `python/preferences.py` | 250 | form, controls, search, save/reset |
| `python/panel.py` | 500 | instruments, plant, paint callbacks |
| `python/web.py` | 80 | the raven_web declarations |
| `workbench.schema.json` | 100 | the settings schema |
| `workbench.jsonc` | 30 | the shipped configuration |
| `help/*.html` | 240 + CSS | the user guide |
