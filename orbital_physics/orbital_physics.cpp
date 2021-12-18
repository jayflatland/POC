#include <limits>
#include <iostream>
#include <functional>
#include <vector>
#include "GL/freeglut.h"
#include "GL/gl.h"

#include "vector.hpp"

using namespace std;

#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_real_distribution.hpp>

boost::random::mt19937 randgen_;

static const double G               = 100.0;    //gravitational constant
static const int    TOTAL_PARTICLES = 1;
static const double TIME_PER_THINK  = 1e-5;//1.0 / 60.0;
static const double INIT_VEL_SCALE  = 1e3;


class Particle {
public:
    Vector pos;
    Vector vel;
    Vector force;
    Vector color;
    double mass;
    Particle() {
        boost::random::uniform_real_distribution<> pos_dist(-100.0, 100.0);
        boost::random::uniform_real_distribution<> vel_dist(-INIT_VEL_SCALE, INIT_VEL_SCALE);
        boost::random::uniform_real_distribution<> color_dist(0.5, 1.0);

        pos = Vector(pos_dist(randgen_), pos_dist(randgen_), 0.0);
        vel = Vector(vel_dist(randgen_), vel_dist(randgen_), 0.0);
        color = Vector(color_dist(randgen_), color_dist(randgen_), color_dist(randgen_));
        mass = 1.0;
    }

    void think(double dt) {
        vel.x += force.x * dt / mass;
        vel.y += force.y * dt / mass;
        vel.z += force.z * dt / mass;

        pos.x += vel.x * dt;
        pos.y += vel.y * dt;
        pos.z += vel.z * dt;

        if(pos.x >  100.0) {pos.x =  200.0 - pos.x; vel.x = -vel.x;}
        if(pos.y >  100.0) {pos.y =  200.0 - pos.y; vel.y = -vel.y;}
        if(pos.z >  100.0) {pos.z =  200.0 - pos.z; vel.z = -vel.z;}
        if(pos.x < -100.0) {pos.x = -200.0 - pos.x; vel.x = -vel.x;}
        if(pos.y < -100.0) {pos.y = -200.0 - pos.y; vel.y = -vel.y;}
        if(pos.z < -100.0) {pos.z = -200.0 - pos.z; vel.z = -vel.z;}
    }

    void preinteract() {
        force.zero();
    }

    void interact(const Particle &other) {
        double r = pos.dist(other.pos);
        double F = G * mass * other.mass / (r * r);
        Vector dirto = other.pos - pos;
        dirto.normalize();
        force += dirto * F;
    }

    void postinteract() {

    }
};

class App {
public:
    std::vector<Particle> particles_;

    void init() {
        for(int i = 0; i < TOTAL_PARTICLES; i++) {
            particles_.push_back(Particle());
        }

        Particle earth;
        earth.pos.zero();
        earth.vel.zero();
        earth.mass = 1e6;
        earth.color = Vector(0.5, 0.5, 1.0);
        particles_.push_back(earth);
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
        glClearColor(0.3, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glLoadIdentity();
        glOrtho(-100.0, 100.0, -100.0, 100.0, -100.0, 100.0);

        glBegin(GL_QUADS);
        for(auto && p : particles_) {
            glColor3f(p.color.x, p.color.y, p.color.z);
            glVertex3f(p.pos.x + 1.0, p.pos.y + 1.0, p.pos.z);
            glVertex3f(p.pos.x - 1.0, p.pos.y + 1.0, p.pos.z);
            glVertex3f(p.pos.x - 1.0, p.pos.y - 1.0, p.pos.z);
            glVertex3f(p.pos.x + 1.0, p.pos.y - 1.0, p.pos.z);
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
