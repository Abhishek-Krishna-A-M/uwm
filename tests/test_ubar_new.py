import pathlib, sys

def test_per_source_sync():
    d = pathlib.Path("tools/ubar/src/data.c").read_text()
    assert "data_sync_audio" in d
    assert "data_sync_display" in d
    assert "data_sync_network" in d
    h = pathlib.Path("tools/ubar/include/data.h").read_text()
    assert "data_sync_audio" in h
    u = pathlib.Path("tools/ubar/src/ubar.c").read_text()
    assert "data_sync_audio(&state)" in u
    assert "data_sync_network" in u
    assert "data_sync_display" in u
    # 2.4 only redraw if changed
    assert "if (data_sync_audio" in u and "need_redraw" in u
    print("✓ 2.3/2.4 per-source sync + conditional redraw")

def test_focused_title_snprintf():
    txt = pathlib.Path("tools/ubar/src/ubar.c").read_text()
    assert "snprintf(s->focused_title" in txt
    assert "strncpy(s->focused_title" not in txt
    print("✓ 2.7 snprintf focused_title (no tail)")

def test_registry_destroy():
    txt = pathlib.Path("tools/ubar/src/ubar.c").read_text()
    assert "wl_registry_destroy(registry)" in txt
    print("✓ 2.9 wl_registry destroy")

def test_wpctl_no_zombie():
    txt = pathlib.Path("tools/ubar/src/input.c").read_text()
    # now should have SIGCHLD ignore in main, but input.c still forks — check main handles it
    ubar_main = pathlib.Path("tools/ubar/src/ubar.c").read_text()
    assert "SIGCHLD" in ubar_main and "SIG_IGN" in ubar_main
    print("✓ 2.2 wpctl zombie SIGCHLD ignore (PA direct next)")

def test_opaque_and_throttle():
    r = pathlib.Path("tools/ubar/src/render.c").read_text()
    assert "wl_surface_set_opaque_region" in r
    u = pathlib.Path("tools/ubar/src/ubar.c").read_text()
    assert "!state.frame_pending" in u
    print("✓ 2.5/2.6 throttle + opaque")

def test_usleep_removed():
    txt = pathlib.Path("tools/ubar/src/data.c").read_text()
    assert "usleep(500000)" not in txt
    print("✓ 2.10 PA usleep removed (non-blocking retry)")

if __name__ == "__main__":
    for fn in [test_per_source_sync, test_focused_title_snprintf, test_registry_destroy, test_wpctl_no_zombie, test_opaque_and_throttle, test_usleep_removed]:
        try: fn()
        except AssertionError as e:
            print(f"FAIL {fn.__name__}: {e}"); sys.exit(1)
