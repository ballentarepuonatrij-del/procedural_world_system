#include "planet/fauna.hpp"

namespace planet {
std::vector<MeshFace> deerMesh(const DeerPose& p,bool simple) {
    std::vector<MeshFace> faces;faces.reserve(simple ? 240 : 600);
    const float tone=static_cast<float>(.88+.18*randomUnit(p.id));
    const Color coat{.55f*tone,.32f*tone,.16f*tone},dark{.18f,.10f,.055f},belly{.77f,.66f,.45f},black{.045f,.035f,.025f};
    const double folded=p.foldLegs,head=p.lowerHead;
    const double bob=(p.speed>4 ? .055*std::cos(tau*p.stride*2) : .012*std::sin(tau*p.stride*2))*(1-folded);
    const double bodyY=1.02-.57*folded+bob;
    const auto triangle=[&](Vec3 a,Vec3 b,Vec3 c,Color color) {
        a=a*p.size;b=b*p.size;c=c*p.size;
        if(length(cross(b-a,c-a))>1e-10) faces.push_back({{a,b,c},color});
    };
    const auto ball=[&](Vec3 center,Vec3 scale,Color color,int sides) {
        const int rows=simple ? 3 : 4;
        const auto v=[&](int row,int col) {const double lat=-pi/2+pi*row/rows,a=tau*col/sides;return center+Vec3{scale.x*std::cos(lat)*std::cos(a),scale.y*std::sin(lat),scale.z*std::cos(lat)*std::sin(a)};};
        for(int row=0;row<rows;++row) for(int col=0;col<sides;++col) {
            triangle(v(row,col),v(row+1,col),v(row+1,col+1),color);triangle(v(row,col),v(row+1,col+1),v(row,col+1),color);
        }
    };
    const auto tube=[&](Vec3 a,Vec3 b,double start,double end,Color color,int sides=5) {
        const auto axis=normalized(b-a),u=normalized(cross(axis,std::abs(axis.y)<.9 ? Vec3{0,1,0} : Vec3{1,0,0})),v=cross(axis,u);
        for(int i=0;i<sides;++i) {
            const auto q=u*std::cos(tau*i/sides)+v*std::sin(tau*i/sides),r=u*std::cos(tau*(i+1)/sides)+v*std::sin(tau*(i+1)/sides);
            triangle(a+q*start,b+r*end,b+q*end,color);triangle(a+q*start,a+r*start,b+r*end,color);
            triangle(a,a+r*start,a+q*start,color);triangle(b,b+q*end,b+r*end,color);
        }
    };
    ball({0,bodyY,0},{.30,.37,.73},coat,simple ? 6 : 8);
    ball({0,bodyY-.17,.04},{.255,.20,.59},belly,6);
    ball({0,bodyY+.07,-.49},{.29,.34,.35},coat,6);
    // Articulated two-segment legs; diagonal walk and paired running phases.
    for(int side:{-1,1}) for(int front:{-1,1}) {
        const double phase=tau*p.stride+(p.speed>4 ? (front>0 ? 0 : pi*.65)+(side>0 ? .3 : 0) : (side==front ? 0 : pi));
        const double swing=(p.speed>4 ? .43 : .19)*std::sin(phase)*(p.speed>.05 ? 1 : 0)*(1-folded);
        const auto hip=Vec3{side*.20,bodyY-.10,front*.49};
        const auto knee=Vec3{side*.22,mix(.53,.22,folded)+std::max(0.,std::sin(phase))*.07*(p.speed>.05 ? 1 : 0),front*.49+swing*.5+folded*(front>0 ? -.25 : .24)};
        const auto foot=Vec3{side*.23,.06+std::max(0.,std::sin(phase))*(p.speed>4 ? .23 : .09)*(p.speed>.05 ? 1 : 0)*(1-folded),front*.49+swing+folded*(front>0 ? -.52 : .38)};
        tube(hip,knee,.080,.052,coat);tube(knee,foot,.046,.027,dark);
        ball(foot+Vec3{0,-.015,.025},{.055,.055,.095},black,5);
    }
    const Vec3 shoulder{0,bodyY+.15,.48},headCenter{0,mix(bodyY+.64,.29,head),mix(.83,1.03,head)};
    tube(shoulder,headCenter,.18,.12,coat,6);
    ball(headCenter,{.13,.18,.26},coat,simple ? 6 : 8);
    const Vec3 snout=headCenter+Vec3{0,-.065-.055*head,.24};
    ball(snout,{.095,.09,.18},coat,6);ball(snout+Vec3{0,-.012,.14},{.08,.061,.06},black,5);
    for(double side:{-1.,1.}) {
        const Vec3 ear=headCenter+Vec3{side*.20,.17,-.075};
        ball(ear,{.15,.07,.08},coat,5);
        if(!simple) {ball(ear+Vec3{0,.025,.015},{.105,.035,.05},belly,5);ball(headCenter+Vec3{side*.12,.04,.075},{.022,.024,.032},black,5);}
        if(p.male) {
            const Vec3 root=headCenter+Vec3{side*.085,.13,-.10};
            const Vec3 tip=root+Vec3{side*.22,.47,-.16};
            tube(root,tip,.032,.013,belly,4);
            for(int j=1;j<=3;++j) {
                const auto joint=root+(tip-root)*(j*.23);
                tube(joint,joint+Vec3{side*.10,.13,.13-j*.015},.017,.003,belly,4);
            }
        }
    }
    ball({0,bodyY+.03,-.71},{.17,.22,.08},belly,6);
    tube({0,bodyY+.20,-.65},{0,bodyY+.10+(p.activity==DeerActivity::Fleeing ? .24 : 0),-.95},.065,.025,coat,5);
    return faces;
}
} // namespace planet
