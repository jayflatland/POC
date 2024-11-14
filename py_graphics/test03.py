#!/usr/bin/env python

#pip install graphics.py
from graphics import *

win = GraphWin(width = 1600, height = 1200)
win.setCoords(0, 0, 10, 10)
mySquare = Rectangle(Point(1, 1), Point(9, 9))
mySquare.draw(win)
win.getMouse()
win.close()