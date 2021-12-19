#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_real_distribution.hpp>

#include "constants.hpp"
#include "vector.hpp"
#include <vector>

static const double GRAV_CONSTANT   = 6.67408e-11; // m3 kg-1 s-2
static const double INIT_VEL_SCALE  = 1e3;

class Particle {
public:
    static boost::random::mt19937 randgen_;

    Vector3 pos;
    Vector3 vel;
    Vector3 force;
    Vector3 color;
    double mass;
    double radius;
    double disp_radius;

    std::vector<Vector3> pos_history;
    Particle() {
        // boost::random::uniform_real_distribution<> pos_dist(-100.0, 100.0);
        // boost::random::uniform_real_distribution<> vel_dist(-INIT_VEL_SCALE, INIT_VEL_SCALE);
        // boost::random::uniform_real_distribution<> color_dist(0.5, 1.0);

        // pos = Vector3(pos_dist(randgen_), pos_dist(randgen_), 0.0);
        // vel = Vector3(vel_dist(randgen_), vel_dist(randgen_), 0.0);
        // color = Vector3(color_dist(randgen_), color_dist(randgen_), color_dist(randgen_));
        // mass = 1.0;
        // radius = 1.0;
        // disp_radius = 1.0;
    }

    void randomize() {
        
    }

    void elapse_time(double dt) {
        vel.x += force.x * dt / mass;
        vel.y += force.y * dt / mass;
        vel.z += force.z * dt / mass;

        pos.x += vel.x * dt;
        pos.y += vel.y * dt;
        pos.z += vel.z * dt;

        pos_history.push_back(pos);

        // if(pos.x >  100.0) {pos.x =  200.0 - pos.x; vel.x = -vel.x;}
        // if(pos.y >  100.0) {pos.y =  200.0 - pos.y; vel.y = -vel.y;}
        // if(pos.z >  100.0) {pos.z =  200.0 - pos.z; vel.z = -vel.z;}
        // if(pos.x < -100.0) {pos.x = -200.0 - pos.x; vel.x = -vel.x;}
        // if(pos.y < -100.0) {pos.y = -200.0 - pos.y; vel.y = -vel.y;}
        // if(pos.z < -100.0) {pos.z = -200.0 - pos.z; vel.z = -vel.z;}
    }

    void preinteract() {
        force.zero();
    }

    void interact(const Particle &other) {
        double r = pos.dist(other.pos);
        double F = GRAV_CONSTANT * mass * other.mass / (r * r);
        Vector3 dirto = other.pos - pos;
        dirto.normalize();
        force += dirto * F;
    }

    void postinteract() {

    }
};
