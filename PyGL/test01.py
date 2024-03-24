#! /usr/bin/env python

from numpy import *
from numpy.random import *
import string, sys
from OpenGL.GL import *
from OpenGL.GLUT import *

MY_LIST = 1
NUMDOTS = 500
NUMDOTS2 = 600
x = random(NUMDOTS) * 2 - 1
y = random(NUMDOTS) * 2 - 1
MAX_AGE = 13
age = randint(0, MAX_AGE, (NUMDOTS,))
move_length = 0.005  # 1.0 = screen width
angle = 0  # in radians
delta_angle = 0.2  # in radians
move_x = move_length * cos(angle)
move_y = move_length * sin(angle)
halted = 0


def display(*args):
    glClearColor(0.0, 0.0, 0.0, 0.0)
    glClear(GL_COLOR_BUFFER_BIT)

    glColor3f(1, 1, 1)
    glBegin(GL_LINES)
    glVertex3f(-1,-1,-1)
    glVertex3f( 1,-1,-1)
    glEnd()

    glFlush()
    glutSwapBuffers()


def halt():
    pass


def keyboard(*args):
    sys.exit()

def setup_viewport():
    glMatrixMode(GL_PROJECTION)
    glLoadIdentity()
    glOrtho(0.0, 1.0, 0.0, 1.0, 0.0, 1.0)


def reshape(w, h):
    glViewport(0, 0, w, h)
    setup_viewport()


def main():
    glutInit(sys.argv)
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB)
    glutInitWindowSize(1200, 800)
    glutCreateWindow("Dots")
    setup_viewport()
    glutReshapeFunc(reshape)
    glutDisplayFunc(display)
    glutIdleFunc(display)
    # glutMouseFunc(mouse)
    glutKeyboardFunc(keyboard)
    glutMainLoop()


print("Use the mouse buttons to control some of the dots.")
print("Hit any key to quit.")
main()