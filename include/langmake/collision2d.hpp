#pragma once
#include "types.hpp"
#include <cmath>
#include <stdexcept>
namespace lm {
// Normalized sprite-space convex fixtures. Bounded authoring keeps contacts cheap.
inline void validate_collision2d(const Json& value) {
    if(!value.is_object())throw std::runtime_error("physics2d must be an object");
    for(const auto& [key,v]:value.items()) {
        if(key=="rotate") { if(!v.is_boolean())throw std::runtime_error("physics2d.rotate must be boolean"); }
        else if(key=="friction"||key=="restitution"||key=="angular_damping") {
            const double max=key=="angular_damping"?20:1;
            if(!v.is_number()||!std::isfinite(v.get<double>())||v.get<double>()<0||v.get<double>()>max)
                throw std::runtime_error("physics2d material value out of range");
        } else if(key!="polygons")throw std::runtime_error("Unknown physics2d property: "+key);
    }
    if(!value.contains("polygons"))return;
    const auto& parts=value["polygons"];
    if(!parts.is_array()||parts.empty()||parts.size()>4)throw std::runtime_error("physics2d requires 1..4 convex polygons");
    for(const auto& poly:parts) {
        if(!poly.is_array()||poly.size()<3||poly.size()>8)throw std::runtime_error("physics2d polygon requires 3..8 vertices");
        for(const auto& v:poly) {
            if(!v.is_array()||v.size()!=2)throw std::runtime_error("physics2d vertex requires two coordinates");
            for(const auto& n:v)if(!n.is_number()||!std::isfinite(n.get<double>())||std::abs(n.get<double>())>.5)
                throw std::runtime_error("physics2d vertices must be finite and within -0.5..0.5");
        }
        // Every other vertex must be strictly left of each directed edge (CCW).
        for(size_t i=0;i<poly.size();++i) {
            const auto& a=poly[i];const auto& b=poly[(i+1)%poly.size()];
            for(size_t j=0;j<poly.size();++j)if(j!=i&&j!=(i+1)%poly.size()) {
                const double cross=(b[0].get<double>()-a[0].get<double>())*(poly[j][1].get<double>()-a[1].get<double>())-(b[1].get<double>()-a[1].get<double>())*(poly[j][0].get<double>()-a[0].get<double>());
                if(cross<1e-6)throw std::runtime_error("physics2d polygon must be strictly convex and counterclockwise");
            }
        }
    }
}
inline void validate_collision2d_entity(const Entity& e,const std::string& dimension) {
    validate_collision2d(e.physics2d);
    if(!e.physics2d.empty()&&(dimension!="2d"||!e.controller.empty()||!e.parent.empty()||e.rotation[0]!=0||e.rotation[1]!=0))
        throw std::runtime_error("physics2d requires unparented 2D bodies without character controllers or X/Y rotation");
}
}
