<!--
SPDX-FileCopyrightText: © 2026 Isaac Freund
SPDX-License-Identifier: 0BSD
-->

# tinyrwm

A tiny [river](https://codeberg.org/river/river/) window manager, implemented in
various languages.

This project aims to help people quickly get started with their own river window
manager by following an example in their language of choice.

Contributions adding implementations in new languages are very welcome, as are
contributions simplifying or otherwise improving existing implementations!

All tinyrwm implementations should:

- Implement the same tiny feature set
- Be written in the style most idiomatic to their language
- Use the standard build/packaging approach for the given language ecosystem
- Be released under the Zero-Clause BSD license

Happy hacking!

## Tiny feature set

Keyboard bindings:

- `Super`+`Space`: Spawn the [foot](https://codeberg.org/dnkl/foot) terminal
- `Super`+`q`: Close the focused window
- `Super`+`n`: Cycle keyboard focus through windows
- `Super`+`Escape`: Exit the Wayland session

Pointer bindings:

- `Super`+`Left Click`: Interactive move
- `Super`+`Right Click`: Interactive resize

Additionally, clicking on a window must give the window keyboard focus and raise
it above all other windows.

This is not a formal specification, [tinyrwm.c](c/tinyrwm.c) is the canonical
implementation and its behavior should be matched where this specification is
unclear/insufficient.
