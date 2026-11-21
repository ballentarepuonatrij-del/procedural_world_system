#include "tree_mesh.hpp"

namespace planet {
namespace {
constexpr Vec3 vertical{0,1,0};
constexpr double goldenAngle=2.399963229728653;
Color tinted(Color color,double factor) {
    const auto f=static_cast<float>(factor);
    return {color.r*f,color.g*f,color.b*f};
}
Vec3 radial(double angle) {return {std::cos(angle),0,std::sin(angle)};}
struct Limb {
    Vec3 root,bend,tip;
    Vec3 at(double t) const {
        if(t<=.5) return root+(bend-root)*(2*t);
        return bend+(tip-bend)*(2*t-1);
    }
};

class BranchBuilder {
public:
    BranchBuilder(const SurfaceObject& object,Color bark,Color leaves,bool simple,bool fine,
                std::vector<MeshFace>& faces,std::vector<Triangle>* shadows)
        : plant(object),wood(bark),green(leaves),far(simple),extra(fine && !simple),
          mesh(faces),shade(shadows),h(object.height),r(object.radius),
          seed(hash64(object.id)^hash64(object.plant.generation)) {}
    BranchJoints grow() {
        if(plant.kind==DetailKind::Shrub) shrub();
        else {
            const auto lean=radial(tau*randomUnit(seed))*r*(.05+.10*randomUnit(seed+1));
            const Limb trunk{{0,-std::min(.16,.024*h),0},lean*.30+vertical*(h*.44),lean+vertical*(h*.95)};
            limbMesh(trunk,plant.collisionRadius(),far ? 3 : 6,true);
            if(plant.evergreen) conifer(trunk);
            else broadleaf(trunk);
        }
        return std::move(joints);
    }
private:
    const SurfaceObject& plant;
    Color wood,green;
    bool far,extra;
    std::vector<MeshFace>& mesh;
    std::vector<Triangle>* shade;
    double h,r;
    std::uint64_t seed;
    BranchJoints joints;

    void woodFace(Vec3 a,Vec3 b,Vec3 c,bool casts,bool sheet=false) {
        mesh.push_back({{a,b,c,sheet},wood,{},0,false,casts});
    }
    void limbMesh(const Limb& limb,double radius,int sides,bool casts) {
        // Two joined rings share the bend. The narrowing tip closes the limb.
        const auto direction=normalized(limb.tip-limb.root);
        const Vec3 across=normalized(cross(direction,std::abs(direction.y)>.9 ? Vec3{1,0,0} : vertical));
        const auto ring=[&](Vec3 axis,double width,Vec3 center) {
            const Vec3 side=normalized(across-axis*dot(axis,across));
            const Vec3 front=cross(axis,side);
            std::array<Vec3,6> points{};
            for(int i=0;i<sides;++i) {
                const auto angle=tau*i/sides;
                points[static_cast<std::size_t>(i)]=center+(side*std::cos(angle)+front*std::sin(angle))*width;
            }
            return points;
        };
        const auto base=ring(normalized(limb.bend-limb.root),radius,limb.root);
        const auto middle=ring(direction,radius*.62,limb.bend);
        for(int i=0;i<sides;++i) {
            const auto j=static_cast<std::size_t>(i),k=static_cast<std::size_t>((i+1)%sides);
            woodFace(base[j],base[k],middle[k],casts);
            woodFace(base[j],middle[k],middle[j],casts);
            woodFace(middle[j],middle[k],limb.tip,casts);
        }
    }
    void twigMesh(const Limb& twig,double radius,bool casts=false) {
        // A continuous, tapered strip for subpixel forks. At the elbow both
        // segments share an edge, including when the branch droops.
        const auto axis=normalized(twig.tip-twig.root);
        const auto side=normalized(vertical-axis*axis.y)*radius;
        const auto a=twig.root-side,b=twig.root+side,c=twig.bend+side*.57,d=twig.bend-side*.57;
        woodFace(a,b,c,casts,true);woodFace(a,c,d,casts,true);woodFace(d,c,twig.tip,casts,true);
    }

    void spray(Vec3 joint,Vec3 extent,double width,LeafSpray kind,std::uint64_t key,bool detail,bool caster) {
        if(plant.plant.stage==GrowthStage::Dead || plant.plant.foliage<=0) return;
        const auto axis=normalized(extent);
        const double roll=(randomUnit(key+1)-.5)*1.9;
        const auto reference=std::abs(axis.y)>.98 ? Vec3{1,0,0} : vertical;
        const auto side=rotate(normalized(cross(axis,reference)),axis,roll)*width;
        // The painted rachis starts at the actual attachment vertex. Three
        // triangles give the shoot a slight twist toward its free end.
        const auto fold=normalized(cross(side,extent))*(width*.23);
        const auto shoulder=joint+extent*.15,tip=joint+extent;
        const auto left=shoulder-side,right=shoulder+side;
        const auto leftEnd=tip-side+fold,rightEnd=tip+side-fold;
        const double phase=tau*randomUnit(key+2);
        const double flutter=std::min(.13,length(extent)*.038);
        const auto color=tinted(green,.92+.42*randomUnit(key+3));
        const auto uv=[&](float u,float v){return leafCoordinates(kind,u,v);};
        const auto rootUV=uv(.5f,0),leftUV=uv(0,.15f),rightUV=uv(1,.15f);
        const auto leftTipUV=uv(0,1),rightTipUV=uv(1,1);
        if(far && plant.kind==DetailKind::Shrub) {
            // A folded pair follows the outer leaves of a distant shoot. Keep
            // its attachment, centre line and leaf texture at the same scale.
            const auto middle=joint+extent*.4;
            const auto tipUV=uv(.5f,1);
            mesh.push_back({{joint,middle-side+fold,tip,true},color,{0,flutter*.4,flutter},phase,detail,false,{rootUV,uv(0,.4f),tipUV}});
            mesh.push_back({{joint,tip,middle+side+fold,true},color,{0,flutter,flutter*.4},phase,detail,false,{rootUV,tipUV,uv(1,.4f)}});
        } else {
            mesh.push_back({{joint,left,leftEnd,true},color,{0,0,flutter},phase,detail,false,{rootUV,leftUV,leftTipUV}});
            mesh.push_back({{joint,leftEnd,rightEnd,true},color,{0,flutter,flutter},phase,detail,false,{rootUV,leftTipUV,rightTipUV}});
            mesh.push_back({{joint,rightEnd,right,true},color,{0,flutter,0},phase,detail,false,{rootUV,rightTipUV,rightUV}});
        }
        if(shade && caster) {
            const auto end=tip-extent*.11,mid=joint+extent*.53;
            if(plant.kind==DetailKind::Shrub) {
                // Let light through the spray. Add the smaller side shadow up close.
                shade->push_back({joint,mid-side*.28,end,true});
                if(!far) shade->push_back({joint+extent*.31,mid+side*.39,tip-extent*.24,true});
            } else {
                const auto base=joint+extent*.17;
                shade->push_back({base,mid-side*.56,end,true});
                shade->push_back({base,end,mid+side*.53,true});
            }
        }
    }

    void heatherSpike(Vec3 base,Vec3 reach,double girth,bool flowering) {
        // The flower-bearing end of the shoot extends past the last leaves.
        const Limb stalk{base,base+reach*.5-vertical*(length(reach)*.035),base+reach};
        twigMesh(stalk,girth);
        if(!flowering) return;
        if(!far) for(double t:{.61,.74,.87}) joints.floweringTips.push_back(stalk.at(t));
        joints.floweringTips.push_back(stalk.tip);
    }

    void shrub() {
        const bool heather=plant.species==Species::Heather;
        const auto kind=heather ? LeafSpray::Narrow : LeafSpray::Needles;
        const auto& flowers=plant.flowers;
        const bool flowering=heather && plant.plant.foliage>0 && plant.plant.stage!=GrowthStage::Dead &&
            std::max({flowers.buds,flowers.bloom,flowers.seeds})>=.015;
        const int stems=plant.plant.stage==GrowthStage::Seedling ? 4 : heather ? 11 : 9;
        const double rotation=tau*randomUnit(seed+19);
        for(int i=0;i<stems;i+=far ? 2 : 1) {
            const auto key=hash64(seed+static_cast<unsigned>(i)+201);
            const double angle=rotation+goldenAngle*i+(randomUnit(key)-.5)*.63;
            const auto out=radial(angle),side=radial(angle+pi/2);
            const bool inner=i%3==0;
            const double reach=r*((inner ? .22 : .56)+.22*randomUnit(key+1));
            const double rise=h*((inner ? .68 : .40)+.13*randomUnit(key+2));
            const auto base=out*(r*(.035+.12*randomUnit(key+3)))-vertical*std::min(.055,h*.045);
            const auto bend=base+out*(reach*(heather ? .64 : .43))+vertical*(rise*(heather ? .30 : .41));
            const auto top=base+out*reach+side*(r*.22*(randomUnit(key+4)-.5))+vertical*rise;
            const Limb stem{base,bend,top};
            const double girth=h*(heather ? .012 : .021)*(.8+.35*randomUnit(key+5));
            if(far) {
                twigMesh(stem,girth);
                if(shade && i%4==0) shade->push_back({base-side*girth,base+side*girth,top,true});
            } else limbMesh(stem,girth,3,i%3==0);

            // Heather tips turn upwards from low spreading stems. Juniper
            // carries wider, outward-facing sprays along each ascending limb.
            const auto terminal=normalized(out*(inner ? .27 : .66)+vertical*(heather ? 1.6 : .86));
            const double terminalLength=h*((heather ? .23 : .28)+.10*randomUnit(key+6));
            const double tipLength=terminalLength*(far ? 1.16 : 1.);
            // The leafy upper length overlaps the basal wood and the terminal
            // spray. This also provides foliage through the middle of the bush.
            spray(bend,top-bend+terminal*(tipLength*.20),h*(heather ? .13 : .15),kind,key+10,false,i%4==0);
            const bool terminalSpike=heather && i%3!=2;
            spray(top,terminal*(tipLength*(terminalSpike ? .78 : 1.)),tipLength*(heather ? .29 : .49),kind,key+7,false,i%2==0);
            if(terminalSpike) heatherSpike(top,terminal*(terminalLength*1.38),girth*.29,flowering);

            for(int fork=0;fork<(far ? 1 : 2);++fork) {
                const auto twigKey=hash64(key+static_cast<unsigned>(fork)+110);
                const double sign=(i+fork)%2 ? -1. : 1.;
                const double turn=angle+sign*(.70+.75*randomUnit(twigKey));
                const auto dir=radial(turn);
                const auto joint=stem.at(.36+.36*fork+.09*randomUnit(twigKey+1));
                const double span=r*(.26+.15*randomUnit(twigKey+2))*(1-.20*fork);
                const double lift=h*((heather ? .22 : .12)+.07*randomUnit(twigKey+3));
                const auto elbow=joint+dir*(span*.53)+vertical*(lift*.31);
                const auto end=elbow+radial(turn+sign*.22)*(span*.47)+vertical*(lift*.69);
                const Limb twig{joint,elbow,end};
                twigMesh(twig,girth*.42,!far && fork==0 && i%2==0);
                const double size=h*(heather ? .28 : .35)*(.84+.30*randomUnit(twigKey+4))*(far ? 1.19 : 1.);
                const auto shoot=normalized(dir+vertical*(heather ? 1.85 : .30+.55*randomUnit(twigKey+5)));
                const bool lateralSpike=heather && fork==1 && i%3!=0;
                spray(end,shoot*(size*(lateralSpike ? .73 : 1.)),size*(heather ? .33 : .48),kind,twigKey+6,false,!far && fork==0);
                if(!far && randomUnit(twigKey+7)<plant.plant.foliage) {
                    const auto lateral=normalized(radial(turn-sign*.93)+vertical*(heather ? 1.35 : .32));
                    spray(twig.at(.51),lateral*(size*.93),size*(heather ? .26 : .37),kind,twigKey+8,false,false);
                }
                if(lateralSpike) heatherSpike(end,shoot*(size*1.28),girth*.24,flowering);
                if(extra) {
                    const auto lower=stem.at(.23+.41*fork);
                    const auto diverge=normalized(radial(angle-sign*.88)+vertical*(heather ? 1.27 : .50));
                    spray(lower,diverge*(size*.87),size*(heather ? .18 : .30),kind,twigKey+9,true,false);
                }
            }
        }
    }

    void broadleaf(const Limb& trunk) {
        const bool umbrella=plant.species==Species::Acacia;
        const bool hanging=plant.species==Species::Willow;
        const bool upright=plant.species==Species::Birch || plant.species==Species::Alder;
        const bool emergent=tropicalCanopy(plant.species);
        const auto kind=hanging || upright || umbrella ? LeafSpray::Narrow : LeafSpray::Broad;
        const int levels=plant.plant.stage==GrowthStage::Seedling ? 5 : 9;
        const bool vines=!far && plant.lianas()>0;
        const auto& flowers=plant.flowers;
        const bool flowering=flowerTraits(plant.species).form!=FlowerForm::None &&
            std::max({flowers.buds,flowers.bloom,flowers.seeds})>=.015;
        const double base=umbrella ? .43 : emergent ? .52 : upright ? .34 : .29;
        const double startAngle=tau*randomUnit(seed+4);
        for(int level=0;level<levels;level+=(far ? 2 : 1)) {
            const auto key=hash64(seed+static_cast<unsigned>(level));
            const double fraction=static_cast<double>(level)/(levels-1);
            const double angle=startAngle+goldenAngle*level+(randomUnit(key)-.5)*.53;
            const Vec3 outward=radial(angle),side=radial(angle+pi/2);
            const auto joint=trunk.at(mix(base,.91,fraction));
            const double spread=upright ? .99-.52*fraction :
                .94+.26*std::sin(pi*fraction)-.36*fraction;
            const double span=r*spread*(.84+.26*randomUnit(key+1));
            const double rise=umbrella ? h*(.74+.14*fraction)-joint.y :
                h*(upright ? .27 : .22)*(1-.60*fraction);
            const auto bend=joint+outward*(span*.31)+side*(span*(randomUnit(key+2)-.5)*.26)+vertical*(rise*.61);
            const auto tip=joint+outward*(span*.70)+side*(span*(randomUnit(key+3)-.5)*.35)+vertical*rise;
            const Limb branch{joint,bend,tip};
            if(far) twigMesh(branch,plant.collisionRadius()*(.54-.24*fraction));
            else limbMesh(branch,plant.collisionRadius()*(.54-.24*fraction),4,level%2==0);
            if(vines) joints.vineAnchors.push_back(branch.at(.72));
            const int forks=far ? 2 : 3;
            for(int fork=0;fork<forks;++fork) {
                const auto twigKey=hash64(key+static_cast<unsigned>(fork));
                const double sign=(fork%2==0 ? 1. : -1.);
                const double forkAngle=angle+sign*(.45+.68*randomUnit(twigKey));
                const auto forkOut=radial(forkAngle);
                const auto root=branch.at(fork==0 ? .54 : fork==1 ? .78 : 1.);
                const auto elbow=root+forkOut*(span*.24)+vertical*(rise*(hanging ? .09 : .25));
                const auto end=elbow+radial(forkAngle+sign*.23)*(span*.22)
                    +vertical*(hanging ? -h*(.12+.06*randomUnit(twigKey+1)) : rise*.21);
                const Limb twig{root,elbow,end};
                if(!far) limbMesh(twig,plant.collisionRadius()*.15,3,false);
                else twigMesh(twig,plant.collisionRadius()*.15);
                const double shoot=(span*.51+h*.027)*(far ? 1.38 : 1.);
                const double pitch=hanging ? -1.9 : umbrella ? .20 : upright ? .73 : .47;
                const int sprays=far ? 2 : 4;
                for(int i=0;i<sprays;++i) {
                    const auto sprayKey=hash64(twigKey+static_cast<unsigned>(i));
                    const double az=forkAngle+(i%2==0 ? 1 : -1)*(.46+randomUnit(sprayKey)*.81);
                    const auto dir=normalized(radial(az)+vertical*(pitch+(randomUnit(sprayKey+1)-.5)*.81));
                    const auto attach=twig.at(far ? (i==0 ? .5 : 1.) : (.28+.24*i));
                    spray(attach,dir*shoot,shoot*(hanging ? .32 : .54),kind,sprayKey,false,i==0 && (fork==0 || (!far && fork==2)));
                }
                if(extra) {
                    // Subdivide the outer fork into two fine woody shoots.
                    for(int i=0;i<2;++i) {
                        const auto fineKey=hash64(twigKey+static_cast<unsigned>(17+i));
                        const auto start=twig.at(.42+.36*i);
                        const auto dir=radial(forkAngle+(i==0 ? 1.25 : -1.15));
                        const auto split=start+dir*(shoot*.30)+vertical*(shoot*(hanging ? -.19 : .14));
                        const auto top=split+dir*(shoot*.20)+vertical*(shoot*(hanging ? -.27 : .22));
                        twigMesh({start,split,top},plant.collisionRadius()*.068);
                        spray(top,normalized(dir+vertical*pitch)*(shoot*.77),shoot*.32,kind,fineKey,true,false);
                    }
                }
                if(flowering && fork==0) joints.floweringTips.push_back(end);
            }
        }
    }

    void conifer(const Limb& trunk) {
        const bool tall=plant.species==Species::Redwood,pine=plant.species==Species::Pine;
        const double skirt=tall ? .44 : pine ? .31 : .17;
        const int layers=plant.plant.stage==GrowthStage::Seedling ? 3 : 6;
        for(int layer=0;layer<layers;++layer) {
            const auto whorl=hash64(seed+static_cast<unsigned>(layer));
            const double fraction=static_cast<double>(layer)/(layers-1);
            const double turn=goldenAngle*layer+randomUnit(whorl)*.76;
            const int spokes=plant.plant.stage==GrowthStage::Seedling ? 4 : 6;
            for(int spoke=0;spoke<spokes;spoke+=(far ? 2 : 1)) {
                const auto key=hash64(whorl+static_cast<unsigned>(spoke));
                const double angle=turn+tau*spoke/spokes+.25*(randomUnit(key)-.5);
                const auto dir=radial(angle);
                const auto root=trunk.at(mix(skirt,.97,fraction)+.025*(randomUnit(key+1)-.5));
                const double span=r*(1-.79*fraction)*(.85+.28*randomUnit(key+2));
                const double lift=pine ? .20 : -.18;
                const auto elbow=root+dir*(span*.50)+vertical*(span*lift);
                const auto tip=elbow+radial(angle+(randomUnit(key+4)-.5)*.31)*(span*.46)+vertical*(span*.12);
                const Limb bough{root,elbow,tip};
                if(far) {
                    if(plant.plant.stage==GrowthStage::Dead || plant.plant.foliage<=0) {
                        twigMesh(bough,h*.009*(1-.65*fraction));
                    } else {
                        // The texture carries the lateral shoots at this scale.
                        // Keep the woody attachment at the same whorl as the near bough.
                        const auto attach=root+(tip-root)*.28;
                        const auto half=vertical*(h*.009*(1-.65*fraction));
                        woodFace(root-half,root+half,tip,false,true);
                        spray(attach,tip-attach+dir*(span*.32),span*.54,LeafSpray::Needles,key,false,spoke%4==0);
                    }
                    continue;
                }
                limbMesh(bough,h*.009*(1-.65*fraction),3,false);
                for(int pair=0;pair<2;++pair) for(int sign:{-1,1}) {
                    const auto attach=bough.at(.25+pair*.43);
                    const auto rake=radial(angle+sign*(.52+.23*randomUnit(key+5)));
                    const double size=span*.79*(1-.22*pair);
                    const auto extent=rake*size+vertical*(size*(pine ? .24 : -.19));
                    spray(attach,extent,size*.34,LeafSpray::Needles,key+static_cast<unsigned>(pair*2+sign+1),false,pair==0 && sign==1 && spoke%2==0);
                }
                if(extra) spray(tip,dir*(span*.37)+vertical*(span*.30),span*.16,LeafSpray::Needles,key+31,true,false);
            }
        }
    }
};
}
BranchJoints buildBranches(const SurfaceObject& plant,Color bark,Color leaves,bool simple,bool fine,
                           std::vector<MeshFace>& faces,std::vector<Triangle>* sprayShadows) {
    return BranchBuilder(plant,bark,leaves,simple,fine,faces,sprayShadows).grow();
}
} // namespace planet
