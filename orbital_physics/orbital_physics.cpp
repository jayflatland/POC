#include <limits>
#include <iostream>
#include <functional>
#include <vector>
#include "GL/freeglut.h"
#include "GL/gl.h"

#include "vector.hpp"
#include "particle.hpp"

using namespace std;

static const double TIME_PER_THINK  = 3600.0; //s
static const double VIEW_RANGE      = 4e11; //m


class App {
public:
    std::vector<Particle> particles_;

    void init() {
        // for(int i = 0; i < 300; i++) {
        //     particles_.push_back(Particle());
        // }

        Particle earth;
        earth.pos = Vector3(0.0, 150e9, 0.0); //m
        earth.vel = Vector3(3.0e4, 0.0, 0.0); //m/s
        earth.mass = 5.97219e24;//kg
        earth.radius = 6.378e6; //m
        earth.disp_radius = VIEW_RANGE / 100.0;
        earth.color = Vector3(0.5, 0.5, 1.0);
        particles_.push_back(earth);

        Particle mars;
        mars.pos = Vector3(0.0, 2.279392e11, 0.0); //m
        mars.vel = Vector3(2.4e4, 0.0, 0.0); //m/s
        mars.mass = 6.36e23;//kg
        mars.radius = 3.3895e6; //m
        mars.disp_radius = VIEW_RANGE / 100.0;
        mars.color = Vector3(1.0, 0.5, 0.5);
        particles_.push_back(mars);

        Particle sun;
        sun.pos.zero();
        sun.vel.zero();
        sun.mass = 1.9891e30;//kg
        sun.radius = 6.95700e8; //m
        sun.disp_radius = VIEW_RANGE / 100.0;
        sun.color = Vector3(1.0, 1.0, 0.0);
        particles_.push_back(sun);
    }

    void think(double dt) {
        for(auto && p : particles_) {
            p.preinteract();
        }

        for(auto && p1 : particles_) {
            for(auto && p2 : particles_) {
                if(&p1 == &p2) {continue;}
                p1.interact(p2);
                p2.interact(p1);
            }
        }

        for(auto && p : particles_) {
            p.postinteract();
        }

        for(auto && p : particles_) {
            p.think(dt);
        }

        double total_energy = 0.0;
        for(auto && p : particles_) {
            total_energy += p.vel.dot(p.vel);
        }
        // cout << "Total energy: " << total_energy << endl;
    }

    void draw() {
        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glLoadIdentity();
        glOrtho(-VIEW_RANGE, VIEW_RANGE, -VIEW_RANGE, VIEW_RANGE, -VIEW_RANGE, VIEW_RANGE);

        glBegin(GL_QUADS);
        for(auto && p : particles_) {
            glColor3f(p.color.x, p.color.y, p.color.z);
            glVertex3f(p.pos.x + p.disp_radius, p.pos.y + p.disp_radius, p.pos.z);
            glVertex3f(p.pos.x - p.disp_radius, p.pos.y + p.disp_radius, p.pos.z);
            glVertex3f(p.pos.x - p.disp_radius, p.pos.y - p.disp_radius, p.pos.z);
            glVertex3f(p.pos.x + p.disp_radius, p.pos.y - p.disp_radius, p.pos.z);
        }
        glEnd();
    }
};



App app;

void draw() {
    double dt = TIME_PER_THINK;
    app.think(dt);
    app.draw();
    glutSwapBuffers();
}

void handleKeys(unsigned char key, int x, int y) {
    if (key == 27) {
        exit(0);
    }
}

int main(int argc, char *argv[]) {

    glutInit(&argc, argv);

    app.init();

    glutInitDisplayMode(GLUT_SINGLE);
    glutInitWindowSize(1300, 1300);
    glutInitWindowPosition(700, 0);
    glutCreateWindow("Orbital Physics");
    glutDisplayFunc(draw);
    glutIdleFunc(draw);
    glutKeyboardFunc(handleKeys);
    glutMainLoop();
    return 0;
}
