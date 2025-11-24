from __future__ import annotations

import itertools
import threading
import time
from pathlib import Path
from typing import List

try:
    import tkinter as tk
    from PIL import ImageTk
except ImportError as exc:  # pragma: no cover
    raise RuntimeError("Tkinter preview requires tkinter and Pillow ImageTk") from exc

from .anim_package import load_package


class PreviewPlayer:
    def __init__(self, package_path: Path) -> None:
        self.package = load_package(package_path)
        self.root = tk.Tk()
        self.root.title(f"AnimeBoot preview - {package_path.name}")
        self.label = tk.Label(self.root)
        self.label.pack()
        self.photos: List[ImageTk.PhotoImage] = [
            ImageTk.PhotoImage(frame.image) for frame in self.package.frames
        ]
        self._running = True

    def run(self) -> None:
        thread = threading.Thread(target=self._loop, daemon=True)
        thread.start()
        self.root.protocol("WM_DELETE_WINDOW", self._stop)
        self.root.mainloop()

    def _stop(self) -> None:
        self._running = False
        self.root.destroy()

    def _loop(self) -> None:
        frames = list(zip(self.photos, self.package.frames))
        if not frames:
            return
        for photo, frame in itertools.cycle(frames):
            if not self._running:
                break
            self.label.configure(image=photo)
            self.label.image = photo
            time.sleep(max(frame.duration_us / 1_000_000, 0.01))

