PyQt GUI for the 8086 emulator server

Usage

1. Install dependencies (recommended in a virtualenv):

   pip install -r requirements.txt

2. Make sure `emu_server` is running on 127.0.0.1:5555 (the server binary in `emulator/build/emu_server.exe` on Windows)

3. Run the GUI:

   python emu_gui.py

Notes

- The GUI uses `nasm` (if present on PATH) to assemble `.asm` to `.com` when using "Assemble & Run".
- The client protocol is a 4-byte little-endian length followed by payload, and the server replies with 4-byte LE length + payload (same as the existing `test_client.py`).
- Output is decoded using ISO-8859-1 (latin-1) to preserve byte values.
