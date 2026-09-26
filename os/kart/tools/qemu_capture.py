#!/usr/bin/env python3
"""Boots build/esp headless under QEMU/OVMF and records screenshots.

usage: qemu_capture.py OUT_DIR STEP...
  wait:SECONDS        let the game run
  down:KEY / up:KEY   press or release a key (QEMU qcode, e.g. up, ret, left)
  tap:KEY             press and release
  shot:NAME           save OUT_DIR/NAME.png
  hmp:COMMAND         print the output of a QEMU monitor command
The serial log is written to OUT_DIR/serial.log.
"""
import json
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import zlib
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
CODE = Path("/usr/share/OVMF/OVMF_CODE_4M.fd")
VARS = Path("/usr/share/OVMF/OVMF_VARS_4M.fd")


def png(ppm, out):
    data = Path(ppm).read_bytes()
    parts = data.split(b"\n", 3)
    width, height = map(int, parts[1].split())
    pixels = parts[3]
    rows = b"".join(b"\x00" + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    Path(out).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                          + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


class Qmp:
    def __init__(self, path):
        for _ in range(100):
            try:
                self.sock = socket.socket(socket.AF_UNIX)
                self.sock.connect(path)
                break
            except OSError:
                time.sleep(0.1)
        self.file = self.sock.makefile("rw")
        self.file.readline()
        self.command("qmp_capabilities")

    def command(self, name, **arguments):
        self.file.write(json.dumps({"execute": name, "arguments": arguments}) + "\n")
        self.file.flush()
        while True:
            reply = json.loads(self.file.readline())
            if "return" in reply or "error" in reply:
                return reply

    def key(self, key, down):
        self.command("input-send-event", events=[{"type": "key", "data": {
            "down": down, "key": {"type": "qcode", "data": key}}}])


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp())
    shutil.copy(VARS, work / "vars.fd")
    sock = str(work / "qmp.sock")
    qemu = subprocess.Popen([
        "qemu-system-x86_64", "-machine", "q35", "-m", "512", "-display", "none",
        "-drive", f"if=pflash,format=raw,readonly=on,file={CODE}",
        "-drive", f"if=pflash,format=raw,file={work / 'vars.fd'}",
        "-drive", f"format=raw,file=fat:rw:{HERE / 'build' / 'esp'}",
        "-serial", f"file:{out / 'serial.log'}", "-net", "none", "-no-reboot",
        "-qmp", f"unix:{sock},server,nowait"])
    try:
        qmp = Qmp(sock)
        run(qmp, out, work)
    except (BrokenPipeError, ConnectionResetError, json.JSONDecodeError):
        print("qemu exited")
    finally:
        try:
            qemu.wait(timeout=10)
        except subprocess.TimeoutExpired:
            qemu.kill()
        shutil.rmtree(work, ignore_errors=True)


def run(qmp, out, work):
    if True:
        for step in sys.argv[2:]:
            action, _, value = step.partition(":")
            if action == "wait":
                time.sleep(float(value))
            elif action in ("down", "up"):
                qmp.key(value, action == "down")
            elif action == "tap":
                qmp.key(value, True)
                time.sleep(0.15)
                qmp.key(value, False)
            elif action == "hmp":
                print(qmp.command("human-monitor-command", **{"command-line": value})["return"])
            elif action == "shot":
                ppm = work / "shot.ppm"
                qmp.command("screendump", filename=str(ppm))
                time.sleep(0.3)
                png(ppm, out / f"{value}.png")
                print("shot", out / f"{value}.png")
        qmp.command("quit")


if __name__ == "__main__":
    main()
