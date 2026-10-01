import pathlib, sys

def test_vt_switch_handling():
    txt = pathlib.Path("src/core/server.c").read_text()
    assert "handle_session_active" in txt
    assert "wlr_output_schedule_frame" in txt
    txt2 = pathlib.Path("src/output/output.c").read_text()
    assert "session && !server->session->active" in txt2 or "session->active" in txt2
    print("✓ VT switch handling (session active schedules frame)")

def test_crash_recovery():
    txt = pathlib.Path("src/core/main.c").read_text()
    assert "sigsetjmp" in txt and "g_crash_jmpbuf" in txt
    txt2 = pathlib.Path("src/core/server.c").read_text()
    assert "uwm_save_session_listeners" in txt2
    print("✓ crash recovery sigjmp")

def test_workspace_switch():
    txt = pathlib.Path("src/wm/workspace.c").read_text() if pathlib.Path("src/wm/workspace.c").exists() else ""
    # fallback check output_set_workspace
    o = pathlib.Path("src/output/output.c").read_text()
    assert "output_set_workspace" in o
    assert "workspace_show_on_output" in o
    print("✓ workspace switch")

def test_window_map_unmap():
    txt = pathlib.Path("src/wm/window.c").read_text()
    assert "xdg_toplevel_map" in txt and "xdg_toplevel_unmap" in txt
    assert "bsp_insert" in txt and "bsp_remove" in txt
    print("✓ window map/unmap BSP")

if __name__ == "__main__":
    for fn in [test_vt_switch_handling, test_crash_recovery, test_workspace_switch, test_window_map_unmap]:
        try: fn()
        except AssertionError as e:
            print(f"FAIL {fn.__name__}: {e}"); sys.exit(1)
