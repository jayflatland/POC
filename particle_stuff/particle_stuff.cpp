#include <limits>
#include <iostream>
#include <functional>
#include <vector>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_real_distribution.hpp>

#include "GL/freeglut.h"
#include "GL/gl.h"

#include "vector.hpp"
#include "particle.hpp"

using namespace std;

static const double TIME_PER_THINK  = 600.0; //s
static const double VIEW_RANGE      = 400e9; //m
// static const double VIEW_RANGE      = 1e9; //m


class App {
public:
    std::vector<Particle> particles_;

    void init() {
        // Particle sun;
        // sun.pos.zero();
        // sun.vel.zero();
        // sun.mass = 1.9891e30;//kg
        // sun.radius = 6.95700e8; //m
        // sun.disp_radius = VIEW_RANGE / 100.0;
        // sun.color = Vector3(1.0, 1.0, 0.0);
        // particles_.push_back(sun);

        // Particle earth;
        // earth.pos = Vector3(0.0, 150e9, 0.0); //m
        // earth.vel = Vector3(3.0e4, 0.0, 0.0); //m/s
        // earth.mass = 5.97219e24;//kg
        // earth.radius = 6.378e6; //m
        // earth.disp_radius = VIEW_RANGE / 100.0;
        // earth.color = Vector3(0.5, 0.5, 1.0);
        // particles_.push_back(earth);

        boost::random::mt19937 randgen_;
        boost::random::uniform_real_distribution<> pos_dist(-150e9, 150e9);
        boost::random::uniform_real_distribution<> vel_dist(-3e4, 3e4);
        for(int i = 0; i < 300; i++) {
            particles_.push_back(Particle());
            auto &p = particles_.back();
            p.pos = Vector3(pos_dist(randgen_), pos_dist(randgen_), 0.0); //m
            // p.vel = Vector3(vel_dist(randgen_), vel_dist(randgen_), 0.0); //m/s
            p.vel.zero();
            // p.mass = 5.97219e24;//kg (earth)
            p.mass = 1.9891e30;//kg (sun)
            p.radius = 6.378e6; //m
            p.disp_radius = VIEW_RANGE / 300.0;
            // p.color = Vector3(0.5, 0.5, 1.0);
        }

        // Particle moon;
        // moon.pos = earth.pos + Vector3(0.0, 384e6, 0.0); //m
        // moon.vel = earth.vel + Vector3(1.022e3, 0.0, 0.0); //m/s
        // moon.mass = 7.342e22;//kg
        // moon.radius = 1.7374e6; //m
        // moon.disp_radius = VIEW_RANGE / 100.0;
        // moon.color = Vector3(0.7, 0.7, 0.7);
        // particles_.push_back(moon);

        // Particle mars;
        // mars.pos = Vector3(0.0, 2.279392e11, 0.0); //m
        // mars.vel = Vector3(2.4e4, 0.0, 0.0); //m/s
        // mars.mass = 6.36e23;//kg
        // mars.radius = 3.3895e6; //m
        // mars.disp_radius = VIEW_RANGE / 100.0;
        // mars.color = Vector3(1.0, 0.5, 0.5);
        // particles_.push_back(mars);

        // Particle jwst;
        // jwst.pos = Vector3(0.0, 150e9 + 1.5e9, 0.0); //m
        // jwst.vel = Vector3(3.0e4 * (150+1.5)/150, 0.0, 0.0); //m/s
        // jwst.mass = 6200;//kg
        // jwst.radius = 22.0; //m
        // jwst.disp_radius = VIEW_RANGE / 100.0;
        // jwst.color = Vector3(0.5, 0.5, 0.0);
        // particles_.push_back(jwst);
    }

    void think(double t, double dt) {
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
            p.elapse_time(dt);
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
        
        double s = 1.0;

        // s = 100.0;
        // auto && p = particles_[1];
        // glScalef(s, s, s);
        // glTranslatef(-p.pos.x, -p.pos.y, 0.0);

        glBegin(GL_QUADS);
        for(auto && p : particles_) {
            glColor3f(p.color.x, p.color.y, p.color.z);
            glVertex3f(p.pos.x + p.disp_radius / s, p.pos.y + p.disp_radius / s, p.pos.z);
            glVertex3f(p.pos.x - p.disp_radius / s, p.pos.y + p.disp_radius / s, p.pos.z);
            glVertex3f(p.pos.x - p.disp_radius / s, p.pos.y - p.disp_radius / s, p.pos.z);
            glVertex3f(p.pos.x + p.disp_radius / s, p.pos.y - p.disp_radius / s, p.pos.z);
        }
        glEnd();

        // for(auto && p : particles_) {
        //     glBegin(GL_LINE_STRIP);
        //     glColor3f(p.color.x, p.color.y, p.color.z);
        //     for(auto && pos: p.pos_history) {
        //         glVertex3f(pos.x, pos.y, pos.z);
        //     }
        //     glEnd();
        // }
    }
};



App app;
double t = 0.0;

void draw() {
    double dt = TIME_PER_THINK;
    app.think(t, dt);
    app.draw();
    glutSwapBuffers();

    t += dt;
    // cout << "day=" << t/86400.0 << endl;
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
