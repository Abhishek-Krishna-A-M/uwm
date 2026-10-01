import pathlib, sys

def grep(path, needle):
    return needle in pathlib.Path(path).read_text()

def test_layer_arrange_on_layout_change():
    txt = pathlib.Path("src/output/output.c").read_text()
    assert "handle_output_layout_change" in txt
    # must call layer_surface_arrange inside that handler
    assert txt.count("layer_surface_arrange(output)") >= 2, "layer arrange should be in layout_change and request_state"
    # check F4 fix present
    assert "F4 fix" in txt or "re-arrange layer surfaces" in txt
    print("✓ HDMI F4 layer arrange")

def test_lock_double_offset_fix():
    s = pathlib.Path("src/shell/session_lock.c").read_text()
    assert "wlr_scene_node_set_position(&scene_tree->node, 0, 0)" in s, "F2 fix 0,0 not found"
    o = pathlib.Path("src/output/output.c").read_text()
    # in output.c handle_output_layout_change the lock reposition should be 0,0
    assert "wlr_scene_node_set_position(&tree->node, 0, 0)" in o
    print("✓ HDMI F2 lock offset fixed")

def test_get_output_size_fallback():
    txt = pathlib.Path("src/wm/bsp_arrange.c").read_text()
    assert "first->lx + first->usable_area.x" in txt, "F3 fix missing lx"
    assert "first->ly + first->usable_area.y" in txt
    print("✓ HDMI F3 fallback lx/ly")

def test_output_destroy_uaf():
    txt = pathlib.Path("src/output/output.c").read_text()
    assert "output->lock_surface" in txt and "wl_list_remove(&output->lock_surface_destroy.link)" in txt, "F5 UAF fix missing"
    print("✓ HDMI F5 UAF fix")

def test_initial_commit_hardening():
    txt = pathlib.Path("src/output/output.c").read_text()
    assert "initial commit failed" in txt or "fallback commit" in txt, "F1 hardening missing"
    print("✓ HDMI F1 commit hardening")

if __name__ == "__main__":
    for fn in [test_layer_arrange_on_layout_change, test_lock_double_offset_fix, test_get_output_size_fallback, test_output_destroy_uaf, test_initial_commit_hardening]:
        try:
            fn()
        except AssertionError as e:
            print(f"FAIL {fn.__name__}: {e}")
            sys.exit(1)
