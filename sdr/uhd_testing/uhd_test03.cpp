#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <GL/glut.h>

static void Key(unsigned char key, int x, int y)
{
    switch (key)
    {
    case 27:
        exit(0);
    }
}

static void Draw(void)
{
    // glViewport(0, 0, 100, 100);
    // glMatrixMode(GL_PROJECTION);
    // glLoadIdentity();
    // // gluPerspective(60.0, 1.0, 0.1, 1000.0);
    // gluOrtho2D(0.0, 100.0, 100.0, 0.0);

    glViewport(0, 0, 780, 460);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, 0, 780, 460);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    glColor3f(1.0, 0.0, 1.0);
    // glBegin(GL_TRIANGLES);
    // glVertex3f(400 + -100.0, 400 + -100.0, 0.0);
    // glVertex3f(400 + 100.0, 400 + -100.0, 0.0);
    // glVertex3f(400 + 100.0, 400 + 100.0, 0.0);
    // glEnd();

    glBegin(GL_LINE_STRIP);
    glVertex2f(0, -1.0f);
    glVertex2f(0, 1.0f);
    glEnd();
    glBegin(GL_LINE_STRIP);
    glVertex2f(-1.0f, 0);
    glVertex2f(1.0f, 0);
    glEnd();

    // glBegin(GL_LINE_STRIP);
    // glVertex2f(0, 230.0f);
    // glVertex2f(780, 230.0f);
    // glEnd();
    // glBegin(GL_LINE_STRIP);
    // glVertex2f(390.0f, 0);
    // glVertex2f(390.0f, 460.0f);
    // glEnd();

    glutSwapBuffers();
}

int main(int argc, char **argv)
{
    GLenum type;

    glutInit(&argc, argv);

    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
    glutInitWindowSize(3400, 2000);
    glutCreateWindow("ABGR extension");

    glutKeyboardFunc(Key);
    glutDisplayFunc(Draw);
    glutMainLoop();
    return 0; /* ANSI C requires main to return int. */
}
