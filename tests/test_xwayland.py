import pathlib, sys

def test_server_h_has_xwayland():
    txt = pathlib.Path("include/core/server.h").read_text()
    assert "WLR_HAS_XWAYLAND" in txt
    assert "xwayland_enabled" in txt
    assert "xwayland_surface" in txt
    print("✓ server.h xwayland fields")

def test_window_h_enum():
    txt = pathlib.Path("include/wm/window.h").read_text()
    assert "uwm_toplevel_type" in txt
    assert "UWM_TOPLEVEL_XWAYLAND" in txt
    assert "xwayland_surface" in txt
    print("✓ window.h enum")

def test_main_handles_x():
    txt = pathlib.Path("src/core/main.c").read_text()
    assert '"s:xh"' in txt or '"s:x' in txt
    assert "enable_xwayland" in txt
    assert "xwayland_enabled" in txt
    print("✓ main.c -x handling")

def test_server_creates_immediate():
    txt = pathlib.Path("src/core/server.c").read_text()
    assert "wlr_xwayland_create" in txt
    # lazy=false => the X server is spawned eagerly, not on first client
    assert "wlr_xwayland_create(server->wl_display, server->compositor, false)" in txt
    assert 'setenv("DISPLAY"' in txt
    assert 'wlr_xwayland_set_seat' in txt
    print("✓ server.c eager XWayland")

def test_window_helpers():
    # the type-agnostic helpers live in toplevel.c; xwayland surface
    # lifecycle lives in window_xwayland.c
    txt = pathlib.Path("src/wm/toplevel.c").read_text()
    for fn in ["toplevel_surface","toplevel_geometry","toplevel_content_box",
               "toplevel_set_size","toplevel_set_activated","toplevel_set_fullscreen",
               "toplevel_app_id","toplevel_title","toplevel_send_close"]:
        assert fn in txt, f"missing helper {fn}"
    xt = pathlib.Path("src/wm/window_xwayland.c").read_text()
    assert "server_new_xwayland_surface" in xt
    assert "UWM_TOPLEVEL_XWAYLAND" in txt
    print("✓ toplevel.c helpers + xwayland surface")

if __name__ == "__main__":
    for fn in [test_server_h_has_xwayland, test_window_h_enum, test_main_handles_x, test_server_creates_immediate, test_window_helpers]:
        try: fn()
        except AssertionError as e:
            print(f"FAIL {fn.__name__}: {e}"); sys.exit(1)
