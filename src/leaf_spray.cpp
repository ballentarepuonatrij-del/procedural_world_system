#include "planet/leaf_spray.hpp"
#include "planet/math.hpp"

namespace planet {
namespace {
struct Point {
    double x,y;
    Point operator+(Point b) const {return {x+b.x,y+b.y};}
    Point operator-(Point b) const {return {x-b.x,y-b.y};}
    Point operator*(double s) const {return {x*s,y*s};}
};
struct Tile {
    std::vector<LeafPixel>& pixels;
    int column,row;

    // Paint a tapered stem or a leaf in its own length/width coordinates.
    void stroke(Point a,Point b,double width,double shade,bool stem,bool lobed=false) {
        const Point axis=b-a;
        const double squared=axis.x*axis.x+axis.y*axis.y;
        const double distance=std::sqrt(squared);
        const auto texel=[](double p){return clamp(static_cast<int>(std::floor(p*leafTileSize)),0,leafTileSize-1);};
        const int x0=texel(std::min(a.x,b.x)-width),x1=texel(std::max(a.x,b.x)+width);
        const int y0=texel(std::min(a.y,b.y)-width),y1=texel(std::max(a.y,b.y)+width);
        for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
            const Point p{(x+.5)/leafTileSize-a.x,(y+.5)/leafTileSize-a.y};
            const double t=(p.x*axis.x+p.y*axis.y)/squared;
            if(t<0 || t>1) continue;
            const double side=(axis.x*p.y-axis.y*p.x)/distance;
            double halfWidth=stem ? width*(1-.45*t) : width*std::sin(pi*t);
            if(lobed) halfWidth*=(.74+.46*t)*(.80+.20*std::cos(t*8*pi));
            const double coverage=clamp((halfWidth-std::abs(side))*leafTileSize+.5,0.,1.);
            if(coverage==0) continue;
            const double fold=stem ? .69 : .88+.10*t+(side>0 ? .07 : -.04);
            const double vein=!stem && std::abs(side)<.13*halfWidth ? .13 : 0.;
            const auto channel=[](double v){return static_cast<std::uint8_t>(255*clamp(v,0.,1.));};
            const double value=shade*fold;
            const auto alpha=channel(coverage);
            auto& pixel=pixels[static_cast<std::size_t>((row+y)*leafAtlasSize+column+x)];
            if(alpha>=pixel[3]) pixel={channel(value*(stem ? 1.03 : .95)+vein),channel(value+.06),channel(value*.84),alpha};
        }
    }

    void paint(LeafSpray kind) {
        const bool needle=kind==LeafSpray::Needles,narrow=kind==LeafSpray::Narrow;
        const auto spine=[](double t){return Point{.5+.035*std::sin(t*2*pi),t};};
        for(int i=0;i<12;++i) stroke(spine(i/12.),spine((i+1)/12.),.010*(1-.65*i/12.),1,true);
        const int pairs=needle ? 10 : narrow ? 6 : 5;
        for(int i=0;i<pairs;++i) for(int sign:{-1,1}) {
            const auto seed=static_cast<std::uint64_t>(143*i+sign+100);
            const double start=.045+(.78/pairs)*(i+(sign>0 ? .37 : 0));
            const auto root=spine(start);
            const double spread=(.36+.06*randomUnit(seed))*(.57+.43*std::sin(pi*(start+.12)))*(1-.59*start);
            const Point end=root+Point{sign*spread,.09+.085*randomUnit(seed+1)};
            stroke(root,end,needle ? .007 : .009,1,true);
            const int blades=needle ? 8 : narrow ? 5 : 3;
            for(int j=0;j<blades;++j) for(int side:{-1,1}) {
                const double k=(j+.15+(side>0 ? .35 : 0))/blades;
                const Point joint=root+(end-root)*k;
                const double lengthScale=(1-.36*k)*(.83+.30*randomUnit(seed+static_cast<unsigned>(j*3+side+2)));
                const double dx=needle ? .045 : narrow ? .078 : .09+.028*randomUnit(seed+4*j);
                const double dy=needle ? .065 : narrow ? .126 : .105+.045*randomUnit(seed+j+5);
                const Point tip=joint+Point{sign*dx,side*dy+.024}*lengthScale;
                const double width=(needle ? .007 : narrow ? .018 : .043)*lengthScale;
                stroke(joint,tip,width,.83+.19*randomUnit(seed+j+11),false,!needle && !narrow);
            }
            stroke(end,end+Point{sign*.063,.026},needle ? .007 : .021,1,false);
        }
        stroke(spine(.87),{.51,.998},needle ? .011 : .027,1,false);
    }
};
}
LeafUV leafCoordinates(LeafSpray kind,float u,float v) {
    const int tile=static_cast<int>(kind);
    return {(.5f+static_cast<float>((tile%2)*leafTileSize)+u*(leafTileSize-1))/leafAtlasSize,
            (.5f+static_cast<float>((tile/2)*leafTileSize)+v*(leafTileSize-1))/leafAtlasSize};
}
std::vector<LeafPixel> makeLeafAtlas() {
    std::vector<LeafPixel> pixels(leafAtlasSize*leafAtlasSize,LeafPixel{220,235,206,0});
    for(int i=0;i<3;++i) Tile{pixels,(i%2)*leafTileSize,(i/2)*leafTileSize}.paint(static_cast<LeafSpray>(i));
    for(int y=leafTileSize;y<leafAtlasSize;++y)
        std::fill_n(pixels.begin()+y*leafAtlasSize+leafTileSize,leafTileSize,LeafPixel{255,255,255,255});
    return pixels;
}
} // namespace planet
