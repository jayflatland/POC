#pragma once

#include <math.h>

class Vector {
public:
    double x, y, z;
    Vector() : x(0.0), y(0.0), z(0.0) {}
    Vector(const double &x, const double &y, const double &z) : x(x), y(y), z(z) {}

    void zero() {
        x = y = z = 0.0;
    }

    Vector operator +(const Vector &other) const {
        return Vector(x + other.x, y + other.y, z + other.z);
    }

    Vector operator -(const Vector &other) const {
        return Vector(x - other.x, y - other.y, z - other.z);
    }

    double dot(const Vector &other) const {
        return x * other.x + y * other.y + z * other.z;
    }

    void operator +=(const Vector &other) {
        x += other.x;
        y += other.y;
        z += other.z;
    }

    Vector operator *(const double &other) const {
        return Vector(x * other, y * other, z * other);
    }

    double magnitude() const {
        return sqrt(x*x + y*y + z*z);
    }

    void normalize() {
        double mag = magnitude();
        if(mag > 0.0) {
            x /= mag;
            y /= mag;
            z /= mag;
        } else {
            x = 1.0;
            y = 0.0;
            z = 0.0;
        }
    }

    double dist(const Vector &other) const {
        return (*this - other).magnitude();
    }
};
