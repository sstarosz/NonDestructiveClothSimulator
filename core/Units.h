#pragma once
// Unit literals. Lengths are stored in centimetres, angles in degrees.
//
//   70_cm    5_mm    25_deg

namespace clothcad {

inline constexpr double pi = 3.14159265358979323846;

inline namespace literals {

constexpr double operator""_cm(long double v) { return static_cast<double>(v); }
constexpr double operator""_cm(unsigned long long v) { return static_cast<double>(v); }
constexpr double operator""_mm(long double v) { return static_cast<double>(v) / 10.0; }
constexpr double operator""_mm(unsigned long long v) { return static_cast<double>(v) / 10.0; }
constexpr double operator""_deg(long double v) { return static_cast<double>(v); }
constexpr double operator""_deg(unsigned long long v) { return static_cast<double>(v); }

} // namespace literals
} // namespace clothcad
