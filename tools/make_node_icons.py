#!/usr/bin/env python3
"""Builds content/icons/nodes from Groot's node icons (MIT, BehaviorTree/Groot bt_editor/resources/svg): each SVG
rendered at 64 px as a white shape on transparent, so the editor can tint it with a node's accent.

    python3 tools/make_node_icons.py path/to/Groot/bt_editor/resources/svg
"""
import sys
from pathlib import Path

from PyQt5.QtCore import Qt
from PyQt5.QtGui import QColor, QGuiApplication, QImage, QPainter
from PyQt5.QtSvg import QSvgRenderer

NAMES = ["sequence", "sequence_star", "sequence_reactive", "fallback", "fallback_reactive", "repeat", "retry",
         "not_equal", "timeout", "edit_list", "subtree"]
SIZE = 64

app = QGuiApplication(["make_node_icons", "-platform", "offscreen"])
source = Path(sys.argv[1])
out = Path(__file__).resolve().parent.parent / "content" / "icons" / "nodes"
out.mkdir(parents=True, exist_ok=True)
for name in NAMES:
    renderer = QSvgRenderer(str(source / f"{name}.svg"))
    image = QImage(SIZE, SIZE, QImage.Format_ARGB32)
    image.fill(Qt.transparent)
    painter = QPainter(image)
    renderer.render(painter)
    # Keep only the shape: white, with the drawing's alpha.
    painter.setCompositionMode(QPainter.CompositionMode_SourceIn)
    painter.fillRect(image.rect(), QColor(255, 255, 255))
    painter.end()
    image.save(str(out / f"{name}.png"))
    print(out / f"{name}.png")
