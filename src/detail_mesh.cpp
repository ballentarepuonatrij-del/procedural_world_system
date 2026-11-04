#include "planet/shadows.hpp"

namespace planet {
std::vector<MeshFace> detailMesh(const SurfaceObject& object,Vec3 origin,Vec3 up,Vec3 right,bool simple) {
    std::vector<MeshFace> faces;bool sheets=false;
    right=rotate(right,up,object.angle);
    const Vec3 back=cross(right,up);
    const auto transform=[&](Vec3 p) { return origin+right*p.x+up*p.y+back*p.z; };
    const auto triangle=[&](Vec3 a,Vec3 b,Vec3 c,Color color) {
        a=transform(a); b=transform(b); c=transform(c);
        if(length(cross(b-a,c-a))>1e-10) faces.push_back({{a,b,c,sheets},color});
    };
    const auto tube=[&](Vec3 a,Vec3 b,double base,double tip,Color color) {
        const bool wasSheet=sheets;sheets=false;
        const Vec3 axis=normalized(b-a);
        const Vec3 side=normalized(cross(axis,Vec3{0.4,0.1,1})), front=cross(axis,side);
        for(int i=0;i<6;++i) {
            const Vec3 u=side*std::cos(tau*i/6)+front*std::sin(tau*i/6);
            const Vec3 v=side*std::cos(tau*(i+1)/6)+front*std::sin(tau*(i+1)/6);
            triangle(a+u*base,a+v*base,b+v*tip,color);
            triangle(a+u*base,b+v*tip,b+u*tip,color);
            triangle(a,a+v*base,a+u*base,color);
            triangle(b,b+u*tip,b+v*tip,color);
        }
        sheets=wasSheet;
    };
    const auto crown=[&](Vec3 center,Vec3 scale,Color color,std::uint64_t salt) {
        const int rings=simple ? 3 : 4, sides=simple ? 6 : 8;
        const auto vertex=[&](int row,int column) {
            column%=sides;
            const double lat=-pi/2+pi*row/rings, angle=tau*column/sides;
            const double rough=0.82+0.30*randomUnit(object.id+salt+static_cast<std::uint64_t>(row*17+column));
            return center+Vec3{std::cos(lat)*std::cos(angle)*scale.x*rough,
                               std::sin(lat)*scale.y,std::cos(lat)*std::sin(angle)*scale.z*rough};
        };
        for(int row=0;row<rings;++row) for(int column=0;column<sides;++column) {
            const float tint=static_cast<float>(0.83+0.22*randomUnit(object.id+salt+static_cast<std::uint64_t>(row*31+column)));
            const Color shaded{color.r*tint,color.g*tint,color.b*tint};
            const auto a=vertex(row,column),b=vertex(row,column+1),c=vertex(row+1,column),d=vertex(row+1,column+1);
            if(row>0) triangle(a,c,b,shaded);
            if(row+1<rings) triangle(b,c,d,shaded);
        }
    };
    const double h=object.height,r=object.radius;
    const float variation=static_cast<float>(0.85+0.30*randomUnit(object.id+11));
    const auto& traits=plantTraits(object.species);
    const float foliage=static_cast<float>(object.plant.foliage);
    const Color leaves{(traits.leaves.r*foliage+.38f*(1-foliage))*variation,
        (traits.leaves.g*foliage+.26f*(1-foliage))*variation,(traits.leaves.b*foliage+.12f*(1-foliage))*variation};
    if(traits.aquatic || object.species==Species::Fern) {
        sheets=true;
        if(object.species==Species::Kelp) {
            // A holdfast, flexible stipe and alternating broad blades.
            tube({0,0,0},{r*.4,h,0},h*.009,h*.003,{.34f,.29f,.10f});
            for(int i=1;i<9;++i) {
                const double y=h*i/10,side=i%2 ? 1.0 : -1.0;
                const Vec3 a{r*.4*i/10,y,0},b=a+Vec3{r*side,y*.06,r*.5};
                triangle(a,b+Vec3{0,h*.12,0},b+Vec3{r*side*.3,0,-r*.4},leaves);
                triangle(a,b+Vec3{r*side*.3,0,-r*.4},b-Vec3{0,h*.06,0},leaves);
            }
        } else if(object.species==Species::Fern) {
            for(int i=0;i<7;++i) {
                const Vec3 direction{std::cos(tau*i/7),0,std::sin(tau*i/7)};
                const Vec3 side=cross(direction,Vec3{0,1,0});
                for(int j=1;j<6;++j) {
                    const double f=j/6.0;
                    const Vec3 a=direction*(r*f)+Vec3{0,h*std::sin(f*pi*.65),0};
                    const Vec3 b=direction*(r*(f-.2))+Vec3{0,h*std::sin(std::max(0.0,f-.2)*pi*.65),0};
                    triangle(b,a+side*(r*.23*(1-f)),a,leaves);
                    triangle(b,a,a-side*(r*.23*(1-f)),leaves);
                }
            }
        } else {
            const int blades=object.species==Species::Rockweed ? 9 : 7;
            for(int i=0;i<blades;++i) {
                const Vec3 side{std::cos(tau*i/blades),0,std::sin(tau*i/blades)};
                for(int j=0;j<4;++j) {
                    const auto point=[&](double f) { return side*(r*(.2+.65*f*f))+Vec3{0,h*f*(.65+.35*randomUnit(object.id+static_cast<unsigned>(i))),0}; };
                    const double a=j/4.0,b=(j+1)/4.0;
                    const double width=r*(object.species==Species::Rockweed ? .19 : .07);
                    triangle(point(a)-side*width*(1-a),point(a)+side*width*(1-a),point(b)+side*width*(1-b),leaves);
                    triangle(point(a)-side*width*(1-a),point(b)+side*width*(1-b),point(b)-side*width*(1-b),leaves);
                }
            }
        }
        return faces;
    }
    switch(object.kind) {
        case DetailKind::Rock:
            crown({0,h*0.32,0},{r,h*0.68,r*0.82},{0.46f*variation,0.43f*variation,0.36f*variation},101);
            break;
        case DetailKind::Tree: {
            const Color bark=object.species==Species::Birch ? Color{.78f,.77f,.69f} : Color{.27f,.19f,.12f};
            tube({0,-0.4,0},{0,h*0.72,0},h*0.025,h*0.008,bark);
            if(object.plant.stage==GrowthStage::Dead) {
                for(int i=0;i<3;++i) tube({0,h*.4,0},{r*std::cos(tau*i/3),h*.75,r*std::sin(tau*i/3)},h*.014,h*.003,bark);
                break;
            }
            if(object.evergreen) {
                for(int layer=0;layer<3;++layer) {
                    const double base=h*(0.24+layer*0.20), width=r*(1-layer*0.22);
                    for(int i=0;i<8;++i) {
                        const Vec3 a{width*std::cos(tau*i/8),base,width*std::sin(tau*i/8)};
                        const Vec3 b{width*std::cos(tau*(i+1)/8),base,width*std::sin(tau*(i+1)/8)};
                        triangle(a,{0,std::min(h,base+h*0.4),0},b,{0.13f,0.29f,0.18f});
                        triangle({0,base,0},a,b,{0.13f,0.29f,0.18f});
                    }
                }
            } else {
                const bool flat=object.species==Species::Acacia;
                crown({0,h*(flat ? .86 : .73),0},{r,h*(flat ? .14 : .27),r},leaves,200);
                if(!simple) for(int i=0;i<3;++i) {
                    const double angle=tau*i/3+0.4;
                    const Vec3 branch{std::cos(angle)*r*0.65,h*(0.56+i*0.055),std::sin(angle)*r*0.65};
                    tube({0,h*0.38,0},branch,h*0.012,h*0.004,bark);
                    crown(branch,{r*0.62,h*0.17,r*0.62},leaves,static_cast<std::uint64_t>(300+i*80));
                }
            }
            break;
        }
        case DetailKind::Shrub:
            crown({0,h*0.43,0},{r,h*0.57,r},leaves,500);
            if(object.species==Species::Heather && foliage>.5f && object.plant.stage==GrowthStage::Mature)
                for(int i=0;i<3;++i) crown({r*.6*std::cos(tau*i/3),h*.8,r*.6*std::sin(tau*i/3)},{r*.24,h*.18,r*.24},{.61f,.32f,.54f},static_cast<unsigned>(700+i));
            break;
        case DetailKind::Grass:
            sheets=true;
            for(int i=0;i<5;++i) {
                const double angle=tau*i/5;
                const Vec3 side{std::cos(angle),0,std::sin(angle)};
                const Vec3 base=side*(r*0.35);
                triangle(base-side*0.06,base+side*0.06,base+Vec3{r*0.3,h*(0.6+0.1*i),r*0.2},leaves);
            }
            break;
    }
    return faces;
}
} // namespace planet
