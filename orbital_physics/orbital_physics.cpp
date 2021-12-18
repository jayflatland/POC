
#include <limits>
#include <iostream>
#include "GL/freeglut.h"
#include "GL/gl.h"

#include <thread>
#include <chrono>

using namespace std::chrono;
using namespace std;



void draw() {
    glClearColor(0.3, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glLoadIdentity();
    glOrtho(0.0, 100.0, 0.0, 100.0, -1.0, 1.0);

    glBegin(GL_QUADS);
    glColor3f(1, 0, 1);
    glVertex3f(0, 0, 0);
    glVertex3f(1, 0, 0);
    glVertex3f(1, 1, 0);
    glVertex3f(0, 1, 0);
    glEnd();

    glutSwapBuffers();
}

void handleKeys(unsigned char key, int x, int y) {
    if (key == 27){
        exit(0);
    }
}

int main(int argc, char *argv[]) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_SINGLE);
    glutInitWindowSize(500, 500);
    glutInitWindowPosition(100, 100);
    glutCreateWindow("Orbital Physics");
    glutDisplayFunc(draw);
    glutIdleFunc(draw);
    glutKeyboardFunc(handleKeys);

    glutMainLoop();
    return 0;
}
