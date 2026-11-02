#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace planet {
constexpr double pi = std::numbers::pi;
constexpr double tau = 2.0 * pi;
constexpr double radians(double degrees) { return degrees * pi / 180.0; }
constexpr double degrees(double angle) { return angle * 180.0 / pi; }
template<class T> constexpr T clamp(T x, T low, T high) { return std::clamp(x, low, high); }
inline double mix(double a, double b, double t) { return a + (b - a) * t; }
inline double smoothstep(double low, double high, double x) {
    const double t = clamp((x - low) / (high - low), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
struct Vec3 {
    double x{}, y{}, z{};
    Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(double s) const { return *this * (1.0 / s); }
    Vec3& operator+=(Vec3 b) { *this = *this + b; return *this; }
};
inline double dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
inline double length(Vec3 v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalized(Vec3 v) {
    const double n = length(v);
    return n > 1e-15 ? v / n : Vec3{0, 1, 0};
}
inline Vec3 direction(double latitude, double longitude) {
    return {std::cos(latitude)*std::cos(longitude), std::sin(latitude), -std::cos(latitude)*std::sin(longitude)};
}
inline double latitude(Vec3 p) { return std::asin(clamp(normalized(p).y, -1.0, 1.0)); }
inline double longitude(Vec3 p) { return std::atan2(-p.z, p.x); }
inline Vec3 east(Vec3 up) {
    if (up.x*up.x+up.z*up.z < 1e-24) return {0,0,-1};
    return normalized(Vec3{up.z, 0.0, -up.x});
}
inline Vec3 north(Vec3 up) { return normalized(cross(up, east(up))); }
inline Vec3 rotate(Vec3 v, Vec3 axis, double angle) {
    return v*std::cos(angle) + cross(axis, v)*std::sin(angle) + axis*(dot(axis, v)*(1-std::cos(angle)));
}
inline std::uint64_t hash64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27U)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31U);
}
inline double randomUnit(std::uint64_t seed) { return static_cast<double>(hash64(seed) >> 11U) * 0x1.0p-53; }

class Noise {
public:
    explicit Noise(std::uint64_t seed) : seed_(seed) {}
    double value(Vec3 p) const;
    double fbm(Vec3 p, int octaves = 5) const;
private:
    std::uint64_t seed_;
};
} // namespace planet
