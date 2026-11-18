/* Optional C++11 conveniences; MIT; (c) 2026 SH2ZAM Contributors. */
#ifndef SH2ZAM_HPP
#define SH2ZAM_HPP
#include "sh2zam.h"
namespace sh2z {
class fixed {
    sh2z_fx_t value_;
    explicit constexpr fixed(sh2z_fx_t raw) : value_(raw) {}
public:
    constexpr fixed() : value_(0) {}
    static constexpr fixed from_raw(sh2z_fx_t raw) { return fixed(raw); }
    static fixed from_int(int32_t n) { return fixed(sh2z_from_int(n)); }
    constexpr sh2z_fx_t raw() const { return value_; }
    int32_t integer() const { return sh2z_to_int(value_); }
    fixed sqrt() const { return fixed(sh2z_sqrt(value_)); }
    friend fixed operator+(fixed a,fixed b) { return fixed(sh2z_add(a.raw(),b.raw())); }
    friend fixed operator-(fixed a,fixed b) { return fixed(sh2z_sub(a.raw(),b.raw())); }
    friend fixed operator*(fixed a,fixed b) { return fixed(sh2z_mul(a.raw(),b.raw())); }
    friend fixed operator/(fixed a,fixed b) { return fixed(sh2z_div(a.raw(),b.raw())); }
    friend bool operator==(fixed a,fixed b) { return a.raw()==b.raw(); }
    friend bool operator!=(fixed a,fixed b) { return !(a==b); }
};
struct vec3 {
    sh2z_vec3_t value;
    vec3(fixed x,fixed y,fixed z) : value{x.raw(),y.raw(),z.raw()} {}
    explicit vec3(sh2z_vec3_t v) : value(v) {}
    fixed length() const { return fixed::from_raw(sh2z_vec3_length(value)); }
    fixed dot(vec3 b) const { return fixed::from_raw(sh2z_vec3_dot(value,b.value)); }
    vec3 direction() const { return vec3(sh2z_vec3_normalize(value)); }
    vec3 transformed(const sh2z_mat34_t& m) const { return vec3(sh2z_mat34_transform_point(&m,value)); }
    friend vec3 operator+(vec3 a,vec3 b) { return vec3(sh2z_vec3_add(a.value,b.value)); }
    friend vec3 operator-(vec3 a,vec3 b) { return vec3(sh2z_vec3_sub(a.value,b.value)); }
    friend vec3 operator*(vec3 a,fixed s) { return vec3(sh2z_vec3_scale(a.value,s.raw())); }
};
}
#endif
