#pragma once
// Unit literals. Lengths are stored in centimetres, angles in degrees.
//
//   70_cm    5_mm    25_deg

namespace twintailor {

inline constexpr double pi = 3.14159265358979323846;

// What a number measures, so that it is shown and typed with its unit:
// 96 cm, 25°, 66.7 % (a fraction: 0.667), or a plain number.
enum class Unit { Length, Angle, Fraction, None };

inline namespace literals {

constexpr double operator""_cm(long double v) { return static_cast<double>(v); }
constexpr double operator""_cm(unsigned long long v) { return static_cast<double>(v); }
constexpr double operator""_mm(long double v) { return static_cast<double>(v) / 10.0; }
constexpr double operator""_mm(unsigned long long v) { return static_cast<double>(v) / 10.0; }
constexpr double operator""_deg(long double v) { return static_cast<double>(v); }
constexpr double operator""_deg(unsigned long long v) { return static_cast<double>(v); }

} // namespace literals
} // namespace twintailor
