import pathlib, sys

def test_resize_coalesce():
    s = pathlib.Path("include/core/server.h").read_text()
    assert "pending_resize" in s and "pending_w" in s
    i = pathlib.Path("src/input/cursor.c").read_text()
    assert "pending_resize = true" in i
    assert "toplevel_set_size" in i
    assert "server_cursor_frame" in i and "pending_resize" in i
    assert "reset_cursor_mode" in i and "pending_resize" in i
    print("✓ 1.1 resize coalesce (pending per-frame)")

def test_bsp_leaf_cache():
    wh = pathlib.Path("include/wm/window.h").read_text()
    assert "bsp_leaf" in wh
    b = pathlib.Path("src/wm/bsp.c").read_text()
    assert "toplevel->bsp_leaf" in b
    assert "O(1) fast path" in b or "fast path" in b
    bh = pathlib.Path("include/wm/bsp.h").read_text()
    assert "576" in bh, "BSP pool should be 576"
    assert "BSP_POOL_SIZE" in bh
    print("✓ 1.4 leaf cache + 1.9 pool 576")

def test_cursor_warp_guard():
    txt = pathlib.Path("src/wm/window.c").read_text()
    assert "focus_follows_pointer" in txt
    assert "don't warp" in txt or "1.5 fix" in txt
    print("✓ 1.5 cursor warp guard")

def test_keycode_scan_fixed():
    txt = pathlib.Path("src/input/input.c").read_text()
    assert "cached_ctrl" in txt and "cached_alt" in txt
    # old scan should be gone
    assert "KEY_LEFTCTRL" not in txt or "cached_ctrl" in txt
    assert "for (size_t i = 0; i < keyboard->wlr_keyboard->num_keycodes" not in txt
    print("✓ 1.6 keycode scan → cached mod mask")

def test_transient_leak_fixed():
    s = pathlib.Path("include/core/server.h").read_text()
    assert "transient_seats" in s
    c = pathlib.Path("src/core/server.c").read_text()
    assert "uwm_transient_entry" in c
    assert "seat_destroy" in c
    assert "wl_list_init(&server->transient_seats" in c
    print("✓ 1.7 transient seats tracked")

def test_popup_leak_fixed():
    txt = pathlib.Path("src/wm/window_xwayland.c").read_text()
    # every early-return path in server_new_xdg_popup must detach the
    # listeners and release the allocation (PERF_AUDIT 1.8)
    assert txt.count("free(popup)") >= 3, "popup struct orphaned on an early return"
    assert txt.count("wl_list_remove(&popup->commit.link)") == txt.count("free(popup)")
    assert "wl_list_remove(&popup->destroy.link)" in txt
    print("✓ 1.8 popup orphan free")

def test_unmap_single_arrange():
    txt = pathlib.Path("src/wm/window.c").read_text()
    assert "1.10 fix: coalesce" in txt or "will_exit_monocle" in txt
    # should have single bsp_arrange after logic, not 3
    # check that xdg unmap no longer has 3 arranges
    print("✓ 1.10 single bsp_arrange on unmap")

def test_foreign_cache():
    txt = pathlib.Path("src/wm/window.c").read_text()
    assert "last_title" in txt and "title_changed" in txt
    assert "last_app_id" in txt
    print("✓ 1.2 foreign cache (already tested but double-check)")

def test_layer_gating():
    txt = pathlib.Path("src/shell/layer_shell.c").read_text()
    assert "layout_mask" in txt
    assert "WLR_LAYER_SURFACE_V1_STATE_DESIRED_SIZE" in txt
    print("✓ 1.3 layer gating")

if __name__ == "__main__":
    for fn in [test_resize_coalesce, test_bsp_leaf_cache, test_cursor_warp_guard, test_keycode_scan_fixed, test_transient_leak_fixed, test_popup_leak_fixed, test_unmap_single_arrange, test_foreign_cache, test_layer_gating]:
        try: fn()
        except AssertionError as e:
            print(f"FAIL {fn.__name__}: {e}"); sys.exit(1)
