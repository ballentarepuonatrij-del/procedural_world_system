#include "planet/globe_camera.hpp"

namespace planet {
void GlobeCamera::center(Vec3 observer) {outward=normalized(observer);up=north(outward);}
void GlobeCamera::drag(double dx,double dy) {
    if(!std::isfinite(dx) || !std::isfinite(dy)) return;
    outward=normalized(rotate(outward,up,-dx*.006));
    const auto axis=right();outward=normalized(rotate(outward,axis,-dy*.006));
    up=normalized(cross(axis,-outward));
}
std::optional<Vec3> GlobeCamera::pick(double x,double y,int width,int height) const {
    if(width<1 || height<1 || !std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>=width || y>=height) return std::nullopt;
    const double scale=std::tan(fov/2),aspect=static_cast<double>(width)/height;
    const Vec3 ray=normalized(-outward+right()*((2*x/width-1)*aspect*scale)+up*((1-2*y/height)*scale));
    const Vec3 origin=outward*distance;const double b=dot(origin,ray),discriminant=b*b-(distance*distance-1);
    if(discriminant<0) return std::nullopt;
    const double t=-b-std::sqrt(discriminant);if(t<=0) return std::nullopt;
    return normalized(origin+ray*t);
}
} // namespace planet
