import sys
import struct
import os
import socket
import subprocess
from pathlib import Path
from PyQt5 import QtWidgets, QtGui, QtCore

HOST = '127.0.0.1'
PORT = 5555


class EmuClient:
    @staticmethod
    def send_bytes(b: bytes) -> bytes:
        """Send length-prefixed bytes to the emulator server and return response bytes."""
        with socket.create_connection((HOST, PORT), timeout=5) as s:
            s.sendall(struct.pack('<I', len(b)))
            s.sendall(b)
            header = s.recv(4)
            if len(header) < 4:
                raise RuntimeError('no response from emulator')
            out_len = struct.unpack('<I', header)[0]
            data = bytearray()
            while len(data) < out_len:
                chunk = s.recv(out_len - len(data))
                if not chunk:
                    break
                data.extend(chunk)
            return bytes(data)


def get_backend_path() -> Path:
    """Return the expected path to the backend executable.

    If running as a packaged/frozen app (PyInstaller/onefile), return the
    executable in the same directory as sys.executable. Otherwise return
    the dev build path under the repo's emulator/build directory.
    """
    exe_name = 'emu_server.exe' if os.name == 'nt' else 'emu_server'
    # packaged
    if getattr(sys, 'frozen', False):
        base_dir = Path(sys.executable).parent
        return base_dir / exe_name
    # dev mode
    base_dir = Path(__file__).resolve().parent
    repo_root = base_dir.parents[2]
    return repo_root / 'emulator' / 'build' / exe_name


def get_media_path(filename: str) -> Path:
    """Return path to a media file, working in both dev and packaged modes."""
    # packaged (PyInstaller) layout: media next to executable
    if getattr(sys, 'frozen', False):
        base_dir = Path(sys.executable).parent / 'media'
    else:
        # dev layout: repo_root/gui/media
        base_dir = Path(__file__).resolve().parents[2] / 'gui' / 'media'
    return base_dir / filename


class TitleBar(QtWidgets.QWidget):
    """Custom title bar for a frameless window with retro-styled controls.

    Emits: minimizeClicked, maximizeClicked, closeClicked
    """
    minimizeClicked = QtCore.pyqtSignal()
    maximizeClicked = QtCore.pyqtSignal()
    closeClicked = QtCore.pyqtSignal()

    def __init__(self, parent=None, title='8086 Emulator'):
        super().__init__(parent)
        self._drag_pos = None
        self.setFixedHeight(36)
        self.setSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Fixed)

        h = QtWidgets.QHBoxLayout(self)
        h.setContentsMargins(8, 2, 8, 2)
        h.setSpacing(6)

        self.title_label = QtWidgets.QLabel(title)
        self.title_label.setStyleSheet('color: #ffd36e; font-weight: bold; font-size:18px;')
        h.addWidget(self.title_label, stretch=1)

        # small control buttons
        btn_style = (
            "QToolButton { background: transparent; color: #ffd36e; border: 1px solid #163d2e; padding:4px; }"
            "QToolButton:hover { background: #08110b; border: 1px solid #8ff3a0; }"
        )

        self.min_btn = QtWidgets.QToolButton(self)
        self.min_btn.setText('\u2013')  # en dash as minimize
        self.min_btn.setStyleSheet(btn_style)
        self.min_btn.setFixedSize(30, 26)
        self.min_btn.setToolTip('Minimize')
        self.min_btn.clicked.connect(lambda: self.minimizeClicked.emit())
        h.addWidget(self.min_btn)

        self.max_btn = QtWidgets.QToolButton(self)
        self.max_btn.setText('\u25A2')  # square
        self.max_btn.setStyleSheet(btn_style)
        self.max_btn.setFixedSize(30, 26)
        self.max_btn.setToolTip('Maximize')
        self.max_btn.clicked.connect(lambda: self.maximizeClicked.emit())
        h.addWidget(self.max_btn)

        self.close_btn = QtWidgets.QToolButton(self)
        self.close_btn.setText('\u2715')  # multiplication X
        self.close_btn.setStyleSheet(btn_style + 'QToolButton:hover { background: #220000; color: #ff6666; }')
        self.close_btn.setFixedSize(30, 26)
        self.close_btn.setToolTip('Close')
        self.close_btn.clicked.connect(lambda: self.closeClicked.emit())
        h.addWidget(self.close_btn)

    def mousePressEvent(self, event):
        if event.button() == QtCore.Qt.LeftButton:
            self._drag_pos = event.globalPos()
            event.accept()

    def mouseMoveEvent(self, event):
        if self._drag_pos is not None:
            w = self.window()
            # do not move when maximized
            try:
                if w.isMaximized():
                    return
            except Exception:
                pass
            delta = event.globalPos() - self._drag_pos
            w.move(w.pos() + delta)
            self._drag_pos = event.globalPos()
            event.accept()

    def mouseReleaseEvent(self, event):
        self._drag_pos = None

    def mouseDoubleClickEvent(self, event):
        # double-click toggles maximize/restore
        self.maximizeClicked.emit()
        event.accept()

    def set_maximized(self, maximized: bool):
        # update the maximize button glyph to indicate restore when maximized
        try:
            if maximized:
                self.max_btn.setText('\u25A1')  # different square for restore
                self.max_btn.setToolTip('Restore')
            else:
                self.max_btn.setText('\u25A2')
                self.max_btn.setToolTip('Maximize')
        except Exception:
            pass


class MainWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        # use a frameless window so the chrome can match the app
        self.setWindowFlags(QtCore.Qt.Window | QtCore.Qt.FramelessWindowHint)
        self.setAttribute(QtCore.Qt.WA_TranslucentBackground, False)
        self.setWindowTitle('8086 Emulator GUI (PyQt)')
        self.resize(1000, 640)

        self.server_proc = None

        # central widget will include a custom TitleBar at the top
        central = QtWidgets.QWidget()
        self.setCentralWidget(central)

        self.asm_edit = QtWidgets.QPlainTextEdit()
        self.asm_edit.setPlainText("org 100h\nmov ah,2\nmov dl,'A'\nint 21h\nmov ax,4C00h\nint 21h")
        self.out_edit = QtWidgets.QPlainTextEdit()
        self.out_edit.setObjectName('out_edit')
        self.out_edit.setReadOnly(True)

        run_btn = QtWidgets.QPushButton('Assemble & Run')
        open_asm_btn = QtWidgets.QPushButton('Open .asm & Run')
        open_com_btn = QtWidgets.QPushButton('Open .com & Run')
        self.server_btn = QtWidgets.QPushButton('Start Server')

        run_btn.clicked.connect(self.assemble_and_run)
        open_asm_btn.clicked.connect(self.open_asm_and_run)
        open_com_btn.clicked.connect(self.open_com_and_run)
        self.server_btn.clicked.connect(self.toggle_server)

        left = QtWidgets.QVBoxLayout()
        left.addWidget(QtWidgets.QLabel('ASM editor:'))
        left.addWidget(self.asm_edit)
        btn_row = QtWidgets.QHBoxLayout()
        btn_row.addWidget(self.server_btn)
        btn_row.addWidget(run_btn)
        btn_row.addWidget(open_asm_btn)
        btn_row.addWidget(open_com_btn)
        left.addLayout(btn_row)

        right = QtWidgets.QVBoxLayout()
        right.addWidget(QtWidgets.QLabel('Emulator output:'))
        right.addWidget(self.out_edit)

        # assemble main content area
        main_content = QtWidgets.QWidget()
        main_layout = QtWidgets.QHBoxLayout(main_content)
        main_layout.setContentsMargins(8, 8, 8, 8)
        main_layout.addLayout(left, 3)
        main_layout.addLayout(right, 2)

        # top-level layout includes custom title bar + content
        top_layout = QtWidgets.QVBoxLayout(central)
        top_layout.setContentsMargins(0, 0, 0, 0)
        top_layout.setSpacing(0)

        self._title_bar = TitleBar(self, title='8086 Emulator')
        self._title_bar.minimizeClicked.connect(self._on_minimize)
        self._title_bar.maximizeClicked.connect(self._on_maximize_restore)
        self._title_bar.closeClicked.connect(self._on_close)
        top_layout.addWidget(self._title_bar)
        top_layout.addWidget(main_content)

        # apply retro theme
        self.apply_retro_style()

    def apply_retro_style(self):
        # richer retro styling: slate background, warm amber highlights, larger monospace text
        style = """
    QWidget { background-color: #071014; color: #cfeecf; font-family: 'Consolas', 'Courier New', monospace; font-size: 16px; }
    /* Unified editor + output styling: same monospace, size and retro palette */
    QPlainTextEdit { background-color: #031014; color: #e6f9de; border: 1px solid #163d2e; selection-background-color: #294b39; font-size: 18px; font-family: 'Consolas', 'Courier New', monospace; }
    QLabel { color: #ffd36e; font-weight: bold; font-size: 16px; }
    QPushButton { background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #12221a, stop:1 #08110b); color: #ffd36e; border: 1px solid #2f6b4f; padding: 10px; border-radius: 4px; font-size: 15px; }
    QPushButton:hover { border: 1px solid #8ff3a0; }
    QToolButton { color: #ffd36e; font-size: 14px; }
    QPlainTextEdit { line-height: 1.2; }
        """
        self.setStyleSheet(style)

        # faint background image using the startup image (low opacity)
        try:
            img_path = get_media_path('startup.png')
            if img_path.exists():
                bg = QtWidgets.QLabel(self)
                pix = QtGui.QPixmap(str(img_path))
                bg.setPixmap(pix)
                bg.setScaledContents(True)
                bg.setGeometry(self.rect())
                bg.setAttribute(QtCore.Qt.WA_TransparentForMouseEvents)
                bg.lower()
                op = QtWidgets.QGraphicsOpacityEffect(self)
                op.setOpacity(0.06)
                bg.setGraphicsEffect(op)
        except Exception:
            pass

    def resizeEvent(self, event):
        super().resizeEvent(event)
        # ensure title bar glyph matches maximized state
        try:
            self._title_bar.set_maximized(self.isMaximized())
        except Exception:
            pass
        # keep splash overlay sized to cover the whole window when present
        try:
            if hasattr(self, 'splash') and self.splash is not None and self.splash.isVisible():
                self.splash.setGeometry(self.rect())
                try:
                    # if a background image label exists, resize it to fill overlay
                    bg = getattr(self.splash, 'findChild')(QtWidgets.QLabel)
                    if bg is not None:
                        bg.setGeometry(self.splash.rect())
                except Exception:
                    pass
        except Exception:
            pass

    def update_output(self, text: str):
        # Replace non-printable characters with '.' so control codes (e.g. 0x10)
        # don't appear as strange glyphs in the GUI or terminal.
        if not isinstance(text, str):
            try:
                text = str(text)
            except Exception:
                text = ''
        safe = ''.join(ch if 32 <= ord(ch) < 127 else '.' for ch in text)
        self.out_edit.setPlainText(safe)

    def _on_minimize(self):
        self.showMinimized()

    def _on_maximize_restore(self):
        # toggle maximize/restore and update title bar button
        if self.isMaximized():
            self.showNormal()
            self._title_bar.set_maximized(False)
        else:
            self.showMaximized()
            self._title_bar.set_maximized(True)

    def _on_close(self):
        # stop server then close
        try:
            self.stop_server()
        except Exception:
            pass
        self.close()

    def locate_server_executable(self):
        # prefer the packaged backend when running frozen
        candidate = get_backend_path()
        if candidate.exists():
            return candidate

        # otherwise fall back to the build directory and find any emu_server binary
        repo_root = Path(__file__).resolve().parents[2]
        build_dir = repo_root / 'emulator' / 'build'
        if not build_dir.exists():
            return None
        for p in build_dir.iterdir():
            if p.is_file() and p.name.startswith('emu_server'):
                return p
        return None

    def start_server(self):
        if self.server_proc is not None:
            return True
        exe = self.locate_server_executable()
        if exe is None:
            self.update_output('Emulator server executable not found in emulator/build')
            return False
        try:
            proc = subprocess.Popen([str(exe)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.server_proc = proc
            self.server_btn.setText('Stop Server')
            self.update_output(f'Server started: {exe}')
            return True
        except Exception as ex:
            self.update_output('Failed to start server: ' + str(ex))
            return False

    def stop_server(self):
        if self.server_proc is None:
            return True
        try:
            self.server_proc.terminate()
            try:
                self.server_proc.wait(timeout=2)
            except Exception:
                self.server_proc.kill()
            self.update_output('Server stopped')
        except Exception as ex:
            self.update_output('Error stopping server: ' + str(ex))
        finally:
            self.server_proc = None
            self.server_btn.setText('Start Server')
        return True

    def toggle_server(self):
        if self.server_proc is None:
            self.start_server()
        else:
            self.stop_server()

    def assemble_and_run(self):
        asm = self.asm_edit.toPlainText()
        tmp = Path.cwd() / '.emu_tmp'
        tmp.mkdir(exist_ok=True)
        asm_file = tmp / 'prog.asm'
        com_file = tmp / 'prog.com'
        asm_file.write_text(asm, encoding='utf-8')
        try:
            # ensure server is running
            if self.server_proc is None:
                started = self.start_server()
                if not started:
                    return
            p = subprocess.run(['nasm', '-f', 'bin', '-o', str(com_file), str(asm_file)], capture_output=True, text=True, timeout=10)
            if p.returncode != 0:
                self.update_output('NASM failed:\n' + p.stdout + p.stderr)
                return
            data = com_file.read_bytes()
            out = EmuClient.send_bytes(data)
            self.update_output(out.decode('latin-1', errors='replace'))
        except FileNotFoundError:
            self.update_output('nasm not found on PATH')
        except Exception as ex:
            self.update_output('Error: ' + str(ex))

    def open_asm_and_run(self):
        path, _ = QtWidgets.QFileDialog.getOpenFileName(self, 'Open ASM', filter='ASM files (*.asm *.s)')
        if not path:
            return
        asm = Path(path).read_text(encoding='utf-8')
        self.asm_edit.setPlainText(asm)
        QtCore.QTimer.singleShot(10, self.assemble_and_run)

    def open_com_and_run(self):
        path, _ = QtWidgets.QFileDialog.getOpenFileName(self, 'Open COM/BIN', filter='COM files (*.com *.bin)')
        if not path:
            return
        data = Path(path).read_bytes()
        try:
            if self.server_proc is None:
                started = self.start_server()
                if not started:
                    return
            out = EmuClient.send_bytes(data)
            self.update_output(out.decode('latin-1', errors='replace'))
        except Exception as ex:
            self.update_output('Error: ' + str(ex))

    # --- Embedded splash overlay (appears as part of the main window) ---
    def create_splash_overlay(self):
        # If already created, just show it
        if hasattr(self, 'splash') and self.splash is not None:
            self.splash.setGeometry(self.rect())
            self.splash.show()
            self.splash.raise_()
            return

        img_path = get_media_path('startup.png')

        splash = QtWidgets.QWidget(self)
        splash.setObjectName('splash_overlay')
        splash.setAttribute(QtCore.Qt.WA_StyledBackground, True)
        # use a solid, opaque background so the underlying GUI is not visible
        splash.setAutoFillBackground(True)
        splash.setStyleSheet('QWidget#splash_overlay { background-color: #071014; }')
        splash.setGeometry(self.rect())

        v = QtWidgets.QVBoxLayout(splash)
        v.setContentsMargins(20, 20, 20, 20)
        v.setSpacing(12)
        v.setAlignment(QtCore.Qt.AlignCenter)

        # If the startup image exists, place a faint background copy that fills the overlay
        # so the splash uses the OG retro theme as its background.
        if img_path.exists():
            pix = QtGui.QPixmap(str(img_path))
            # background image (faint, fills overlay)
            bg_label = QtWidgets.QLabel(splash)
            bg_label.setPixmap(pix)
            bg_label.setScaledContents(True)
            bg_label.setGeometry(splash.rect())
            bg_label.setAttribute(QtCore.Qt.WA_TransparentForMouseEvents)
            op = QtWidgets.QGraphicsOpacityEffect(bg_label)
            op.setOpacity(0.06)
            bg_label.setGraphicsEffect(op)
            bg_label.lower()

            # centered prominent image above the faint background
            img_label = QtWidgets.QLabel()
            scaled = pix.scaled(900, 500, QtCore.Qt.KeepAspectRatio, QtCore.Qt.SmoothTransformation)
            img_label.setPixmap(scaled)
            img_label.setAlignment(QtCore.Qt.AlignCenter)
            v.addWidget(img_label, alignment=QtCore.Qt.AlignCenter)

        msg = QtWidgets.QLabel('Press Enter to enter the emulator')
        msg.setAlignment(QtCore.Qt.AlignCenter)
        msg.setStyleSheet('color: #ffd36e; font-size: 18px;')
        v.addWidget(msg)

        # focus and event handlers; prevent pointer events from reaching the widgets below
        splash.setFocusPolicy(QtCore.Qt.StrongFocus)
        splash.setAttribute(QtCore.Qt.WA_TransparentForMouseEvents, False)

        def on_mouse(event):
            self._fade_splash_out()

        def on_key(event):
            if event.key() in (QtCore.Qt.Key_Return, QtCore.Qt.Key_Enter):
                self._fade_splash_out()
            else:
                QtWidgets.QWidget.keyPressEvent(splash, event)

        splash.mousePressEvent = on_mouse
        splash.keyPressEvent = on_key

        # fade in animation
        splash.setWindowOpacity(0.0)
        anim = QtCore.QPropertyAnimation(splash, b'windowOpacity')
        anim.setDuration(600)
        anim.setStartValue(0.0)
        anim.setEndValue(1.0)
        anim.start()

        # store references
        self.splash = splash
        self._splash_anim = anim

    def _fade_splash_out(self):
        if not hasattr(self, 'splash') or self.splash is None:
            return
        try:
            fade = QtCore.QPropertyAnimation(self.splash, b'windowOpacity')
            fade.setDuration(400)
            fade.setStartValue(1.0)
            fade.setEndValue(0.0)
            fade.finished.connect(lambda: self.splash.hide())
            fade.start()
            # keep ref so it doesn't get GC'd immediately
            self._splash_fade = fade
        except Exception:
            try:
                self.splash.hide()
            except Exception:
                pass

    def show_splash(self):
        self.create_splash_overlay()
        try:
            self.splash.raise_()
            self.splash.setFocus()
            self.splash.show()
        except Exception:
            pass


class InteractiveSplash(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowFlags(QtCore.Qt.FramelessWindowHint | QtCore.Qt.Dialog)
        self.setModal(True)
        self.setAttribute(QtCore.Qt.WA_TranslucentBackground)
        v = QtWidgets.QVBoxLayout(self)
        v.setContentsMargins(20, 20, 20, 20)
        img_path = get_media_path('startup.png')
        pix = QtGui.QPixmap(str(img_path)) if img_path.exists() else None
        # image label
        self.img_label = QtWidgets.QLabel()
        if pix:
            scaled = pix.scaled(900, 500, QtCore.Qt.KeepAspectRatio, QtCore.Qt.SmoothTransformation)
            self.img_label.setPixmap(scaled)
            # size the dialog to fit the image plus margins
            self.resize(scaled.width() + 40, scaled.height() + 120)
        # ensure image is centered
        self.img_label.setAlignment(QtCore.Qt.AlignCenter)
        v.addWidget(self.img_label, alignment=QtCore.Qt.AlignCenter)

        # instruction label
        self.msg = QtWidgets.QLabel('Press Enter to enter the emulator')
        self.msg.setAlignment(QtCore.Qt.AlignCenter)
        self.msg.setStyleSheet('color: #ffd36e; font-size: 18px;')
        v.addWidget(self.msg)

        # animate fade in
        self._anim = QtCore.QPropertyAnimation(self, b'windowOpacity')
        self._anim.setDuration(600)
        self._anim.setStartValue(0.0)
        self._anim.setEndValue(1.0)
        self._anim.start()

        # ensure we receive key events
        self.setFocusPolicy(QtCore.Qt.StrongFocus)
        self.setFocus()
        try:
            self.grabKeyboard()
        except Exception:
            pass
        self._fade = None

        # center dialog on primary screen
        try:
            screen = QtWidgets.QApplication.primaryScreen().availableGeometry()
            qr = self.frameGeometry()
            center = screen.center()
            qr.moveCenter(center)
            self.move(qr.topLeft())
        except Exception:
            pass

    def keyPressEvent(self, event):
        if event.key() in (QtCore.Qt.Key_Return, QtCore.Qt.Key_Enter):
            self._fade = QtCore.QPropertyAnimation(self, b'windowOpacity')
            self._fade.setDuration(400)
            self._fade.setStartValue(1.0)
            self._fade.setEndValue(0.0)
            self._fade.finished.connect(self._on_fade_finished)
            self._fade.start()
        else:
            super().keyPressEvent(event)

    def mousePressEvent(self, event):
        self._fade = QtCore.QPropertyAnimation(self, b'windowOpacity')
        self._fade.setDuration(300)
        self._fade.setStartValue(1.0)
        self._fade.setEndValue(0.0)
        self._fade.finished.connect(self._on_fade_finished)
        self._fade.start()

    def _on_fade_finished(self):
        try:
            self.releaseKeyboard()
        except Exception:
            pass
        self.accept()


if __name__ == '__main__':
    app = QtWidgets.QApplication(sys.argv)
    w = MainWindow()
    # show the main window (not maximized) and display embedded splash overlay
    w.show()
    w.show_splash()
    # try to auto-start server on launch (after splash)
    try:
        w.start_server()
    except Exception:
        pass
    try:
        sys.exit(app.exec_())
    finally:
        try:
            w.stop_server()
        except Exception:
            pass
