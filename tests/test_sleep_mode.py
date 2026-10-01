import pathlib, sys, re

def test_no_busy_poll_500():
    for path in ["src/core/main.c", "src/core/server.c", "src/output/output.c", "tools/ubar/src/data.c", "tools/ubar/src/ubar.c", "tools/ulaunch/src/ulaunch.c"]:
        if not pathlib.Path(path).exists():
            continue
        txt = pathlib.Path(path).read_text()
        # allow poll with -1 or with variable, but not hardcoded 500
        assert "poll(&pfd, 1, 500)" not in txt, f"{path} still has 500ms poll (should be -1 for sleep)"
        assert "poll(fds, 1, 500)" not in txt
    print("✓ no 500ms polls (sleep blocks)")

def test_no_usleep_blocking():
    for path in ["tools/ubar/src/data.c", "tools/ulaunch/src/input.c", "src/input/input.c"]:
        txt = pathlib.Path(path).read_text() if pathlib.Path(path).exists() else ""
        # audio_sink_info_cb must not have usleep(500000)
        assert "usleep(500000)" not in txt, f"{path} still blocks PA thread with usleep"
    print("✓ no usleep blocking PA thread")

def test_output_frame_bails_on_inactive():
    txt = pathlib.Path("src/output/output.c").read_text()
    assert "server->session && !server->session->active" in txt or "session->active" in txt
    assert "if (!output->wlr_output->enabled" in txt
    print("✓ output_frame bails when disabled/inactive (sleep saves power)")

def test_wl_display_poll_infinite():
    for path in ["tools/ubar/src/ubar.c", "tools/ulaunch/src/ulaunch.c"]:
        txt = pathlib.Path(path).read_text() if pathlib.Path(path).exists() else ""
        assert "poll(" in txt
        assert ", -1" in txt, f"{path} main loop should poll(-1) infinite"
    # uwm uses wl_display_run, not poll, but its monitors should also block
    txt = pathlib.Path("tools/ubar/src/data.c").read_text()
    assert "poll(&pfd, 1, -1)" in txt, "ubar monitors should poll -1"
    print("✓ main loops poll(-1) infinite")

def test_ubar_monitors_use_cond():
    txt = pathlib.Path("tools/ubar/src/data.c").read_text()
    assert "pthread_cond_wait" in txt, "audio monitor should use cond_wait not spin"
    assert "g_run_cond" in txt
    print("✓ ubar audio uses cond_wait (sleep)")

if __name__ == "__main__":
    for fn in [test_no_busy_poll_500, test_no_usleep_blocking, test_output_frame_bails_on_inactive, test_wl_display_poll_infinite, test_ubar_monitors_use_cond]:
        try: fn()
        except AssertionError as e:
            print(f"FAIL {fn.__name__}: {e}"); sys.exit(1)
