# vgui2

Valve VGUI2 (GoldSource) sources, built as the `vgui2client` shared library
for the CS client.

Why it is here
--------------

The engine probes for a VGUI2 support library at startup. When it cannot find
one it logs `Failed to load vgui_support library` and the Steam-style scoreboard
never comes up. The upstream `libvgui_support` shim links against Valve's
prebuilt VGUI2 library, which only exists as i386 ELF and Win32 binaries, so it
cannot be built for arm. Compiling the VGUI2 sources here gives us an arm build
with no external binary dependency.

Layout
------

- `public/` — VGUI2 headers (Surface, Panel, scheme, controls, UTL).
- `core/vgui_controls/`, `src/tier1`, `src/tier2`, `runtime/` — implementation.
- `CMakeLists.txt` — builds `vgui2client`; also compiles the two client glue
  files from `cl_dll/VGUI/` (`vgui2_client.cpp` bootstrap and
  `cstrikeclientscoreboard.cpp` panel) so Valve's UTL classes stay inside one
  self-consistent module instead of colliding with the client's miniutl copies.

Coupling to the client is plain C only, through `cl_dll/VGUI/cs_scoreboard_bridge.h`
(`csb_board_t` snapshots in, `CSB_ShowBoard` / `CSB_HideBoard` /
`CSB_IsAvailable` out), so the library never sees a game header.

Excluded sources (editor/perforce-only panels, files with platform deps nothing
references, or symbols the client already provides) are listed in
`CMakeLists.txt` as `VGUI2_EXCLUDE`.
