"""Launch the build in developer mode (--pce-dev --windowed) and get into gameplay (loads the most recent save).
A developer instance keeps its own state in bin/data_dev and opens the developer bridge (see bridge.py).
Usage: python -I run_game.py [--no-load]"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME = os.path.join(ROOT, 'bin', 'ZeldaOnPC_release.exe')


def kill_existing():
    """Stop only dev instances (--pce-dev). A session started by the user is never touched."""
    out = subprocess.run(['powershell', '-NoProfile', '-Command',
                          "Get-CimInstance Win32_Process -Filter \"Name like 'ZeldaOnPC%'\" | "
                          "ForEach-Object { \"$($_.ProcessId)|$($_.CommandLine)\" }"], capture_output=True, text=True).stdout
    for line in out.splitlines():
        if '|' not in line:
            continue
        pid, cmd = line.split('|', 1)
        if '--pce-dev' in cmd:
            subprocess.run(['taskkill', '/PID', pid.strip(), '/F'], capture_output=True)
        else:
            raise SystemExit(f'user game session running (pid {pid.strip()}) - not starting a dev instance')
    time.sleep(1.5)


def launch():
    kill_existing()
    p = subprocess.Popen([GAME, '--pce-dev', '--windowed'], cwd=os.path.dirname(GAME))
    t0 = time.time()
    while True:
        try:
            b = bridge.Bridge()
            break
        except OSError:
            if time.time() - t0 > 60:
                raise
            time.sleep(0.5)
    # wait until the game polls input (title screen)
    while b.frame() < 400:
        time.sleep(0.25)
    return p, b


def load_save(b):
    """Title menu: Continue -> save list -> Start. Press A until the gameplay camera is running."""
    time.sleep(4)
    for _ in range(12):
        if b.cmd('camobj') != '00000000':
            break
        b.press('A', 6, 6)
        time.sleep(3)
    t0 = time.time()
    while b.cmd('camobj') == '00000000' and time.time() - t0 < 60:
        time.sleep(1)
    time.sleep(6)


if __name__ == '__main__':
    p, b = launch()
    if '--no-load' not in sys.argv:
        load_save(b)
    print('pid', p.pid, 'frame', b.frame())
