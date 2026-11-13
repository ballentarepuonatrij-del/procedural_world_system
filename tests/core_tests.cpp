#include "planet/navigation.hpp"
#include "planet/weather.hpp"
#include "planet/hydrology.hpp"
#include "planet/sound.hpp"
#include "planet/preferences.hpp"
#include "planet/globe_camera.hpp"
#include "planet/lighting.hpp"
#include "planet/phenomena.hpp"
#include "planet/state.hpp"
#include "planet/shadows.hpp"
#include "planet/rivers.hpp"
#include "planet/terrain_mesh.hpp"
#include "planet/landscape.hpp"
#include <future>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace planet;
void require(bool condition,const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}
void near(double actual,double expected,double tolerance,const std::string& message) {
    if(!std::isfinite(actual) || std::abs(actual-expected)>tolerance)
        throw std::runtime_error(message+": actual="+std::to_string(actual)+", expected="+std::to_string(expected));
}
struct Test { std::string suite,name; std::function<void()> run; };
std::vector<Test> tests;
void test(std::string suite,std::string name,std::function<void()> run) { tests.push_back({std::move(suite),std::move(name),std::move(run)}); }
Vec3 point(int i,int count) { return direction(std::asin(1-2*(i+0.5)/count),i*pi*(3-std::sqrt(5.0))); }
void walkTo(Explorer& explorer,const World& world,Vec3 target,const Hydrology* water=nullptr) {
    for(int i=0;i<700;++i) {
        if(length(target-explorer.up())*world.config().radius<1.7) return;
        const Vec3 forward=normalized(target-explorer.up()*dot(target,explorer.up()));
        const double heading=std::atan2(dot(forward,east(explorer.up())),dot(forward,north(explorer.up())));
        explorer.look(std::remainder(heading-explorer.heading(),tau),0);
        explorer.update(world,{1,0,0,true,false},0.2,water);
    }
    throw std::runtime_error("Walkable cave route was blocked");
}
#include "landscape_tests.hpp"
#include "fauna_tests.hpp"
#include "snow_tests.hpp"
#include "biome_tests.hpp"
#include "phenomena_tests.hpp"
#include "state_tests.hpp"
#include "material_tests.hpp"
void registerTests() {
    registerMaterialTests();
    registerLandscapeTests();
    registerFaunaTests();
    registerSnowTests();
    registerBiomeTests();
    registerPhenomenaTests();
    registerStateTests();
    test("detail","CPU terrain snapshots preserve coordinates and bound flight detail",[] {
        World w;const auto p=w.findLand();Hydrology water(w,p);const double height=w.surface(p)+1.75;
        const auto fine=makeTerrainMesh(w,water,p,height,79*86400),fast=makeTerrainMesh(w,water,p,height,79*86400,64);
        require(!fine.terrain.empty() && fast.terrain.size()<fine.terrain.size(),"Flight LOD bounds geometry work");
        require(!fine.plants.empty() && fast.plants.empty(),"Full local plants return at walking detail");
        for(const auto* mesh:{&fine,&fast}) {
            require(mesh->terrain.size()%3==0 && mesh->caves.size()%3==0 && mesh->grid.size()%2==0,"Complete primitives");
            near(length(mesh->anchor-p),0,0,"Snapshot anchor stays at the observer");
            for(const auto& v:mesh->terrain) require(std::isfinite(length(v.position)) && std::isfinite(length(v.normal)),"Finite mesh data");
            for(const auto& tri:mesh->waterTriangles) for(auto i:tri) require(i<mesh->waterVertices.size(),"Water indices refer to this snapshot");
        }
        near(w.surface(p)+1.75,height,0,"Render LOD does not change the physical surface");
        for(double bad:{0.,-2.,9000.,std::numeric_limits<double>::quiet_NaN()}) {
            bool rejected=false;try {(void)terrainSpacing(w,p,height,bad);} catch(const std::invalid_argument&) {rejected=true;}
            require(rejected,"Invalid mesh density is rejected before allocating geometry");
        }
    });
    test("detail","A mesh worker owns its world and ground snapshot across world changes",[] {
        World live;const auto p=live.findLand();Hydrology water(live,p);const double height=live.surface(p)+1.75;
        const auto expected=makeTerrainMesh(live,water,p,height,79*86400,64);
        auto job=std::async(std::launch::async,[copy=live,ground=water,p,height] {return makeTerrainMesh(copy,ground,p,height,79*86400,64);});
        live.modules().set(Module::Terrain,false);water.synchronize(live,p);
        Config config;config.seed=73;live=World(config);
        const auto actual=job.get();require(actual.terrain.size()==expected.terrain.size(),"World replacement cannot alter an in-flight mesh");
        for(std::size_t i=0;i<actual.terrain.size();++i) {
            near(length(actual.terrain[i].position-expected.terrain[i].position),0,0,"Stable snapshot vertices");
            near(actual.terrain[i].color.r,expected.terrain[i].color.r,0,"Stable snapshot colours");
        }
    });
    test("rivers","Immutable river atlases support independent concurrent sampling",[] {
        World w;const auto atlas=w.waterNetwork();const auto site=atlas->find(RiverKind::River,w.findLand());
        std::array<Vec3,64> points{};std::array<RiverSample,64> expected{};
        for(std::size_t i=0;i<points.size();++i) {
            points[i]=normalized(site.center+east(site.center)*((static_cast<double>(i)-32)*2/w.config().radius));expected[i]=atlas->sample(points[i]);
        }
        const auto sample=[&](std::size_t offset) {
            for(std::size_t i=0;i<1200;++i) {
                const auto j=(i*17+offset)%points.size();const auto a=atlas->sample(points[j]),b=atlas->sample(points[j]);
                near(a.bed,expected[j].bed,0,"Concurrent river bed");near(b.discharge,expected[j].discharge,0,"Thread-local repeated-query cache");
            }
        };
        auto first=std::async(std::launch::async,sample,0);auto second=std::async(std::launch::async,sample,31);
        first.get();second.get();
    });
    test("flora","Flowering belongs to angiosperms and new meadow herbs have distinct forms",[] {
        for(auto s:{Species::Pine,Species::Juniper,Species::Fern,Species::Kelp,Species::Rockweed})
            require(flowerTraits(s).form==FlowerForm::None,"Conifers ferns and algae do not produce flowers");
        require(flowerTraits(Species::Seagrass).form!=FlowerForm::None,"Seagrass is an underwater flowering plant");
        std::array<bool,8> forms{};
        for(unsigned i=static_cast<unsigned>(Species::Daisy);i<=static_cast<unsigned>(Species::Buttercup);++i) {
            const auto s=static_cast<Species>(i);const auto& f=flowerTraits(s);
            require(!plantTraits(s).aquatic && f.radius>0 && f.reproductiveDays>0,"Terrestrial reproductive herbs");
            forms[static_cast<unsigned>(f.form)]=true;
        }
        require(std::count(forms.begin(),forms.end(),true)>=4,"Different floral architectures");
    });
    test("flora","Thermal time agrees with numerical integration of positive temperature excess",[] {
        constexpr double year=365.25,days=281.3;
        for(double mean:{-3.,6.,20.}) for(double amplitude:{0.,10.,20.}) {
            double numeric=0;constexpr int count=20000;
            for(int i=0;i<count;++i) numeric+=std::max(0.,mean-amplitude*std::cos(tau*(i+.5)*days/count/year)-5)*days/count;
            near(growingDegreeDays(days,year,mean,amplitude,5),numeric,.0001,"Growing degree days integrate only warmth above the base");
        }
        near(growingDegreeDays(100,year,-10,3,5),0,0,"Frozen season contributes no thermal time");
    });
    test("flora","Seasonal bloom shifts between hemispheres and advances in warmer climates",[] {
        Config c;c.eccentricity=0;Sample climate;climate.temperature=288;climate.precipitation=1000;
        const auto n=flowerCycle(Species::Daisy,42,c,direction(radians(45),0),climate);
        const auto s=flowerCycle(Species::Daisy,42,c,direction(radians(-45),0),climate);
        require(n.enabled && s.enabled,"Temperate flowers have a reproductive window");
        near(std::abs(std::remainder(n.winterDay-s.winterDay,n.yearDays)),n.yearDays/2,1e-8,"Opposite thermal seasons");
        PlantState adult;adult.ageDays=900;adult.size=adult.foliage=1;adult.stage=GrowthStage::Mature;
        const double date=n.winterDay+(n.openDay+n.closeDay)*.5;
        require(flowerState(Species::Daisy,adult,n,date).bloom>.9 && flowerState(Species::Daisy,adult,s,date).bloom==0,"Summer bloom is absent in opposite season");
        climate.temperature+=4;const auto warm=flowerCycle(Species::Daisy,42,c,direction(radians(45),0),climate);
        require(warm.openDay<n.openDay,"A warmer reference climate reaches heat requirement earlier");
        climate.temperature=245;require(!flowerCycle(Species::Daisy,42,c,direction(radians(45),0),climate).enabled,"Insufficient yearly heat prevents flowering");
    });
    test("flora","Buds flowers and seed heads are ordered without juvenile or retroactive reproduction",[] {
        Config c;Sample climate;climate.temperature=289;climate.precipitation=1000;
        const auto cycle=flowerCycle(Species::Clover,91,c,direction(radians(40),0),climate);
        PlantState p;p.ageDays=600;p.size=p.foliage=1;p.stage=GrowthStage::Mature;
        const auto at=[&](double day){return flowerState(Species::Clover,p,cycle,cycle.winterDay+day);};
        require(at((cycle.budDay+cycle.openDay)*.5).phase==FlowerPhase::Budding,"Buds precede open flowers");
        require(at((cycle.openDay+cycle.closeDay)*.5).phase==FlowerPhase::Flowering,"Flowering window");
        const double fruit=(cycle.closeDay+cycle.endDay)*.5;require(at(fruit).phase==FlowerPhase::Fruiting,"Seed heads follow flowering");
        require(at(0).phase==FlowerPhase::Dormant,"Winter is vegetative");
        p.stage=GrowthStage::Seedling;require(at((cycle.openDay+cycle.closeDay)*.5).bloom==0,"Seedlings cannot flower");
        p.stage=GrowthStage::Dead;require(at(fruit).seeds==0,"Dead plant has no active reproduction");
        p.stage=GrowthStage::Mature;p.ageDays=flowerTraits(Species::Clover).reproductiveDays+1;
        require(at(fruit).seeds==0,"A plant which matured after flowering cannot acquire earlier seed heads");
    });
    test("flora","Flower envelopes are continuous periodic and independent of query order",[] {
        Config c;Sample climate;climate.temperature=288;climate.precipitation=1000;
        const auto cycle=flowerCycle(Species::Daisy,7,c,direction(radians(40),3.14),climate);
        PlantState p;p.ageDays=1000;p.size=p.foliage=1;p.stage=GrowthStage::Mature;
        for(double boundary:{cycle.budDay,cycle.openDay,cycle.closeDay,cycle.endDay,cycle.yearDays}) {
            const auto a=flowerState(Species::Daisy,p,cycle,cycle.winterDay+boundary-1e-5),b=flowerState(Species::Daisy,p,cycle,cycle.winterDay+boundary+1e-5);
            require(std::abs(a.buds-b.buds)+std::abs(a.bloom-b.bloom)+std::abs(a.seeds-b.seeds)<1e-4,"No abrupt stage geometry change");
        }
        const double day=cycle.winterDay+(cycle.openDay+cycle.closeDay)*.5;
        const auto a=flowerState(Species::Daisy,p,cycle,day),b=flowerState(Species::Daisy,p,cycle,day+cycle.yearDays);
        near(a.bloom,b.bloom,1e-12,"Annual climate cycle repeats for a reproductive plant");
        c.obliquity=0;require(flowerCycle(Species::Daisy,7,c,{0,1,0},climate).enabled,"Zero obliquity and poles remain defined");
    });
    test("flora","Biological calendar freezes with growth and accelerates without changing physical clocks",[] {
        World w;Simulation s(w,w.findLand(),79*86400);s.flora.setRate(86400);const double weather=s.weather.seconds();
        s.advance(w,w.findLand(),2);near(s.flora.calendarDay(),81,1e-12,"Two experimental biological days");
        near(s.weather.seconds()-weather,2,1e-12,"Atmosphere advances only two seconds");
        w.modules().set(Module::PlantGrowth,false);s.advance(w,w.findLand(),2);near(s.flora.calendarDay(),81,0,"Growth switch freezes phenology");
        s.setClock(10000);near(s.flora.calendarDay(),81,0,"Lighting clock does not jump plant seasons");
        const double age=s.flora.days();s.flora.setCalendarDay(180);near(s.flora.days(),age,0,"Season selection does not change plant age");
    });
    test("flora","Flower meshes have finite nondegenerate geometry and seasonal differences",[] {
        for(unsigned i=static_cast<unsigned>(Species::Daisy);i<=static_cast<unsigned>(Species::Buttercup);++i) {
            SurfaceObject p;p.id=71;p.kind=DetailKind::Grass;p.species=static_cast<Species>(i);p.height=plantTraits(p.species).height;p.radius=p.height*.35;
            p.plant.size=p.plant.foliage=1;p.plant.stage=GrowthStage::Mature;
            const auto dormant=detailMesh(p,{}, {0,1,0},{1,0,0},false);p.flowers.bloom=1;
            const auto open=detailMesh(p,{}, {0,1,0},{1,0,0},false);require(open.size()>dormant.size(),"Flower heads add visible geometry");
            std::vector<Triangle> caster;
            for(const auto& face:open) {const auto& t=face.triangle;require(std::isfinite(length(t.a)) && length(cross(t.b-t.a,t.c-t.a))>1e-10,"Valid floral triangle");caster.push_back(t);}
            require(!shadowVolume(caster,normalized(Vec3{.2,1,.3})).empty(),"Visible flowers share the shadow geometry");
        }
    });
    test("flora","Meadow route exposes several rooted flowering species at stable sites",[] {
        World w;const auto place=findMeadow(w);const auto sites=surfaceSites(w,place.position,60);
        std::array<bool,speciesCount> kinds{};Flora a,b;a.setCalendarDay(130);b.setCalendarDay(280);unsigned observed=0;
        for(const auto& site:sites) if(site.species>=Species::Daisy) {
            kinds[static_cast<unsigned>(site.species)]=true;const auto first=grownObject(site,&a),second=grownObject(site,&b);
            near(length(first.up-second.up),0,0,"Season change keeps roots fixed");near(first.height,second.height,0,"Season preview does not age plants");++observed;
        }
        require(observed>12 && std::count(kinds.begin(),kinds.end(),true)>=4,"Several new meadow herbs coexist in a suitable habitat");
    });
    test("rivers","Drainage is acyclic and accounts for all reference runoff at outlets",[] {
        World w;const auto atlas=w.waterNetwork();const auto& nodes=atlas->nodes();double rain=0,outflow=0,area=0;
        std::vector<double> incoming(nodes.size());
        for(const auto& n:nodes) {
            rain+=n.runoff;require(n.area>0 && n.discharge>=n.runoff,"Positive catchment and accumulated runoff");
            if(n.downstream>=0) {const auto& parent=nodes[static_cast<std::size_t>(n.downstream)];require(parent.filled<n.filled,"Strictly earlier receiver excludes cycles");incoming[static_cast<std::size_t>(n.downstream)]+=n.discharge;}
            else {outflow+=n.discharge;area+=n.area;}
        }
        for(std::size_t i=0;i<nodes.size();++i) near(nodes[i].discharge,nodes[i].runoff+incoming[i],1e-7,"Confluence conserves volumetric discharge");
        near(rain,outflow,rain*1e-12,"All runoff reaches a terminal");near(atlas->runoff(),atlas->oceanDischarge(),rain*1e-12,"Atlas budget");
        near(area,4*pi*w.config().radius*w.config().radius,area*1e-12,"Spherical cell areas cover the planet once");
        require(atlas->reaches().size()>1000,"A planet-wide drainage network exists");
    });
    test("rivers","Longitudinal stages descend and meet at shared junctions",[] {
        World w;const auto atlas=w.waterNetwork();
        for(const auto& reach:atlas->reaches()) {
            const auto& n=atlas->nodes()[reach.node];const auto& p=reach.points;
            near(p.front().level,n.level,1e-9,"Shared upstream stage");near(p.back().level,atlas->nodes()[static_cast<std::size_t>(n.downstream)].level,1e-9,"Shared downstream stage");
            for(std::size_t i=1;i<p.size();++i) {require(p[i].level<=p[i-1].level+1e-9 && p[i].along>p[i-1].along,"Downhill flow and positive reach length");require(p[i].width>0 && p[i].depth>0,"Finite wet cross section");}
        }
    });
    test("rivers","Manning sections carry the requested volume and bound fast flow",[] {
        for(double q:{.02,.1,1.,30.,500.,20000.}) for(double slope:{.00002,.001,.02}) {
            const auto s=riverSection(q,slope);near(.8*s.width*s.depth*s.speed,q,q*1e-12,"Q equals area times mean speed");require(s.speed<=6.000001,"Bounded channel speed");
            if(s.speed<5.99) {const double area=.8*s.width*s.depth,perimeter=.6*s.width+2*std::hypot(.2*s.width,s.depth);near(area*std::pow(area/perimeter,2./3)*std::sqrt(slope)/.035,q,q*1e-8,"Manning conveyance");}
        }
        require(riverSection(0,.01).depth==0,"Zero runoff is dry");bool bad=false;try{(void)riverSection(-1,.01);}catch(const std::invalid_argument&){bad=true;}require(bad,"Negative flow rejected");
    });
    test("rivers","Stream river and major river have continuous wet cross sections",[] {
        World w;const auto atlas=w.waterNetwork();double previous=0;
        for(auto kind:{RiverKind::Stream,RiverKind::River,RiverKind::Major}) {
            const auto site=atlas->find(kind,w.findLand());const auto r=atlas->sample(site.center);
            require(r.wet && r.width>previous && r.speed>0 && r.speed<=6.000001,"Increasing channel sizes with bounded moving water");previous=r.width;
            const auto liquid=liquidAt(w,nullptr,nullptr,site.center);require(liquid.present && liquid.river && !liquid.ocean,"River is fresh surface water");near(liquid.level,r.level,1e-8,"Navigation and atlas share water stage");
            const auto crossBank=normalized(cross(r.downstream,site.center));double area=0;constexpr int count=1000;
            for(int i=0;i<count;++i) {const auto p=normalized(site.center+crossBank*((-.5+(i+.5)/count)*r.width/w.config().radius));const auto s=atlas->sample(p);require(s.wet,"No hole across the wet channel");area+=s.depth*r.width/count;}
            near(area*r.speed,r.discharge,std::max(.001,r.discharge*.025),"Integrated cross-section flux agrees with Q");
            require(!atlas->sample(normalized(site.center+crossBank*(r.width/w.config().radius))).wet,"Bank outside the channel is dry");
        }
    });
    test("rivers","River current carries a swimmer and switching off motion stops the forcing",[] {
        World w;const auto site=w.waterNetwork()->find(RiverKind::Major,w.findLand());Simulation sim(w,site.center,0,0);Explorer e(w);
        e.teleport(w,site.center,&sim.water);const auto origin=e.up();const auto flow=liquidAt(w,&sim.water,&sim.motion,origin).current;
        for(int i=0;i<40;++i) e.update(w,{},.1,&sim.water,&sim.motion);
        require(e.swimming() && dot(e.up()-origin,flow)>0 && length(e.up()-origin)*w.config().radius>0.05,"Passive downstream drift");
        w.modules().set(Module::WaterMotion,false);sim.advance(w,e.up(),0);near(length(liquidAt(w,&sim.water,&sim.motion,e.up()).current),0,0,"Motion toggle removes river velocity");
        w.modules().set(Module::Rivers,false);require(!liquidAt(w,nullptr,nullptr,site.center).river,"River toggle removes analytical water");
    });
    test("rivers","Local runoff exchanges with a prescribed river without losing budget",[] {
        RunoffGrid grid(8,4);for(auto& c:grid.cells){c.rock=0;c.soil=c.initialSoil=0;c.conductivity=0;c.water=.1;}
        for(int z=0;z<8;++z) {auto& c=grid.cells[static_cast<std::size_t>(z*8+7)];c.reservoir=true;c.reservoirLevel=.4;}
        const double before=grid.waterVolume();const std::vector<double> rain(64,0);const auto change=grid.advance(5,rain,0,false,false,false,0);
        require(change.boundaryM3<0 && grid.waterVolume()>before,"Prescribed stage supplies neighbouring land cells even with ocean disabled");
        near(grid.waterVolume()+change.boundaryM3,before,1e-9,"River boundary exchange closes local volume budget");
    });
    test("rivers","Atlas is deterministic and toggles restore channel geometry",[] {
        World a,b;const auto site=a.waterNetwork()->find(RiverKind::River,a.findLand());
        near(a.elevation(site.center),b.elevation(site.center),0,"Same seed reproduces bed");
        const double bed=a.elevation(site.center);a.modules().set(Module::Rivers,false);const double natural=a.elevation(site.center);
        require(std::abs(natural-bed)>.01,"Enabled rivers condition the initial terrain");a.modules().set(Module::Rivers,true);near(a.elevation(site.center),bed,0,"Re-enabling restores original channel");
        const auto atlas=a.waterNetwork();for(const auto p:std::array{direction(0,pi-1e-12),direction(0,-pi+1e-12),Vec3{0,1,0},Vec3{0,-1,0}}) require(std::isfinite(a.elevation(p)) && std::isfinite(length(atlas->oceanCurrent(p))),"Seam and poles remain finite");
        require(serializePreferences(Preferences{}).find("rivers=1")!=std::string::npos,"River preference persists and defaults on");
    });
    test("rivers","Global ocean circulation is tangent bounded and fixed to the planet",[] {
        World w;const auto atlas=w.waterNetwork();require(atlas->circulationResidual()<1e-6,"Steady streamfunction solve converged");double fastest=0;Vec3 sea{};
        for(int i=0;i<400;++i) {const auto p=point(i,400),v=atlas->oceanCurrent(p);near(dot(p,v),0,1e-12,"Tangent ocean flow");fastest=std::max(fastest,length(v));if(length(v)>.01) sea=p;if(w.naturalElevation(p)>0) near(length(v),0,0,"No ocean current on land");}
        require(fastest>.02 && fastest<2,"Nonzero bounded basin circulation");
        const auto surface=atlas->oceanCurrent(sea),deep=atlas->oceanCurrent(sea,80);near(length(deep)/length(surface),std::exp(-1.),1e-10,"Prescribed mixed-layer attenuation");
        World again;near(length(surface-again.waterNetwork()->oceanCurrent(sea)),0,0,"Moving observers do not reset the reference current");
    });
    test("lighting","Solar visibility and direct light vanish on the night side",[] {
        Config c;c.obliquity=0;c.eccentricity=0;const Vec3 p{1,0,0};
        const auto noon=lighting(c,p,solarHourTime(c,p,79*c.day,12));
        const auto night=lighting(c,p,solarHourTime(c,p,79*c.day,0));
        near(noon.elevation,pi/2,1e-7,"Solar noon above equator");near(night.elevation,-pi/2,1e-7,"Midnight below globe");
        require(noon.direct>.7 && noon.starVisibility==0,"Sunlight and daylight hide stars");
        SkyConditions mountain;mountain.altitude=6000;
        require(lighting(c,p,solarHourTime(c,p,79*c.day,12),mountain).starVisibility==0,"Daytime stars remain hidden on high mountains");
        require(night.direct==0 && night.starVisibility>.99,"No direct sunlight at midnight");
        require(night.ambient.b>night.ambient.r*2 && night.ambient.b<.75,"Adapted cold night fill remains below full daylight");
        require(night.starlightLux<.001 && night.starlightLux>0,"Moonless starlight scale");
    });
    test("lighting","Display adaptation preserves visibility through night twilight and storms",[] {
        Config c;c.obliquity=0;c.eccentricity=0;const Vec3 p{1,0,0};
        for(double cloud:{0.,.2,.8}) for(int hour=0;hour<48;++hour) {
            SkyConditions sky;sky.cloudWater=sky.sunCloudWater=cloud;sky.rainMmH=cloud*60;
            const auto l=lighting(c,p,solarHourTime(c,p,0,hour*.5),sky);
            const double gray=.2126*(l.ambient.r+l.sunlight.r*std::max(0.,std::sin(l.elevation)))+
                .7152*(l.ambient.g+l.sunlight.g*std::max(0.,std::sin(l.elevation)))+
                .0722*(l.ambient.b+l.sunlight.b*std::max(0.,std::sin(l.elevation)));
            require(gray>.18,"Display illumination has no black interval between day and night");
            if(hour==24 && cloud>.7) require(gray>.55,"Overcast noon retains diffuse visibility");
        }
    });
    test("lighting","Solar time selection follows geography and keeps the existing orbital clock",[] {
        Config c;
        for(const auto p:std::array{direction(.7,3.13),direction(-.8,-3.13),direction(0,0)}) for(double hour:{0.,6.,9.,12.,18.,24.}) {
            const double time=solarHourTime(c,p,79*c.day,hour);
            const double angle=std::remainder(longitude(p)-longitude(orbit(c,time).sunDirection),tau);
            near(std::remainder(angle-(hour-12)*tau/24,tau),0,1e-10,"Requested apparent hour angle");
        }
        const auto a=lighting(c,{1,0,0},0),b=lighting(c,{1,0,0},c.year/2);
        require(a.angularRadius>b.angularRadius,"Sun is larger at perihelion");
        near(a.angularRadius*orbit(c,0).distanceAU,.00465047,1e-12,"Physical solar angular radius");
    });
    test("lighting","Air mass reddens the Sun and clouds suppress direct light and stars",[] {
        near(relativeAirMass(pi/2),.99971199,1e-7,"Zenith air mass reference");
        require(relativeAirMass(0)>35 && relativeAirMass(radians(10))>relativeAirMass(radians(45)),"Longer path near horizon");
        Config c;c.obliquity=0;c.eccentricity=0;const Vec3 p{1,0,0};const double time=solarHourTime(c,p,0,6.15);
        const auto clear=lighting(c,p,time);SkyConditions weather;weather.cloudWater=.6;weather.sunCloudWater=.6;weather.rainMmH=30;
        const auto overcast=lighting(c,p,time,weather);
        require(clear.sunlight.r>clear.sunlight.b*3,"Atmospheric extinction makes low Sun red");
        require(overcast.direct<clear.direct*.01,"Clouds remove the direct beam and its shadows");
        const auto starry=lighting(c,p,solarHourTime(c,p,0,0)),cloudy=lighting(c,p,solarHourTime(c,p,0,0),weather);
        require(cloudy.starVisibility<starry.starVisibility*.01 && cloudy.starlightLux<starry.starlightLux*.01,"Clouds hide stars and attenuate their illumination");
    });
    test("lighting","Sky transitions include blue day warm twilight and airless black",[] {
        Config c;c.obliquity=0;const Vec3 p{1,0,0};
        const auto noon=lighting(c,p,solarHourTime(c,p,0,12)),dawn=lighting(c,p,solarHourTime(c,p,0,6));
        const auto blue=skyColor(noon,p),warm=skyColor(dawn,normalized(dawn.sun-p*dot(p,dawn.sun)));
        require(blue.b>blue.r && warm.r>warm.b*2,"Day and solar horizon have distinct spectral tendencies");
        SkyConditions vacuum;vacuum.atmosphere=false;const auto space=lighting(c,p,solarHourTime(c,p,0,12),vacuum);
        const auto black=skyColor(space,p);require(black.r==0 && black.g==0 && black.b==0 && space.starVisibility==1,"No scattering in vacuum, stars remain visible");
        const double dusk=solarHourTime(c,p,0,18);
        const auto a=lighting(c,p,dusk-.01),b=lighting(c,p,dusk+.01);
        require(std::abs(a.direct-b.direct)<.001 && std::abs(a.ambient.b-b.ambient.b)<.001,"Continuous horizon crossing");
    });
    test("lighting","Stars are deterministic isotropic and rotate at the sidereal rate",[] {
        const auto a=starCatalog(42),b=starCatalog(42),different=starCatalog(43);Vec3 mean{};
        for(std::size_t i=0;i<a.size();++i) {near(length(a[i].inertialDirection),1,1e-12,"Unit star direction");near(length(a[i].inertialDirection-b[i].inertialDirection),0,0,"Stable catalog");mean+=a[i].inertialDirection;}
        require(length(mean/static_cast<double>(a.size()))<.04 && length(a[0].inertialDirection-different[0].inertialDirection)>.01,"Sphere coverage and seed variation");
        Config c;const double period=1/(1/c.day+1/c.year);
        const auto p=a[18].inertialDirection;
        near(length(inertialToPlanet(p,c,period)-p),0,1e-12,"One sidereal revolution");
        require(length(inertialToPlanet({1,0,0},c,c.day)-Vec3{1,0,0})>.01,"Solar day is not sidereal day");
        near(length(inertialToPlanet({0,1,0},c,123456)-Vec3{0,1,0}),0,0,"Celestial pole is fixed");
    });
    test("lighting","Invalid sky controls are rejected",[] {
        Config c;bool rejected=false;try {(void)solarHourTime(c,{1,0,0},0,25);}catch(const std::invalid_argument&) {rejected=true;}require(rejected,"Invalid hour");
        rejected=false;try {(void)starCatalog(42,20001);}catch(const std::invalid_argument&) {rejected=true;}require(rejected,"Bounded catalog");
        SkyConditions bad;bad.sunCloudWater=-1;rejected=false;try {(void)lighting(c,{1,0,0},0,bad);}catch(const std::invalid_argument&) {rejected=true;}require(rejected,"Invalid column water");
    });
    test("shadows","A shared-edge square produces a closed prism without an internal wall",[] {
        const std::array<Triangle,2> square{{{{-1,2,-1},{-1,2,1},{1,2,1}},{{-1,2,-1},{1,2,1},{1,2,-1}}}};
        const auto volume=shadowVolume(square,{0,1,0},10,0);require(volume.size()==12,"Two caps and four side quads");
        using Point=std::array<long long,3>;using Edge=std::pair<Point,Point>;std::map<Edge,int> edges;
        const auto key=[](Vec3 p){return Point{std::llround(p.x*1e6),std::llround(p.y*1e6),std::llround(p.z*1e6)};};
        for(const auto& t:volume) {const std::array p{key(t.a),key(t.b),key(t.c)};for(unsigned i=0;i<3;++i) {const auto a=p[i],b=p[(i+1)%3];edges[{std::min(a,b),std::max(a,b)}]+=a<b ? 1 : -1;}}
        require(std::all_of(edges.begin(),edges.end(),[](const auto& e){return e.second==0;}),"Every edge has an opposite winding, including far caps");
    });
    test("shadows","Shadow location follows height divided by solar elevation tangent",[] {
        const std::array<Triangle,2> square{{{{-1,2,-1},{-1,2,1},{1,2,1}},{{-1,2,-1},{1,2,1},{1,2,-1}}}};
        const auto inside=[](const auto& volume,Vec3 origin) {
            const Vec3 ray=normalized(Vec3{.31,.72,.53});int winding=0;
            for(const auto& f:volume) {
                const auto e1=f.b-f.a,e2=f.c-f.a,h=cross(ray,e2);const double det=dot(e1,h);if(std::abs(det)<1e-10) continue;
                const auto s=origin-f.a;const double u=dot(s,h)/det;if(u<0 || u>1) continue;
                const auto q=cross(s,e1);const double v=dot(ray,q)/det,t=dot(e2,q)/det;
                if(v<0 || u+v>1 || t<=1e-8) continue;
                winding+=dot(cross(e1,e2),ray)>0 ? 1 : -1;
            }
            return winding!=0;
        };
        for(double h:{radians(15),radians(45),radians(75)}) {
            const auto volume=shadowVolume(square,{std::cos(h),std::sin(h),0},30,0);
            const double offset=2/std::tan(h);
            require(inside(volume,{-offset,0,.17}),"Shadow lies opposite the Sun at analytic distance");
            require(!inside(volume,{-offset-2.2,0,.17}) && !inside(volume,{2,0,.17}),"Outside silhouette remains sunlit");
            require(!inside(volume,{0,3,.17}),"Point above caster is not in shadow");
            const auto biased=shadowVolume(square,{std::cos(h),std::sin(h),0},30);
            require(!inside(biased,{0,2,.17}),"Lit surface must not shadow itself after bias");
            require(inside(biased,{-offset,0,.17}),"Bias preserves the shadow on a receiver below the caster");
        }
    });
    test("shadows","Thin sheets cast on both sides and degenerate inputs are bounded",[] {
        Triangle leaf{{0,2,0},{1,2,0},{0,2,1},true};
        require(!shadowVolume(std::span(&leaf,1),{0,1,0}).empty(),"Back-facing leaf still casts a shadow");
        leaf.twoSided=false;require(shadowVolume(std::span(&leaf,1),{0,1,0}).empty(),"Closed solid uses only light-facing faces");
        Triangle empty{};require(shadowVolume(std::span(&empty,1),{0,1,0}).empty(),"Degenerate face omitted");
        bool rejected=false;try {(void)shadowVolume({},{});}catch(const std::invalid_argument&) {rejected=true;}require(rejected,"Zero light direction rejected");
    });
    test("shadows","Visible plant and rock meshes have matching closed solid casters",[] {
        for(const auto kind:{DetailKind::Rock,DetailKind::Tree,DetailKind::Shrub}) {
            SurfaceObject object;object.id=42;object.kind=kind;object.height=10;object.radius=3;object.evergreen=kind==DetailKind::Tree;
            object.species=kind==DetailKind::Tree ? Species::Pine : Species::None;
            const auto mesh=detailMesh(object,{}, {0,1,0},{1,0,0},false);require(!mesh.empty(),"Visible procedural geometry exists");
            std::vector<Triangle> faces;for(const auto& face:mesh) {faces.push_back(face.triangle);require(length(cross(face.triangle.b-face.triangle.a,face.triangle.c-face.triangle.a))>0,"No zero-area visible face");}
            const auto volume=shadowVolume(faces,normalized(Vec3{.4,1,.2}));require(!volume.empty(),"Same geometry casts directional shadows");
        }
    });
    test("preferences","Seed accepts the complete unsigned 64-bit range without rounding",[] {
        require(parseSeed("0")==0 && parseSeed("42")==42,"Zero and ordinary seed");
        require(parseSeed("18446744073709551615")==std::numeric_limits<std::uint64_t>::max(),"Full 64-bit precision");
        for(const auto value:{"","-1","+1"," 42","42 ","1.2","1e3","18446744073709551616","184467440737095516150","seed"})
            require(!parseSeed(value),"Invalid or overflowing seed must not be truncated");
    });
    test("preferences","Settings preserve requested dependencies and all user options",[] {
        Preferences p;require(!p.grid && p.interfaceVisible && p.volume==55 && p.seed==42,"First-launch defaults");
        p.seed=std::numeric_limits<std::uint64_t>::max();p.volume=0;p.grid=true;p.interfaceVisible=false;p.sky=false;p.shadows=false;
        p.modules.set(Module::Atmosphere,false);p.modules.set(Module::Sound,false);
        const auto restored=parsePreferences(serializePreferences(p));
        require(restored.seed==p.seed && restored.grid && !restored.interfaceVisible && restored.volume==0,"Options round-trip");
        require(!restored.sky && !restored.shadows,"Graphics preferences round-trip");
        for(unsigned i=0;i<moduleCount;++i) require(restored.modules.requested(static_cast<Module>(i))==p.modules.requested(static_cast<Module>(i)),"Requested switches round-trip");
        require(restored.modules.requested(Module::Climate) && !restored.modules.enabled(Module::Climate),"Waiting dependency retains its requested state");
        const auto partial=parsePreferences("# settings\r\n version = 1\r\nfuture_option=abc\r\nvolume=100\r\n");
        require(partial.volume==100 && !partial.grid && partial.sky && partial.shadows,"Old settings keep the new graphics defaults");
    });
    test("preferences","Malformed settings are rejected instead of applying partial state",[] {
        for(const auto value:{"grid=1", "version=2", "version=1\nvolume=101", "version=1\nvolume=-1", "version=1\ngrid=true", "version=1\nsound=7", "version=1\nseed=18446744073709551616", "version=1\nbroken"}) {
            bool rejected=false;try {(void)parsePreferences(value);}catch(const std::invalid_argument&) {rejected=true;}
            require(rejected,"Malformed settings rejected");
        }
        bool rejected=false;try {(void)parsePreferences(std::string(17000,' '));}catch(const std::invalid_argument&) {rejected=true;}
        require(rejected,"Bounded configuration file size");
    });
    test("preferences","Disk replacement survives invalid saves and reports unreadable data",[] {
        struct Directory {
            std::filesystem::path path=std::filesystem::temp_directory_path()/std::filesystem::path("planet-prefs-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            ~Directory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
        } directory;
        const auto file=directory.path/"nested/settings.ini";
        require(loadPreferences(file).warning.empty(),"Absent first-run file uses defaults");
        Preferences p;p.grid=true;p.volume=23;savePreferences(file,p);
        auto read=loadPreferences(file);require(read.warning.empty() && read.value.grid && read.value.volume==23,"Write and reopen");
        p.seed=0;p.volume=100;savePreferences(file,p);read=loadPreferences(file);
        require(read.warning.empty() && read.value.seed==0 && read.value.volume==100,"Replace existing file");
        p.volume=101;bool rejected=false;try {savePreferences(file,p);}catch(const std::exception&) {rejected=true;}
        require(rejected && loadPreferences(file).value.volume==100,"Failed validation preserves old file");
        p.volume=11;const auto blocked=directory.path/"blocked";std::filesystem::create_directory(blocked);
        {std::ofstream marker(blocked/"keep");marker << "keep";}
        rejected=false;try {savePreferences(blocked,p);}catch(const std::exception&) {rejected=true;}
        require(rejected && std::filesystem::exists(blocked/"keep"),"Failed replacement preserves existing destination");
        for(const auto& entry:std::filesystem::directory_iterator(directory.path)) require(entry.path().filename().string().find(".tmp-")==std::string::npos,"Temporary files cleaned after failure");
        {std::ofstream out(file);out << "version=1\ngrid=1\nvolume=bad\n";}
        read=loadPreferences(file);require(!read.warning.empty() && !read.value.grid && read.value.volume==55,"Corrupt file returns full defaults and a warning");
        std::ifstream untouched(file);std::string content((std::istreambuf_iterator<char>(untouched)),{});
        require(content.find("volume=bad")!=std::string::npos,"Load does not overwrite corrupt input");
    });
    test("globe_camera","Center picking matches the observer including both poles",[] {
        for(const auto p:std::array<Vec3,5>{{{0,1,0},{0,-1,0},{1,0,0},{-1,0,0},direction(.4,2.9)}}) {
            GlobeCamera camera;camera.center(p);
            for(const auto size:std::array<std::array<int,2>,3>{{{1280,800},{952,606},{600,900}}}) {
                const auto hit=camera.pick(size[0]/2.,size[1]/2.,size[0],size[1]);
                require(hit.has_value(),"Center hits planet");near(length(*hit-normalized(p)),0,1e-12,"Correct geographic center");
                near(dot(camera.outward,camera.up),0,1e-12,"Polar camera remains orthogonal");
            }
        }
    });
    test("globe_camera","Picking inverts the same perspective after rotation and resize",[] {
        for(const auto size:std::array<std::array<int,2>,2>{{{1280,800},{952,606}}}) {
            GlobeCamera camera;camera.center(direction(.3,-2.4));camera.drag(190,-110);
            unsigned hits=0;
            for(int i=0;i<300;++i) {
                const auto p=point(i,300);if(dot(p,camera.outward)<.4) continue;
                const auto relative=p-camera.outward*GlobeCamera::distance;
                const double depth=dot(relative,-camera.outward),scale=std::tan(GlobeCamera::fov/2);
                const double x=(1+dot(relative,camera.right())/(depth*scale*size[0]/size[1]))*size[0]/2;
                const double y=(1-dot(relative,camera.up)/(depth*scale))*size[1]/2;
                const auto hit=camera.pick(x,y,size[0],size[1]);require(hit.has_value(),"Projected visible point is selectable");
                near(length(*hit-p),0,1e-10,"Cursor returns the front surface, not the opposite hemisphere");++hits;
            }
            require(hits>50,"Broad geographic coverage");
        }
    });
    test("globe_camera","Dragging across poles remains stable and background cannot teleport",[] {
        GlobeCamera camera;const auto before=camera.outward;camera.drag(100,80);
        require(length(camera.outward-before)>.1,"Drag rotates camera");
        for(int i=0;i<20000;++i) camera.drag(i%2 ? 7 : -4,5);
        near(length(camera.outward),1,1e-12,"No radius drift");near(length(camera.up),1,1e-12,"Unit up vector");
        near(dot(camera.outward,camera.up),0,1e-12,"Orthonormal after many pole crossings");
        for(const auto xy:std::array<std::array<double,2>,4>{{{0,0},{1279,799},{-1,400},{1280,400}}})
            require(!camera.pick(xy[0],xy[1],1280,800),"Empty space and out-of-client clicks miss");
        require(!camera.pick(1,1,0,0) && !camera.pick(std::numeric_limits<double>::quiet_NaN(),0,1280,800),"Invalid viewport/input rejected");
    });
    test("sound","Acoustic propagation uses temperature and distance with physical units",[] {
        near(speedOfSound(293.15),343.23369365,.000001,"Dry-air sound speed at 20 C");
        near(thunderDelay(1000,293.15),1000/speedOfSound(293.15),1e-12,"Propagation in seconds");
        require(thunderDelay(1000,270)>thunderDelay(1000,310),"Cold air gives later thunder");
        for(double t:{149.,371.,std::numeric_limits<double>::quiet_NaN()}) {
            bool rejected=false;try {(void)speedOfSound(t);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Invalid temperature rejected");
        }
    });
    test("sound","Synthesis is deterministic and independent of output buffer partition",[] {
        SoundScene s;s.audible=true;s.wind=.8;s.rain=.6;s.water=.5;s.rustle=.4;
        SoundSynth a,b;a.setScene(s);b.setScene(s);std::vector<std::int16_t> x(48000),y(x.size());a.render(x);
        for(std::size_t i=0;i<y.size();) {const auto n=std::min<std::size_t>(734,y.size()-i);b.render(std::span(y).subspan(i,n));i+=n;}
        require(x==y,"Exactly the same PCM for different block lengths");
        s.seed=73;SoundSynth c(s.seed);c.setScene(s);c.render(y);require(x!=y,"Seed changes procedural microstructure");
    });
    test("sound","Quiet controls produce silence and the maximum mix stays below PCM clipping",[] {
        SoundSynth synth;std::vector<std::int16_t> pcm(96000);synth.render(pcm);
        require(std::all_of(pcm.begin(),pcm.end(),[](auto n){return n==0;}),"Initial scene is silent");
        SoundScene s;s.audible=true;s.volume=1;s.wind=s.rain=s.water=s.rustle=1;
        for(unsigned i=1;i<=8;++i) s.strikes.push_back({i,0,0,2,0});
        synth.setScene(s);synth.render(pcm);int peak=0;for(auto v:pcm)peak=std::max(peak,std::abs(static_cast<int>(v)));
        require(peak>1000 && peak<29491,"Audible bounded mix with eight overlapping thunder voices");
        require(synth.activeThunder()<=8,"Voice count is bounded");
    });
    test("sound","Wind intensity follows the physical control instead of a fixed recording",[] {
        const auto energy=[](const auto& pcm) {double sum=0;for(auto v:pcm) sum+=static_cast<double>(v)*v;return sum;};
        SoundScene s;s.audible=true;s.wind=.1;SoundSynth quiet;quiet.setScene(s);std::vector<std::int16_t> low(96000),high(low.size());quiet.render(low);
        s.wind=1;SoundSynth storm;storm.setScene(s);storm.render(high);
        require(energy(high)>energy(low)*25,"Strong wind has substantially greater energy");
    });
    test("sound","Stereo direction follows listener orientation",[] {
        SoundScene s;s.audible=true;s.wind=1;s.windPan=1;SoundSynth synth;synth.setScene(s);std::vector<std::int16_t> pcm(192000);synth.render(pcm);
        double left=0,right=0;for(std::size_t i=48000;i<pcm.size();i+=2){left+=static_cast<double>(pcm[i])*pcm[i];right+=static_cast<double>(pcm[i+1])*pcm[i+1];}
        require(right>left*1.8,"Wind on the right has greater right-channel energy");
        s.windPan=-1;SoundSynth reversed;reversed.setScene(s);reversed.render(pcm);left=right=0;
        for(std::size_t i=48000;i<pcm.size();i+=2){left+=static_cast<double>(pcm[i])*pcm[i];right+=static_cast<double>(pcm[i+1])*pcm[i+1];}
        require(left>right*1.8,"Turning changes the stereo balance");
    });
    test("sound","Underwater filtering reduces high frequencies beyond simple attenuation",[] {
        SoundScene s;s.audible=true;s.rain=1;s.volume=1;SoundSynth dry;dry.setScene(s);std::vector<std::int16_t> a(192000),b(a.size());dry.render(a);
        s.underwater=true;SoundSynth wet;wet.setScene(s);wet.render(b);
        const auto spectrum=[](const auto& pcm) {double energy=0,difference=0;for(std::size_t i=48002;i<pcm.size();i+=2) {
            const double v=pcm[i],d=v-pcm[i-2];energy+=v*v;difference+=d*d;}return std::array{energy,difference/energy};};
        const auto x=spectrum(a),y=spectrum(b);require(y[0]<x[0]*.1 && y[1]<x[1]*.15,"Water attenuates and removes high-frequency energy");
    });
    test("sound","Thunder arrives after travel time and repeated snapshots cannot replay it",[] {
        SoundScene s;s.audible=true;s.volume=1;s.temperature=293.15;s.strikes.push_back({91,speedOfSound(s.temperature),.7,1,0});
        SoundSynth synth;synth.setScene(s);std::vector<std::int16_t> pcm(192000);synth.render(pcm);
        require(std::all_of(pcm.begin(),pcm.begin()+96000,[](auto n){return n==0;}),"No sound before one-second propagation delay");
        auto first=std::find_if(pcm.begin(),pcm.end(),[](auto n){return n!=0;});require(first!=pcm.end(),"Thunder becomes audible");
        require(std::distance(pcm.begin(),first)<98000,"Attack starts promptly after arrival");
        synth.setScene(s);require(synth.thunderCount()==1,"Same strike has one audio voice");
        s.strikes[0].key=92;s.strikes[0].age=10;synth.setScene(s);require(synth.thunderCount()==1,"Missed old thunder is not replayed after a fast clock or teleport");
    });
    test("sound","Muting fades to exact silence and removes pending thunder",[] {
        SoundScene s;s.audible=true;s.wind=1;s.strikes.push_back({71,3000,0,1,0});SoundSynth synth;synth.setScene(s);
        std::vector<std::int16_t> pcm(96000);synth.render(pcm);s.audible=false;synth.setScene(s);synth.render(pcm);
        require(synth.activeThunder()==0,"Pending thunder is cancelled on mute or pause");
        require(std::all_of(pcm.begin()+72000,pcm.end(),[](auto n){return n==0;}),"Fade settles to digital silence");
        s.audible=true;s.volume=0;synth.setScene(s);synth.render(pcm);require(std::all_of(pcm.begin(),pcm.end(),[](auto n){return n==0;}),"Zero volume stays silent");
        s.strikes[0].key=72;synth.setScene(s);require(synth.activeThunder()==1,"New strike can be queued");
        s.thunderEnabled=false;synth.setScene(s);require(synth.activeThunder()==0,"Turning off the cloud/global source cancels queued thunder");
    });
    test("sound","Sound scene follows weather water and module dependencies",[] {
        World w;GlobalWeather catalog(w);const auto p=catalog.observation(StormKind::Hurricane);Simulation sim(w,p,0);Explorer e(w);e.teleport(w,p,&sim.water);
        auto s=soundScene(w,sim,e);require(s.audible && s.wind>.8 && s.rain>0 && s.water>.5,"Storm drives all ambient controls");
        require(!soundScene(w,sim,e,.5,true).audible && !soundScene(w,sim,e,.5,false,true).audible,"Pause and orbital view are silent");
        w.modules().set(Module::Sound,false);require(!soundScene(w,sim,e).audible,"Sound module can be disabled independently");
        w.modules().set(Module::Sound,true);w.modules().set(Module::Clouds,false);sim.advance(w,p,0);s=soundScene(w,sim,e);
        require(s.rain==0 && s.wind>0,"Cloud switch silences rain while retaining wind");
        w.modules().set(Module::Atmosphere,false);sim.advance(w,p,0);require(!soundScene(w,sim,e).audible,"No airborne ambience in vacuum");
    });
    test("sound","Thunder references the same world strike as the lightning renderer",[] {
        World w;GlobalWeather catalog(w);const auto p=catalog.observation(StormKind::Thunderstorm);Simulation sim(w,p,0);Explorer e(w);e.teleport(w,p,&sim.water);
        const auto s=soundScene(w,sim,e);require(!s.strikes.empty(),"Local convective strike is audible");
        require(s.strikes.front().distance>690 && s.strikes.front().distance<1000,"Nearby lightning foot includes terrain and eye height");
        near(s.strikes.front().distance,length(s.strikes.front().source-s.listener),0,"Three-dimensional propagation distance");
        const double oldPan=s.strikes.front().pan;e.look(pi,0);const auto other=soundScene(w,sim,e);
        near(other.strikes.front().pan,-oldPan,1e-9,"Turning reverses the world-fixed source direction");
        near(other.strikes.front().distance,s.strikes.front().distance,0,"Turning does not alter travel distance");
    });
    test("sound","An existing thunder voice stays fixed in the world when the listener turns",[] {
        SoundScene s;s.audible=true;s.volume=1;s.strikes.push_back({0,1000,1,1,0,{1000,0,0}});
        SoundSynth synth;synth.setScene(s);std::vector<std::int16_t> before(336000),after(48000);synth.render(before);
        s.right={-1,0,0};synth.setScene(s);synth.render(after);
        double l=0,r=0;for(std::size_t i=24000;i<after.size();i+=2){l+=static_cast<double>(after[i])*after[i];r+=static_cast<double>(after[i+1])*after[i+1];}
        require(l>r*10,"Sound moves from right to left after a half turn");require(synth.thunderCount()==1,"Zero event key is valid and deduplicated");
    });
    test("sound","Invalid acoustic controls and malformed buffers are rejected",[] {
        SoundSynth synth;SoundScene s;
        for(double volume:{-1.,2.,std::numeric_limits<double>::quiet_NaN()}) {
            s.volume=volume;bool rejected=false;try{synth.setScene(s);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Invalid volume rejected");
        }
        std::array<std::int16_t,3> broken{};bool rejected=false;try{synth.render(broken);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"Incomplete stereo frame rejected");
    });
    test("global_weather","Spherical areas cover the planet including polar caps",[] {
        constexpr double radius=6371000;double area=0;
        for(int y=0;y<36;++y) {const double a=sphericalCellArea(y,72,36,radius);require(a>0,"Positive polar area");area+=72*a;}
        near(area/(4*pi*radius*radius),1,1e-14,"Exact spherical area");
        near(sphericalCellArea(0,72,36,radius),sphericalCellArea(35,72,36,radius),.01,"Symmetric polar caps");
    });
    test("global_weather","Spherical transport wraps longitude conservatively and remains positive",[] {
        constexpr int nx=16,ny=8;constexpr double radius=20000;
        std::vector<double> water(nx*ny);std::vector<Wind2> wind(nx*ny,{70,20});water[3*nx+15]=12;
        const auto mass=[&](const auto& v) {double total=0;for(int y=0;y<ny;++y) for(int x=0;x<nx;++x) total+=v[static_cast<std::size_t>(y*nx+x)]*sphericalCellArea(y,nx,ny,radius);return total;};
        const auto one=transportSphere(water,wind,nx,ny,radius,1);
        require(one[3*nx]>0,"Flux crosses the date line");
        const auto many=transportSphere(water,wind,nx,ny,radius,3600);
        near(mass(many)/mass(water),1,1e-13,"Area-weighted mass including closed polar boundary");
        for(double v:many) require(std::isfinite(v) && v>=0,"Outgoing CFL substeps preserve positivity");
        wind[0].north=std::numeric_limits<double>::infinity();bool rejected=false;
        try { (void)transportSphere(water,wind,nx,ny,radius,1); } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected,"Nonfinite velocity rejected");
    });
    test("global_weather","Cyclone wind satisfies gradient balance with calm eye and decaying exterior",[] {
        const double r=55000,dp=6000,f=1e-4,v=cycloneWind(r,r,dp,f);
        near(v*v+f*r*v,1.4*dp/(1.15*std::exp(1.0)),1e-9,"Gradient wind pressure balance");
        near(cycloneWind(r,r,dp,-f),v,0,"Hemisphere changes rotation not speed");
        near(cycloneWind(0,r,dp,f),0,0,"Calm vortex center");
        require(cycloneWind(r*8,r,dp,f)<v*.3,"Outer winds decline");
        near(cycloneWind(1,1e250,dp,f),0,0,"Extreme inner profile has no overflow");
    });
    test("global_weather","Tropical genesis requires warm ocean humidity and off-equatorial latitude",[] {
        near(tropicalSupport(299, radians(15),true,.9),0,0,"Cold ocean cannot support genesis");
        near(tropicalSupport(305,0,true,.9),0,0,"Equator lacks planetary rotation");
        near(tropicalSupport(305,radians(45),true,.9),0,0,"Extratropical latitude excluded");
        near(tropicalSupport(305,radians(15),false,.9),0,0,"No genesis over land");
        near(tropicalSupport(305,radians(15),true,.4),0,0,"Dry air suppresses genesis");
        require(tropicalSupport(302,radians(15),true,.85)>.8,"Warm humid ocean supports development");
        Config config;double mean=0;
        for(int i=0;i<360;++i) mean+=oceanTemperature(config,direction(radians(12),radians(i)),297)-297;
        near(mean/360,0,1e-13,"Prescribed basin anomaly has zero zonal mean");
    });
    test("global_weather","Seeded catalog is deterministic and placed in eligible habitats",[] {
        World w;GlobalWeather a(w),b(w);require(a.storms().size()==12,"Default planet has all three system families");
        for(std::size_t i=0;i<a.storms().size();++i) {
            const auto& s=a.storms()[i];near(length(s.center-b.storms()[i].center),0,0,"Deterministic seed");
            const auto climate=w.climatology(s.center);
            if(s.kind==StormKind::Hurricane) require(tropicalSupport(oceanTemperature(w.config(),s.center,climate.temperature),latitude(s.center),climate.ocean,.85)>.35,"Warm tropical ocean");
            if(s.kind==StormKind::Thunderstorm) require(!climate.ocean && climate.precipitation>=700,"Moist terrestrial convective site");
            if(s.kind!=StormKind::Thunderstorm) {
                const auto p=normalized(s.center+east(s.center)*(s.radius/w.config().radius));const auto v=a.sample(p).velocity;
                require(dot(v,north(p))*std::sin(latitude(s.center))>0,"Cyclonic circulation has the correct hemisphere sign");
                near(dot(v,p),0,1e-10,"Wind tangent to sphere");
            }
        }
        w.modules().set(Module::Ocean,false);GlobalWeather dry(w);require(!dry.strongest(StormKind::Hurricane),"No tropical cyclone seeded on an oceanless world");
    });
    test("global_weather","Scalar weather is continuous at the date line and both poles",[] {
        World w;GlobalWeather g(w);
        for(double lat:{-pi/2,pi/2}) {
            const auto a=g.sample(direction(lat,0)),b=g.sample(direction(lat,2));
            near(a.cloud,b.cloud,1e-10,"Same pole cloud");near(a.temperature,b.temperature,1e-10,"Same pole temperature");
        }
        const auto a=g.sample(direction(.2,pi-1e-9)),b=g.sample(direction(.2,-pi+1e-9));
        near(a.cloud,b.cloud,1e-7,"Cloud seam continuity");near(length(a.velocity-b.velocity),0,1e-6,"Vector seam continuity");
    });
    test("global_weather","Global water accounting and evolution are independent of frame partition",[] {
        World w;GlobalWeather a(w),b(w);a.advance(w,10800);
        for(int i=0;i<36;++i) b.advance(w,300);
        require(a.steps()==36 && b.steps()==36,"Fixed five-minute global step");
        require(std::abs(a.budget().residualKg())/a.budget().initialKg<1e-12,"Closed evaporation transport condensation rain ledger");
        require(a.budget().precipitatedKg>0,"Actual global precipitation");
        for(std::size_t i=0;i<a.cells().size();++i) {
            near(a.cells()[i].vapour,b.cells()[i].vapour,0,"Same transported water");
            near(a.cells()[i].temperature,b.cells()[i].temperature,0,"Same thermal history");
            require(a.cells()[i].cloud>=0 && a.cells()[i].vapour>=0,"Positive water columns");
        }
        require(length(a.storms()[0].center-b.storms()[0].center)==0,"Same track");
    });
    test("global_weather","Storms travel on the sphere age and renew with reproducible cohorts",[] {
        World w;GlobalWeather g(w);const auto first=g.storms();g.advance(w,43200);
        unsigned renewed=0;
        for(std::size_t i=0;i<first.size();++i) {
            const auto& s=g.storms()[i];near(length(s.center),1,1e-12,"Spherical track");
            if(s.kind==StormKind::Thunderstorm && s.generation>first[i].generation) ++renewed;
            if(s.kind==StormKind::Cyclone) require(length(s.center-first[i].center)*w.config().radius>10000,"Synoptic translation");
        }
        require(renewed==4,"Finite-lived thunderstorm cells are replaced by young cohorts");
    });
    test("global_weather","Tropical cyclones decay over land with a finite relaxation time",[] {
        World w;GlobalWeather g(w);const auto before=g.storms();w.modules().set(Module::Ocean,false);g.advance(w,21600);
        for(std::size_t i=0;i<before.size();++i) if(before[i].kind==StormKind::Hurricane)
            near(g.storms()[i].deficit,before[i].deficit*std::exp(-6.0/5),1e-8,"Five-hour landfall intensity decay");
    });
    test("global_weather","Module dependencies pause restore and solar clock preserve storm state",[] {
        World w;GlobalWeather g(w);g.advance(w,123);const auto center=g.storms()[0].center;
        for(auto module:{Module::GlobalWeather,Module::Wind,Module::Atmosphere,Module::Climate}) {
            w.modules().set(module,false);g.advance(w,600);g.advanceEffects(2);
            require(!g.sample(center).available,"Disabled dependency stops global field");near(g.elapsed(),123,0,"Pending weather time frozen");
            near(g.effectsTime(),0,0,"Disabled effects clock frozen");w.modules().set(module,true);g.advance(w,0);
        }
        g.setSolarClock(1e7);near(g.elapsed(),123,0,"Noon does not age storms");near(length(g.storms()[0].center-center),0,0,"No teleport from solar clock change");
        const auto p=g.observation(StormKind::Thunderstorm);require(g.flash(p)>0,"Convective lightning pulse");
        w.modules().set(Module::Clouds,false);g.advance(w,0);
        require(g.sample(p).cloud==0 && g.flash(p)==0 && length(g.sample(p).velocity)>0,"Cloud module removes moisture effects but retains circulation");
        w.modules().set(Module::Clouds,true);g.advance(w,0);g.advanceEffects(1);near(g.flash(p),0,0,"Lightning pulse expires on model clock");
        for(unsigned i=0;i<moduleCount;++i) w.modules().set(static_cast<Module>(i),false);
        Simulation bare(w,{1,0,0},0);bare.advance(w,{1,0,0},2);
        require(!bare.global.enabled(),"New planet with all modules off is valid");
        for(unsigned i=0;i<moduleCount;++i) w.modules().set(static_cast<Module>(i),true);
        bare.advance(w,{1,0,0},0);require(bare.global.strongest(StormKind::Hurricane).has_value(),"Restored climate can initialize eligible missing systems");
    });
    test("global_weather","Storm wind reaches the sea and nesting water has an explicit ledger",[] {
        World w;GlobalWeather catalog(w);const auto p=catalog.observation(StormKind::Hurricane);Simulation sim(w,p,0);
        require(length(sim.motion.forcingWind())>33,"Hurricane-strength natural water wind");
        sim.advance(w,p,120);
        require(sim.motion.significantHeight()>.1 && length(sim.motion.forcingWind())>33,"Natural storm grows wind waves");
        require(sim.weather.sample(p).rainRate>0,"Storm rain reaches local column");
        const auto budget=sim.weather.budget();require(std::abs(budget.synopticExchangeKg)>1,"Nesting exchanges moisture");
        require(std::abs(budget.residualKg())<.001,"Regional water accounting includes the exchange");
    });
    test("global_weather","Observer relocation cannot change the global forecast",[] {
        World w;const auto p=w.findLand();Simulation a(w,p,0),b(w,p,0);
        for(int i=0;i<3;++i) {
            a.advance(w,p,120);b.advance(w,i%2 ? p : -p,120);
        }
        near(length(a.global.storms()[0].center-b.global.storms()[0].center),0,0,"Same storm independent of recentering");
        near(a.global.budget().storedKg,b.global.budget().storedKg,0,"Same global column water");
        near(a.global.elapsed(),360,0,"Elapsed time includes fractional global step");
    });
    test("global_weather","Thunderstorm rain feeds runoff once and paused frames add nothing",[] {
        World w;GlobalWeather catalog(w);const auto p=catalog.observation(StormKind::Thunderstorm);Simulation sim(w,p,0);
        sim.advance(w,p,2);const double rain=sim.water.budget().rainM3;
        require(rain>0 && sim.weather.sample(p).rainRate*3600>20,"Heavy convective rain enters soil and runoff");
        sim.advance(w,p,0);near(sim.water.budget().rainM3,rain,0,"No rain during pause");
        require(std::abs(sim.water.budget().waterResidual())<1e-6,"Conservative ground water");
        near(sim.global.elapsed(),2,0,"Zero frame leaves global clock unchanged");
    });
    test("global_weather","Global preview validates limits and advances only the global model",[] {
        World w;const auto p=w.findLand();Simulation sim(w,p,0,{},2);
        near(sim.global.elapsed(),7200,0,"Two-hour global forecast");require(sim.weather.steps()==0 && sim.water.steps()==0,"Local history is not fabricated by preview");
        for(double hours:{-1.,241.,std::numeric_limits<double>::quiet_NaN()}) {
            bool rejected=false;try {GlobalWeather invalid(w,0,hours);} catch(const std::invalid_argument&) {rejected=true;}
            require(rejected,"Invalid preview rejected");
        }
    });
    test("flora","Species traits cover distinct land and marine growth strategies",[] {
        unsigned marine=0,fresh=0,woody=0;
        for(unsigned i=1;i<speciesCount;++i) {
            const auto& t=plantTraits(static_cast<Species>(i));
            require(t.height>0 && t.widthRatio>0 && t.maturityDays>0 && t.lifespanDays>t.maturityDays && t.decayDays>0,"Valid biological dimensions and timescales");
            if(t.aquatic) {if(freshwaterPlant(static_cast<Species>(i))) ++fresh;else ++marine;}
            if(t.woody) ++woody;
        }
        require(marine==3 && fresh==4 && woody==16,"Marine, freshwater and woody archetypes");
        require(plantTraits(Species::Oak).maturityDays>plantTraits(Species::MeadowGrass).maturityDays*50,"Trees take years, herbs months");
    });
    test("flora","Growth is continuous bounded and follows its analytic size curve",[] {
        for(unsigned i=1;i<speciesCount;++i) {
            const auto species=static_cast<Species>(i);const auto& t=plantTraits(species);
            const auto a=plantState(species,71,0,1,true),b=plantState(species,71,t.maturityDays/2,1,true);
            require(b.size>a.size && b.size<1 && a.stage==GrowthStage::Seedling,"Seeds grow rather than appearing mature");
            near(b.size,1-std::exp(-3*b.ageDays/t.maturityDays),1e-12,"Analytic growth law");
            const auto c=plantState(species,71,t.maturityDays/2+.0001,1,true);
            require(std::abs(c.size-b.size)<.00001,"Continuous dimensions");
            require(plantState(species,71,t.maturityDays/2,.3,true).size<b.size,"Poor habitat slows growth");
        }
    });
    test("flora","Senescence decay and local seed bank renew a site without mature spawn",[] {
        const auto& t=plantTraits(Species::Kelp);const auto initial=plantState(Species::Kelp,24,0,1,true).ageDays;
        const auto dying=plantState(Species::Kelp,24,t.lifespanDays*.95-initial,1,true);
        require(dying.stage==GrowthStage::Senescent && dying.foliage<1,"Senescence before death");
        const auto dead=plantState(Species::Kelp,24,t.lifespanDays+1-initial,1,true);
        const auto decayed=plantState(Species::Kelp,24,t.lifespanDays+t.decayDays*.9-initial,1,true);
        require(dead.stage==GrowthStage::Dead && decayed.size<dead.size,"Biomass geometry decays");
        const auto seed=plantState(Species::Kelp,24,t.lifespanDays+t.decayDays+10-initial,1,true);
        require(seed.stage==GrowthStage::Seed && seed.size==0,"Dormant seed/spore interval");
        const auto next=plantState(Species::Kelp,24,t.lifespanDays+t.decayDays+91-initial,1,true);
        require(next.generation==1 && next.stage==GrowthStage::Seedling && next.size<.02,"Next cohort emerges small at the same site");
    });
    test("flora","Habitat filters temperature moisture substrate and underwater light",[] {
        Sample s;s.temperature=288;s.precipitation=900;s.elevation=100;
        require(plantSuitability(Species::Oak,s,.1,.5,0)>0,"Temperate oak habitat");
        require(plantSuitability(Species::TropicalTree,s,.1,.5,0)==0,"No tropical tree in a cold habitat");
        s.precipitation=100;require(plantSuitability(Species::Oak,s,.1,.5,0)==0,"Too dry for oak");
        s.ocean=true;s.elevation=-4;
        const double shallow=plantSuitability(Species::Kelp,s,.1,.8,0);require(shallow>0,"Kelp on lit rock");
        s.elevation=-20;require(plantSuitability(Species::Kelp,s,.1,.8,0)<shallow,"Light declines with depth");
        require(plantSuitability(Species::Seagrass,s,.1,.9,0)==0,"Seagrass excludes rocky substrate");
        require(plantSuitability(Species::Oak,s,.1,.2,0)==0,"No underwater oak");
        s.elevation=-50;require(plantSuitability(Species::Kelp,s,.1,.8,0)==0,"No deep dark kelp");
    });
    test("flora","Stable plant sites grow independently of query order and observer",[] {
        World w;const auto p=w.findLand(),q=normalized(p+east(p)*(12/w.config().radius));
        Flora young;young.setYoung(true);Flora old=young;old.advanceDays(w,700);
        const auto a=surfaceObjects(w,p,180,false,&young),b=surfaceObjects(w,q,180,false,&old);
        std::map<std::uint64_t,SurfaceObject> aged;for(const auto& o:b) aged.emplace(o.id,o);
        unsigned compared=0;
        for(const auto& o:a) if(o.kind==DetailKind::Tree && length(o.up-q)*w.config().radius<170) {
            require(aged.contains(o.id),"Same plant still exists after return");const auto& other=aged.at(o.id);
            require(o.species==other.species && other.height>o.height,"Same species and larger dimensions");
            near(length(o.up-other.up),0,0,"Root coordinates unchanged");++compared;
        }
        require(compared>3,"Several trees compared");
        const auto again=surfaceObjects(w,p,180,false,&young);require(a.size()==again.size(),"Query has no hidden simulation side effects");
    });
    test("flora","Coasts contain rooted marine vegetation but the abyss does not",[] {
        World w;const auto coast=w.findCoast();const auto plants=surfaceObjects(w,coast.lookToward,80);
        unsigned count=0;
        for(const auto& p:plants) if(p.species!=Species::None) {
            require(plantTraits(p.species).aquatic,"Marine types only at offshore fixture");
            require(p.elevation<0 && p.elevation+p.height<0,"Roots on bed, crowns below mean sea level");++count;
        }
        require(count>15,"A visible coastal meadow or kelp stand");
        w.modules().set(Module::Ocean,false);
        for(const auto& p:surfaceObjects(w,coast.lookToward,80)) require(!plantTraits(p.species).aquatic,"Marine plants require seawater");
    });
    test("flora","Biological clock respects pause modules and frame partition",[] {
        World w;Flora a,b;a.setRate(86400);b.setRate(86400);
        a.advance(w,60);for(int i=0;i<240;++i)b.advance(w,.25);
        near(a.days(),b.days(),1e-12,"Frame independent biological age");
        a.advance(w,0);near(a.days(),60,0,"Pause freezes age");
        w.modules().set(Module::PlantGrowth,false);a.advance(w,120);near(a.days(),60,0,"I freezes growth but retains age");
        w.modules().set(Module::PlantGrowth,true);a.advance(w,2);near(a.days(),62,0,"Growth resumes");
        w.modules().set(Module::Biomes,false);require(!w.modules().enabled(Module::PlantGrowth),"Growth waits for habitat dependencies");
        a.advance(w,10);near(a.days(),62,0,"Unavailable habitat freezes growth");
    });
    test("flora","Growth acceleration leaves atmospheric and water clocks unchanged",[] {
        World w;const auto p=w.findLand();Simulation a(w,p,0),b(w,p,0);
        b.flora.setRate(30*86400);a.advance(w,p,2);b.advance(w,p,2);
        near(b.flora.days(),60,1e-12,"Biological experiment advances sixty days");
        near(a.weather.seconds(),b.weather.seconds(),0,"Weather time unchanged by biological multiplier");
        near(a.water.budget().storedWaterM3,b.water.budget().storedWaterM3,0,"No hidden water source");
        b.setClock(10000);near(b.flora.days(),60,0,"Noon clock change does not age plants");
    });
    test("flora","Navigation invalidates growing trunk collisions without movement",[] {
        World w;Flora f;f.setYoung(true);const auto sites=surfaceSites(w,w.findLand(),180,false);
        const auto tree=std::find_if(sites.begin(),sites.end(),[](const auto& o){return o.kind==DetailKind::Tree && o.suitability>.4;});
        require(tree!=sites.end(),"A growing trunk fixture");
        const Vec3 start=normalized(tree->up-north(tree->up)*(2/w.config().radius));
        Explorer e(w);e.teleport(w,start);e.update(w,{},.01,nullptr,nullptr,&f);
        f.advanceDays(w,8000);const auto grown=grownObject(*tree,&f);
        e.update(w,{1,0,0,false,false},1.1,nullptr,nullptr,&f);
        require(length(e.up()-tree->up)*w.config().radius>=grown.collisionRadius()+.28,"New trunk radius blocks walking after age change");
    });
    test("flora","Biology validates inputs and bounds long time horizons",[] {
        World w;Flora f;
        for(double days:{-1.0,365251.0,std::numeric_limits<double>::infinity()}) {
            bool rejected=false;try{f.advanceDays(w,days);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Invalid age rejected");
        }
        bool rejected=false;try{f.setRate(0);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Invalid rate rejected");
        f.setRate(2592000);f.advance(w,86400);near(f.days(),365250,0,"Maximum supported horizon saturates safely");
        for(unsigned i=1;i<speciesCount;++i) {
            const auto s=plantState(static_cast<Species>(i),29,f.days(),.5);
            require(std::isfinite(s.size) && s.size>=0 && s.size<=1,"Finite long-term state");
        }
    });
    test("water_motion","Bulk wind stress has physical units and opposes relative motion",[] {
        const auto a=windStress({10,0}); near(a.east,0.177625,1e-12,"Ten metre reference wind stress in Pa"); near(a.north,0,0,"No crosswind force");
        const auto b=windStress({-10,0}); near(b.east,-a.east,1e-12,"Reversing wind reverses force");
        near(windStress({10,0},{10,0}).east,0,0,"No stress without relative wind");
        require(windStress({0,0},{2,0}).east<0,"Still air resists moving water");
        bool rejected=false;try { windStress({std::numeric_limits<double>::infinity(),0}); } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected,"Nonfinite wind rejected");
    });
    test("water_motion","Gravity wave dispersion approaches deep and shallow water limits",[] {
        const double k=tau/20;
        near(waveFrequency(k,200),std::sqrt(standardGravity*k),1e-12,"Deep water dispersion");
        near(waveFrequency(1e-5,2)/1e-5,std::sqrt(standardGravity*2),1e-8,"Shallow water phase speed");
        near(tau/waveFrequency(tau/100,1000),8.004415,0.000001,"100 metre deep water wave period");
        bool rejected=false;try { waveFrequency(k,0); } catch(const std::invalid_argument&) { rejected=true; } require(rejected,"Zero depth rejected");
    });
    test("water_motion","Ocean slab preserves inertial energy and integrates wind momentum",[] {
        const double f=1e-4; const Wind2 initial{1,0}; auto u=initial;
        for(int i=0;i<120;++i) u=advanceSlab(u,{},15,f,0,60);
        near(std::hypot(u.east,u.north),1,1e-12,"Coriolis force does no work"); require(u.north<0,"Northern current turns right");
        const auto south=advanceSlab(initial,{},15,-f,0,120); require(south.north>0,"Southern current turns left");
        const auto impulse=advanceSlab({}, {1,0},10,0,0,100); near(impulse.east,100.0/10250,1e-12,"Stress changes momentum by tau dt");
        const auto damped=advanceSlab(initial,{},10,0,0.01,100); near(damped.east,std::exp(-1.0),1e-12,"Linear drag dissipates energy");
    });
    test("water_motion","Wind raises downwind lake level without creating water or sediment",[] {
        RunoffGrid lake(12,4); for(auto& c:lake.cells) { c.water=1;c.soilWater=0;c.conductivity=0;c.sediment=0.2; }
        const double volume=lake.waterVolume(),mass=lake.solidMass();
        std::vector<double> rain(lake.cells.size());std::vector<Wind2> stress(lake.cells.size(),windStress({16,0}));
        lake.advance(1,rain,0,false,false,false,0,stress);
        require(lake.velocity(6,6).east>0,"Initial current responds downwind");
        lake.advance(59,rain,0,false,false,false,0,stress);
        near(lake.waterVolume(),volume,1e-8,"Wind supplies momentum, not water");near(lake.solidMass(),mass,1e-7,"Wind transport conserves solids");
        require(lake.cells[6*12+11].water>lake.cells[6*12].water,"Downwind setup develops");
        RunoffGrid dry(12,4);for(auto& c:dry.cells)c.soilWater=0;
        dry.advance(10,rain,0,false,false,false,0,stress);near(dry.waterVolume(),0,0,"Wind cannot create water on dry ground");
    });
    test("water_motion","Wind seas decay in calm and retain state when disabled",[] {
        Config c;c.seaLevel=20;World w(c);w.modules().set(Module::Terrain,false);const Vec3 p{1,0,0};Simulation sim(w,p,0,12);
        const double height=sim.motion.significantHeight();require(height>0.2,"Wind creates finite wave variance");
        sim.advance(w,p,120);require(length(sim.motion.current(p))>0.001,"Ocean current receives wind momentum");
        sim.motion.setWindExperiment(0);for(int i=0;i<10;++i)sim.advance(w,p,120);
        require(sim.motion.significantHeight()<height*0.6,"Calm waves dissipate rather than disappearing instantly");
        const double time=sim.motion.seconds(),saved=sim.motion.significantHeight();
        w.modules().set(Module::WaterMotion,false);sim.advance(w,p,10);near(sim.motion.seconds(),time,0,"Disabled wave clock pauses");
        near(sim.motion.waves(p,20).height,0,0,"Disabled module leaves flat water");near(length(sim.motion.current(p)),0,0,"Disabled wind current is not applied");
        w.modules().set(Module::WaterMotion,true);sim.advance(w,p,0);near(sim.motion.significantHeight(),saved,1e-12,"Enabling restores retained variance");
    });
    test("water_motion","Wave and current clocks are frame independent and pause is exact",[] {
        Config c;c.seaLevel=20;World w(c);w.modules().set(Module::Terrain,false);const Vec3 p{1,0,0};Simulation a(w,p,0,12),b(w,p,0,12);
        for(int i=0;i<240;++i) a.advance(w,p,0.5);
        b.advance(w,p,120);
        near(length(a.motion.current(p)-b.motion.current(p)),0,1e-12,"Same accumulated ocean forcing");
        near(a.motion.waves(p,20).height,b.motion.waves(p,20).height,1e-12,"Same wave phase and variance");
        const double wave=a.motion.waves(p,20).height;const auto probe=a.motion.probes()[0];a.advance(w,p,0);
        near(a.motion.waves(p,20).height,wave,0,"Pause retains wave phase");near(length(a.motion.probes()[0]-probe),0,0,"Pause retains current probe");
    });
    test("water_motion","Wave surface is zero mean and bounded above the seabed",[] {
        Config c;c.seaLevel=30;World w(c);w.modules().set(Module::Terrain,false);const Vec3 p{1,0,0};Simulation sim(w,p,0,16);
        double mean=0,squares=0;const int n=64;
        for(int y=0;y<n;++y)for(int x=0;x<n;++x) {
            const auto q=normalized(p+(east(p)*(x*17)+north(p)*(y*23))/c.radius);
            const auto wave=sim.motion.waves(q,30);mean+=wave.height;squares+=wave.height*wave.height;
            require(std::abs(sim.motion.waves(q,0.03).height)<=0.0105+1e-10,"Shallow trough never cuts through bed");
        }
        mean/=n*n;squares/=n*n;
        require(std::abs(mean)<sim.motion.significantHeight()*0.03,"Wave elevation has approximately zero spatial mean");
        near(4*std::sqrt(squares),sim.motion.significantHeight(),sim.motion.significantHeight()*0.15,"Resolved variance agrees with significant height");
        const double before=sim.motion.waves(p,30).height;sim.advance(w,p,0.3);
        require(std::abs(before-sim.motion.waves(p,30).height)>0.001,"Wave geometry propagates between 2 s weather steps");
    });
    test("water_motion","Swimmer floats dives resurfaces and respects the seabed",[] {
        Config c;c.seaLevel=20;World w(c);w.modules().set(Module::Terrain,false);Simulation sim(w,{1,0,0},0,0);Explorer e(w);e.teleport(w,{1,0,0},&sim.water);
        for(int i=0;i<100;++i)e.update(w,{},0.1,&sim.water,&sim.motion);
        require(e.swimming() && !e.underwater(),"Buoyant swimmer rests above the waterline");near(e.altitude(),20+Explorer::eyeHeight*(1-1/1.14),0.02,"Archimedes equilibrium");
        for(int i=0;i<40;++i)e.update(w,{0,0,-1,false,false},0.1,&sim.water,&sim.motion);
        require(e.underwater() && e.altitude()<18,"Ctrl supplies downward swimming thrust");
        for(int i=0;i<50;++i)e.update(w,{0,0,1,false,false},0.1,&sim.water,&sim.motion);
        require(!e.underwater(),"Space returns through the surface");
        e.dive(w,{1,0,0},200,&sim.water,&sim.motion);require(e.altitude()>=Explorer::eyeHeight,"Dive cannot pass through the seabed");
    });
    test("water_motion","Ocean flow advects a submerged swimmer",[] {
        Config c;c.seaLevel=20;World w(c);w.modules().set(Module::Terrain,false);const Vec3 p{1,0,0};Simulation sim(w,p,0,16);
        for(int i=0;i<5;++i)sim.advance(w,p,120);
        Explorer a(w),b(w);a.dive(w,p,5,&sim.water,&sim.motion);b.dive(w,p,5,&sim.water);
        for(int i=0;i<50;++i){a.update(w,{},0.1,&sim.water,&sim.motion);b.update(w,{},0.1,&sim.water);}
        require(dot(a.up()-p,east(p))*c.radius>0.05,"Water drag carries observer downcurrent");near(length(b.up()-p),0,1e-12,"Still water does not create lateral movement");
    });
    test("water_motion","Coast route connects walking swimming and diving",[] {
        for(std::uint64_t seed=0;seed<6;++seed) {
            Config c;c.seed=seed;World seeded(c);const auto coast=seeded.findCoast();
            require(seeded.elevation(coast.position)>0 && seeded.elevation(coast.lookToward)<-8,"Seeded coast has a dry shore and a diving site");
        }
        World w;w.modules().set(Module::SurfaceDetail,false);const auto place=w.findCoast();
        require(w.elevation(place.position)>0 && w.elevation(place.position)<12,"Dry shore within walking height of sea");
        require(w.elevation(place.lookToward)<-8,"Offshore destination has diving depth");
        Simulation sim(w,place.position,0,0);Explorer e(w);e.teleport(w,place.position,&sim.water);
        const auto look=normalized(place.lookToward-place.position*dot(place.lookToward,place.position));
        e.look(std::atan2(dot(look,east(place.position)),dot(look,north(place.position))),0);
        for(int i=0;i<1200 && !e.swimming();++i){e.update(w,{1,0,0,true,false},0.1,&sim.water,&sim.motion);sim.advance(w,e.up(),0);}
        require(e.swimming(),"Observer can enter the sea continuously from land");
    });
    test("water_motion","Hydrostatic water pressure and module changes remain finite",[] {
        near(waterPressure(101325,10,false),199391.5,1e-8,"Ten metres of fresh water");
        near(waterPressure(101325,10,true),201843.1625,1e-8,"Seawater density raises pressure");
        Config c;c.seaLevel=20;World w(c);w.modules().set(Module::Terrain,false);Simulation sim(w,{1,0,0},0,12);Explorer e(w);e.dive(w,{1,0,0},5,&sim.water,&sim.motion);
        w.modules().set(Module::Ocean,false);sim.advance(w,e.up(),0);e.update(w,{},0.1,&sim.water,&sim.motion);
        require(!e.swimming() && !e.underwater() && !w.modules().enabled(Module::WaterMotion),"Disabling the last water module removes immersion");
        require(std::isfinite(e.altitude()) && e.altitude()>=Explorer::eyeHeight,"Observer remains above dry ground");
    });
    test("landforms","Regional relief contains distinct mountains canyons and gorges",[] {
        World w; const auto home=w.findLand();
        for(auto kind:{Landform::Mountains,Landform::Canyon,Landform::Gorge}) {
            const auto place=w.findLandmark(kind,home); const auto target=w.relief(place.lookToward);
            require(place.type==kind && target.type==kind,"Requested landform is reachable");
            require(w.elevation(place.position)>w.config().seaLevel,"Survey ledge is dry land");
            if(kind!=Landform::Mountains) require(target.incision>80,"Valley has substantial incision");
        }
    });
    test("landforms","Steep new relief stays continuous at small distances",[] {
        World w;
        for(int i=0;i<2500;++i) {
            const auto p=point(i,2500),q=normalized(p+east(p)*(0.1/w.config().radius)),r=normalized(p+east(p)*(0.001/w.config().radius));
            require(std::abs(w.elevation(q)-w.elevation(p))<2,"No height discontinuity across 10 cm on steep relief");
            require(std::abs(w.elevation(r)-w.elevation(p))<0.02,"No quantized terrace steps across 1 mm");
        }
        const auto place=w.findLandmark(Landform::Karst,w.findLand()); const auto cave=w.caveSite(place.position);
        require(cave.has_value(),"Cave exists for boundary check");
        for(double z:{-140.,-85.,-40.,65.,310.,380.,410.,500.}) {
            const auto a=cave->directionAt(0,z-0.001),b=cave->directionAt(0,z+0.001);
            require(std::abs(w.elevation(a)-w.elevation(b))<0.02,"Cave cover blends continuously");
        }
    });
    test("landforms","Caves have deterministic dry entrances across seeds",[] {
        for(std::uint64_t seed=0;seed<6;++seed) {
            Config c;c.seed=seed; World a(c),b(c);
            const auto place=a.findLandmark(Landform::Karst,a.findLand()); const auto ca=a.caveSite(place.position),cb=b.caveSite(place.position);
            require(ca && cb && ca->id==cb->id,"Same seed and location give same cave");
            require(ca->base>c.seaLevel+80,"Underground rooms remain above sea level");
            near(ca->columnLocal(0,250).floor,cb->columnLocal(0,250).floor,0,"Deterministic chamber floor");
        }
    });
    test("landforms","Entrance opens through the surface and gallery has solid cover",[] {
        World w; const auto place=w.findLandmark(Landform::Karst,w.findLand()); const auto site=w.caveSite(place.position);
        require(site.has_value(),"Cave present");
        const auto mouth=site->directionAt(0,-50),deep=site->directionAt(site->nodes[4].x,site->nodes[4].z);
        const auto a=site->column(mouth),b=site->column(deep);
        require(a.floor<w.elevation(mouth) && a.ceiling>w.elevation(mouth),"Real opening intersects the top surface");
        require(b.ceiling<w.elevation(deep)-25 && b.ceiling-b.floor>8,"Gallery lies under a substantial rock roof");
        require(!site->columnLocal(110,120).valid(),"Solid material outside the footprint");
        for(const auto link:site->links) for(int i=0;i<=20;++i) {
            const auto first=site->nodes[link[0]],last=site->nodes[link[1]];
            const auto column=site->columnLocal(mix(first.x,last.x,i/20.0),mix(first.z,last.z,i/20.0));
            require(column.valid() && column.ceiling-column.floor>6,"All connected passage centres admit a person");
        }
    });
    test("landforms","First person enters explores both branches and walks out",[] {
        World w; w.modules().set(Module::SurfaceDetail,false);
        const auto place=w.findLandmark(Landform::Karst,w.findLand()); const auto site=w.caveSite(place.position);
        require(site.has_value(),"Cave present"); Explorer e(w); e.teleport(w,place.position); Hydrology water(w,place.position);
        for(unsigned node:{1U,2U,3U,4U,5U,6U,5U,4U,3U,7U,8U,9U,8U,7U,3U,2U}) {
            const auto p=site->directionAt(site->nodes[node].x,site->nodes[node].z); walkTo(e,w,p,&water);
            const auto column=site->column(e.up());
            require(e.altitude()>=column.floor+Explorer::eyeHeight-0.05,"Feet stay above the floor");
            if(node!=1) require(e.underground() && e.altitude()<column.ceiling,"Walking under the roof, not above terrain");
        }
        walkTo(e,w,site->directionAt(0,-25),&water); walkTo(e,w,place.position,&water);
        require(!e.underground(),"Exit reconnects to surface navigation");
    });
    test("landforms","Cave ceiling and walls block high speed flight",[] {
        World w;w.modules().set(Module::SurfaceDetail,false);
        const auto place=w.findLandmark(Landform::Karst,w.findLand()); const auto site=w.caveSite(place.position);
        require(site.has_value(),"Cave present");Explorer e(w); e.teleport(w,place.position);
        walkTo(e,w,site->directionAt(0,-25)); walkTo(e,w,site->directionAt(site->nodes[2].x,25));
        e.flying=true;e.flightSpeed=20000;e.update(w,{0,0,1,true,false},1);
        require(e.altitude()<=site->column(e.up()).ceiling-0.19,"Head stops at ceiling");
        const auto right=site->right; const double heading=std::atan2(dot(right,east(e.up())),dot(right,north(e.up())));
        e.look(std::remainder(heading-e.heading(),tau),0);e.update(w,{1,0,0,true,false},3);
        require(e.underground() && site->column(e.up()).valid(),"Flight cannot tunnel through a cave wall");
    });
    test("landforms","Surface walking does not fall through a closed cave roof",[] {
        World w;w.modules().set(Module::SurfaceDetail,false);
        const auto place=w.findLandmark(Landform::Karst,w.findLand());const auto site=w.caveSite(place.position);
        require(site.has_value(),"Cave present");Explorer e(w);e.teleport(w,site->directionAt(5,125));e.update(w,{},1);
        near(e.altitude(),w.surface(e.up())+Explorer::eyeHeight,0.02,"Standing on the upper surface");
        require(!e.underground(),"Upper and lower navigable surfaces stay separate");
    });
    test("landforms","Disabling caves returns the observer safely to the surface",[] {
        World w;w.modules().set(Module::SurfaceDetail,false);
        const auto place=w.findLandmark(Landform::Karst,w.findLand());const auto site=w.caveSite(place.position);
        require(site.has_value(),"Cave present");Explorer e(w);e.teleport(w,place.position);
        walkTo(e,w,site->directionAt(0,-25));walkTo(e,w,site->directionAt(site->nodes[2].x,25));
        require(e.underground(),"Starts inside");w.modules().set(Module::Caves,false);e.update(w,{},0.1);
        require(!e.underground(),"Safe return after disabling cave geometry");
        near(e.altitude(),w.surface(e.up())+Explorer::eyeHeight,1e-8,"Observer is not embedded in the filled volume");
        w.modules().set(Module::Caves,true); w.modules().set(Module::Landforms,false);
        require(!w.modules().enabled(Module::Caves) && w.modules().requested(Module::Caves),"Cave dependency retains request");
    });
    test("landforms","Runoff excludes unresolved caves and rebases on module changes",[] {
        World w;const auto place=w.findLandmark(Landform::Karst,w.findLand());const auto site=w.caveSite(place.position);
        require(site.has_value(),"Cave present");const auto p=site->directionAt(0,-25);
        Simulation sim(w,p,0);sim.setRainExperiment(80);sim.advance(w,p,20);
        require(!sim.water.sample(p).available,"Heightfield water does not cover the cave entrance");
        const auto check=[&] { const auto b=sim.water.budget();require(std::abs(b.waterResidual())<1e-6 && std::abs(b.solidResidual())<.001,"Mask/rebase ledger stays closed"); };
        check();w.modules().set(Module::Caves,false);sim.advance(w,p,0);
        require(sim.water.sample(p).available,"Ordinary surface water returns without caves");check();
        w.modules().set(Module::Landforms,false);sim.advance(w,p,2);check();
        near(sim.water.ground(w,p),w.surface(p),1.0,"Runoff bed follows the selected generator");
        w.modules().set(Module::Landforms,true);w.modules().set(Module::Caves,true);sim.advance(w,p,0);check();
    });
    test("astronomy","Kepler perihelion and aphelion",[] {
        Config c;
        near(orbit(c,0).distanceAU,1-c.eccentricity,1e-12,"Perihelion");
        near(orbit(c,c.year/2).distanceAU,1+c.eccentricity,1e-12,"Aphelion");
        near(orbit(c,0).irradiance/ orbit(c,c.year/2).irradiance,
             std::pow((1+c.eccentricity)/(1-c.eccentricity),2),1e-12,"Inverse-square flux");
    });
    test("astronomy","Equinox analytic daily flux",[] {
        Config c; c.obliquity=0; c.eccentricity=0;
        near(dailyInsolation(c,0,0),c.solarConstant/pi,1e-10,"Equatorial flux");
        near(dailyInsolation(c,pi/2,0),0,1e-10,"Polar flux");
        near(dailyInsolation(c,pi/3,0),c.solarConstant/(2*pi),1e-10,"60 degree flux");
    });
    test("astronomy","Global absorbed energy integrates to S/4",[] {
        Config c; const double time=124*86400;
        double total=0;
        constexpr int n=20000;
        for(int i=0;i<n;++i) total+=dailyInsolation(c,std::asin(1-2*(i+0.5)/n),time);
        near(total/n,orbit(c,time).irradiance/4,0.01,"Global daily flux");
    });
    test("astronomy","Polar night and midnight sun",[] {
        Config c;
        const auto summer=orbit(c,c.year*0.46);
        require(summer.declination>radians(20),"Northern summer declination");
        require(dailyInsolation(c,pi/2,c.year*0.46)>400,"Polar day receives light");
        near(dailyInsolation(c,-pi/2,c.year*0.46),0,1e-12,"Polar night receives none");
    });
    test("astronomy","Circular equatorial solar day",[] {
        Config c; c.eccentricity=0; c.obliquity=0;
        near(length(orbit(c,0).sunDirection-orbit(c,c.day).sunDirection),0,1e-12,"Solar day period");
        near(length(orbit(c,0).sunDirection),1,1e-12,"Unit sun direction");
    });
    test("atmosphere","Sea level standard values",[] {
        const auto a=standardAtmosphere(0);
        near(a.temperature,288.15,1e-10,"Sea temperature");
        near(a.pressure,101325,1e-8,"Sea pressure");
        near(a.density,1.225,0.0001,"Sea density");
    });
    test("atmosphere","Standard atmosphere table",[] {
        constexpr double radius=6371000;
        for(const auto [h,t,p]:std::vector<std::array<double,3>>{{11000,216.65,22632.06},{20000,216.65,5474.89},{32000,228.65,868.02},{47000,270.65,110.906}}) {
            const auto a=standardAtmosphere(radius*h/(radius-h));
            near(a.temperature,t,0.001,"Layer temperature");
            near(a.pressure,p,0.1,"Layer pressure");
        }
    });
    test("atmosphere","Hydrostatic pressure derivative",[] {
        constexpr double h=4500,r=6371000;
        const auto a=standardAtmosphere(h);
        const double derivative=(standardAtmosphere(h+1).pressure-standardAtmosphere(h-1).pressure)/2;
        near(derivative,-a.density*standardGravity*std::pow(r/(r+h),2),1e-5,"Hydrostatic balance");
    });
    test("atmosphere","Continuous layer interfaces",[] {
        for(const double h:{11000.,20000.,32000.,47000.,51000.,71000.,84852.}) {
            const double z=6371000*h/(6371000-h);
            const auto a=standardAtmosphere(z-0.001),b=standardAtmosphere(z+0.001);
            require(std::abs(a.pressure-b.pressure)<0.02,"Pressure has no interface jump");
            require(std::abs(a.temperature-b.temperature)<0.001,"Temperature has no interface jump");
        }
    });
    test("atmosphere","Altitude range and ideal gas law",[] {
        double previous=1e9;
        for(int z=-4000;z<=100000;z+=1000) {
            const auto a=standardAtmosphere(z);
            require(a.pressure>0 && a.pressure<previous,"Pressure decreases with altitude");
            require(a.temperature>0 && a.density>0,"Finite positive air properties");
            near(a.density*gasConstantAir*a.temperature,a.pressure,1e-7,"Ideal gas law");
            previous=a.pressure;
        }
    });
    test("terrain","Determinism and seed variation",[] {
        World a,b; Config c; c.seed=43; World other(c);
        double difference=0;
        for(int i=0;i<200;++i) {
            const Vec3 p=point(i,200);
            near(a.elevation(p),b.elevation(p),0,"Same seed has identical terrain");
            difference+=std::abs(a.elevation(p)-other.elevation(p));
        }
        require(difference/200>100,"Distinct seeds produce different worlds");
    });
    test("terrain","Global finite elevations and land/ocean coverage",[] {
        World w; int land=0,sea=0;
        for(int i=0;i<5000;++i) {
            const auto h=w.elevation(point(i,5000));
            require(std::isfinite(h) && h>-6000 && h<9000,"Terrestrial terrain range (including high massifs)");
            if(h>0) ++land; else ++sea;
        }
        require(land>150 && sea>150,"Both land and ocean exist");
    });
    test("terrain","Longitude seam and poles",[] {
        World w;
        for(int lat=-90;lat<=90;lat+=5) {
            near(w.elevation(direction(radians(lat),pi)),w.elevation(direction(radians(lat),-pi)),1e-7,"Dateline seamless");
        }
        near(w.elevation(direction(pi/2,0)),w.elevation(direction(pi/2,2)),1e-7,"North pole seamless");
    });
    test("terrain","Local continuity across random plate boundaries",[] {
        World w; w.modules().set(Module::Landforms,false);
        for(int i=0;i<3000;++i) {
            const Vec3 p=point(i,3000),q=normalized(p+east(p)*(0.1/w.config().radius));
            require(std::abs(w.elevation(p)-w.elevation(q))<0.15,"No terrain cracks over 10 cm");
        }
    });
    test("terrain","Safe spawn on land",[] {
        for(std::uint64_t seed=0;seed<12;++seed) {
            Config c; c.seed=seed; World w(c);
            require(w.elevation(w.findLand())>c.seaLevel,"Spawn is dry land");
        }
    });
    test("modules","Core survives all modules disabled",[] {
        World w;
        for(unsigned i=0;i<moduleCount;++i) w.modules().set(static_cast<Module>(i),false);
        near(w.surface({1,0,0}),0,0,"Bare sphere surface");
        const auto s=w.sample({1,0,0},0);
        require(!s.ocean && s.pressure==0 && std::isnan(s.temperature),"Vacuum state");
        Explorer e(w); e.update(w,{1,0,0,false,false},1);
        near(e.altitude(),Explorer::eyeHeight,1e-6,"Core collision still works");
    });
    test("modules","Dependencies restore without losing requested state",[] {
        Modules m; m.set(Module::Atmosphere,false);
        require(m.requested(Module::Climate) && !m.enabled(Module::Climate),"Climate suspended");
        require(m.requested(Module::Biomes) && !m.enabled(Module::Biomes),"Biomes suspended");
        m.set(Module::Atmosphere,true);
        require(m.enabled(Module::Climate) && m.enabled(Module::Biomes),"Dependencies restored");
        m.set(Module::Climate,false); require(!m.enabled(Module::Biomes),"Biomes need climate");
        require(m.enabled(Module::Ocean) && m.enabled(Module::Terrain),"Independent modules remain active");
    });
    test("modules","Ocean switch reveals seafloor",[] {
        World w;
        for(int i=0;i<300;++i) {
            const Vec3 p=point(i,300);
            if(w.elevation(p)<-100) {
                near(w.surface(p),0,0,"Ocean collision surface");
                w.modules().set(Module::Ocean,false);
                near(w.surface(p),w.elevation(p),0,"Seafloor collision surface"); return;
            }
        }
        throw std::runtime_error("No ocean test point");
    });
    test("modules","All module combinations yield valid samples",[] {
        World w;
        for(unsigned bits=0;bits<(1U<<moduleCount);++bits) {
            for(unsigned i=0;i<moduleCount;++i) w.modules().set(static_cast<Module>(i),(bits&(1U<<i))!=0);
            const auto s=w.sample(direction(0.5,0.3),100*86400);
            require(std::isfinite(s.elevation) && std::isfinite(s.pressure) && std::isfinite(s.precipitation),"Finite sample");
            require(w.modules().enabled(Module::Phenomena)==(w.modules().requested(Module::Phenomena) && w.modules().enabled(Module::Atmosphere)),"Phenomena dependency in every mask");
        }
    });
    test("navigation","Latitude longitude coordinate roundtrip",[] {
        for(double lat:{-80.,0.,35.,80.}) for(double lon:{-170.,0.,95.,170.}) {
            const auto p=direction(radians(lat),radians(lon));
            near(degrees(latitude(p)),lat,1e-10,"Latitude roundtrip");
            near(degrees(longitude(p)),lon,1e-10,"Longitude roundtrip");
            near(dot(north(p),east(p)),0,1e-12,"Orthonormal tangent frame");
        }
    });
    test("navigation","Metric walking speed and east direction",[] {
        World w; w.modules().set(Module::Terrain,false); w.modules().set(Module::Ocean,false);
        Explorer e(w); e.teleport(w,{1,0,0}); const auto initial=e.up();
        e.update(w,{1,0,0,false,false},4);
        near(length(e.up()-initial)*(w.config().radius+e.altitude()),10,0.001,"Walking ten metres");
        e.teleport(w,{1,0,0}); e.update(w,{0,1,0,false,false},4);
        require(longitude(e.up())>0,"D moves east while facing north");
        e.teleport(w,{1,0,0}); e.look(pi/2,0);
        near(dot(e.horizontalForward(),east(e.up())),1,1e-12,"Mouse right turns east");
    });
    test("navigation","Diagonal speed is normalized",[] {
        World w; w.modules().set(Module::Terrain,false);
        Explorer a(w),b(w); const auto initial=a.up();
        a.update(w,{1,0,0,false,false},2); b.update(w,{1,1,0,false,false},2);
        near(length(a.up()-initial),length(b.up()-initial),1e-12,"Diagonal is not faster");
    });
    test("navigation","Jump follows gravity and lands",[] {
        World w; w.modules().set(Module::Terrain,false); Explorer e(w);
        const double floor=e.altitude();
        e.update(w,{0,0,0,false,true},1.0/120);
        double apex=e.altitude();
        for(int i=0;i<240;++i) { e.update(w,{},1.0/120); apex=std::max(apex,e.altitude()); }
        near(apex-floor,4.5*4.5/(2*standardGravity),0.03,"Ballistic jump height");
        near(e.altitude(),floor,1e-8,"Lands on ground"); require(e.grounded(),"Grounded after landing");
    });
    test("navigation","Holding jump does not bounce automatically",[] {
        World w; w.modules().set(Module::Terrain,false); Explorer e(w);
        e.update(w,{0,0,0,false,true},3);
        near(e.altitude(),Explorer::eyeHeight,1e-8,"Only one jump while key held");
    });
    test("navigation","Pole crossing with parallel transport",[] {
        World w; w.modules().set(Module::Terrain,false);
        Explorer e(w); e.teleport(w,direction(radians(89.999),0)); e.flying=true;
        e.flightSpeed=1000; e.update(w,{1,0,0,false,false},2);
        near(length(e.up()),1,1e-12,"Unit radial direction");
        near(dot(e.up(),e.horizontalForward()),0,1e-12,"Heading remains tangent");
        require(std::abs(longitude(e.up()))>3,"Crossed pole");
        e.teleport(w,{0,1,0}); near(dot(e.up(),e.horizontalForward()),0,1e-12,"Exact pole frame");
    });
    test("navigation","Flight collision and altitude cap",[] {
        World w; Explorer e(w); e.flying=true; e.flightSpeed=20000;
        e.update(w,{0,0,-1,true,false},1);
        require(e.altitude()>=w.surface(e.up())+Explorer::eyeHeight-1e-8,"Flight stays above surface");
        e.update(w,{0,0,1,true,false},1);
        near(e.altitude(),100000,1e-8,"Flight altitude limit");
    });
    test("navigation","Framerate independent fixed substeps",[] {
        World w; w.modules().set(Module::Terrain,false); Explorer a(w),b(w);
        for(int i=0;i<120;++i) a.update(w,{1,0,0,false,false},1.0/30);
        for(int i=0;i<480;++i) b.update(w,{1,0,0,false,false},1.0/120);
        near(length(a.up()-b.up()),0,1e-12,"Same trajectory across frame rates");
    });
    test("climate","Annual global solar energy",[] {
        World w; double total=0;
        for(int i=0;i<10000;++i) total+=w.annualInsolation(latitude(point(i,10000)));
        const auto& c=w.config();
        near(total/10000,c.solarConstant/(4*std::sqrt(1-c.eccentricity*c.eccentricity)),0.1,"Annual global flux");
    });
    test("climate","Global climatology remains terrestrial",[] {
        World w; double low=1e9,high=-1e9;
        for(int i=0;i<600;++i) for(const double day:{0.,90.,180.,270.}) {
            const auto s=w.sample(point(i,600),day*86400);
            require(s.temperature>150 && s.temperature<345,"Temperature range");
            require(s.pressure>0 && s.density>0,"Air properties");
            require(s.precipitation>=0 && s.precipitation<5000,"Rainfall range");
            low=std::min(low,s.temperature); high=std::max(high,s.temperature);
        }
        require(high-low>40,"Latitude, seasons and elevation affect climate");
    });
    test("climate","Standard atmosphere baseline with climate disabled",[] {
        World w; w.modules().set(Module::Terrain,false); w.modules().set(Module::Climate,false);
        const auto s=w.sample({1,0,0},0);
        near(s.temperature,288.15,1e-8,"Standard baseline temperature");
        near(s.pressure,101325,1e-8,"Standard baseline pressure");
        require(s.biome==Biome::Rock && s.precipitation==0,"No hidden climate model");
    });
    test("climate","Invalid physical configuration rejected",[] {
        for(int i=0;i<4;++i) {
            Config c;
            if(i==0) c.radius=-1;
            if(i==1) c.eccentricity=1;
            if(i==2) c.emissivity=0;
            if(i==3) c.year=0;
            bool rejected=false;
            try { World w(c); } catch(const std::invalid_argument&) { rejected=true; }
            require(rejected,"Invalid configuration throws");
        }
    });
    test("detail","Stable objects in overlapping neighbourhoods",[] {
        World w;
        const Vec3 p=w.findLand(),q=normalized(p+east(p)*(17/w.config().radius));
        const auto a=surfaceObjects(w,p,180),b=surfaceObjects(w,q,180),again=surfaceObjects(w,p,180);
        require(a.size()>30 && a.size()==again.size(),"Populated, reproducible neighbourhood");
        std::map<std::uint64_t,SurfaceObject> lookup;
        for(const auto& o:b) require(lookup.emplace(o.id,o).second,"No duplicate object identities");
        int shared=0;
        for(std::size_t i=0;i<a.size();++i) {
            const auto& o=a[i];
            require(o.id==again[i].id && o.kind==again[i].kind,"Query order does not alter generation");
            if(o.kind==DetailKind::Grass || o.kind==DetailKind::Shrub || length(o.up-q)*w.config().radius>179) continue;
            require(lookup.contains(o.id),"Objects in overlap cannot disappear on mesh recenter");
            const auto& other=lookup.at(o.id);
            near(length(o.up-other.up),0,0,"Position stays fixed");
            near(o.elevation,other.elevation,0,"Base stays fixed");
            near(o.height,other.height,0,"Shape stays fixed");
            ++shared;
        }
        require(shared>10,"Compared enough shared rocks and trees");
        Config c; c.seed=43; World different(c);
        const auto changed=surfaceObjects(different,different.findLand(),180);
        require(!changed.empty() && changed.front().id!=a.front().id,"Seed changes object identities");
    });
    test("detail","Cube edges corners poles and dateline have stable coverage",[] {
      for(double radius:{100000.,6371000.}) {
        Config c; c.radius=radius; c.seaLevel=-5000; World w(c);
        w.modules().set(Module::Rivers,false); // This fixture checks populated dry cube charts, not wet channels.
        w.modules().set(Module::Biomes,false);
        std::size_t total=0;
        for(Vec3 p:std::vector<Vec3>{{1,1,0},{1,-1,0},{-1,1,0},{1,0,1},{-1,0,-1},{0,1,-1},{1,1,1},{-1,-1,-1},{0,1,0},{0,-1,0},{-1,0,0}}) {
            p=normalized(p);
            const auto q=normalized(p+east(p)*(20/c.radius));
            const auto a=surfaceObjects(w,p,160,false),b=surfaceObjects(w,q,160,false);
            std::map<std::uint64_t,SurfaceObject> lookup;
            for(const auto& o:b) require(lookup.emplace(o.id,o).second,"Unique face ownership");
            // On a 100 km planet the same continental heights can form cliffs:
            // slope filtering legitimately removes rocks at some corners.
            if(radius>100000) require(a.size()>5,"No empty cube seam on terrestrial terrain");
            total+=a.size();
            for(const auto& o:a) if(length(o.up-q)*c.radius<159) {
                require(lookup.contains(o.id),"Face transition preserves overlapping objects");
                near(length(o.up-lookup.at(o.id).up),0,0,"No displacement across chart change");
            }
        }
        require(total>100,"Seam comparisons cover a populated sample");
      }
    });
    test("detail","Habitat ocean and slope restrictions",[] {
        std::array<int,4> counts{};
        for(std::uint64_t seed=0;seed<6;++seed) {
            Config c; c.seed=seed; World w(c);
            const auto objects=surfaceObjects(w,w.findLand(),240);
            for(const auto& o:objects) {
                ++counts[static_cast<std::size_t>(o.kind)];
                const auto s=w.climatology(o.up);
                require(s.ocean==plantTraits(o.species).aquatic,"Plants match terrestrial or marine habitat");
                near(o.elevation,w.elevation(o.up),1e-8,"Objects meet the collision surface");
                if(o.kind!=DetailKind::Rock && !plantTraits(o.species).aquatic) {
                    require(s.temperature>267 && s.temperature<315 && s.precipitation>180,"Plant climate envelope");
                    require(s.elevation>c.seaLevel+2,"Plants avoid shoreline");
                    const double dx=(w.elevation(normalized(o.up+east(o.up)*(2/c.radius)))-s.elevation)/2;
                    const double dy=(w.elevation(normalized(o.up+north(o.up)*(2/c.radius)))-s.elevation)/2;
                    require(std::hypot(dx,dy)<0.65,"Plants avoid steep slopes");
                }
            }
            for(int i=0;i<200;++i) if(w.elevation(point(i,200))<-100) {
                require(surfaceObjects(w,point(i,200),80).empty(),"Open ocean has no props"); break;
            }
        }
        for(int count:counts) require(count>0,"All four detail kinds are represented");
    });
    test("detail","Module dependencies remove and restore detail",[] {
        World w; const auto p=w.findLand();
        const auto initial=surfaceObjects(w,p,150);
        w.modules().set(Module::SurfaceDetail,false);
        require(surfaceObjects(w,p,150).empty(),"Detail switches off independently");
        w.modules().set(Module::SurfaceDetail,true); w.modules().set(Module::Terrain,false);
        require(!w.modules().enabled(Module::SurfaceDetail) && surfaceObjects(w,p,150).empty(),"Detail waits for terrain");
        w.modules().set(Module::Terrain,true);
        const auto restored=surfaceObjects(w,p,150);
        require(initial.size()==restored.size(),"Restored identical population");
        for(std::size_t i=0;i<initial.size();++i) require(initial[i].id==restored[i].id,"Restored identities");
        w.modules().set(Module::Atmosphere,false);
        const auto vacuum=surfaceObjects(w,p,150);
        require(!vacuum.empty(),"Rocks remain without air");
        for(const auto& o:vacuum) require(o.kind==DetailKind::Rock,"No vegetation without biomes");
    });
    test("detail","Bounded query and invalid radius handling",[] {
        World w; const auto p=w.findLand();
        const auto objects=surfaceObjects(w,p,600);
        require(objects.size()<12000,"Finite local population budget");
        for(const auto& o:objects) {
            require(length(o.up-p)*w.config().radius<=600,"Within query radius");
            require(std::isfinite(o.height) && o.height>0 && o.radius>0,"Finite positive dimensions");
        }
        for(double radius:{-1.,0.,601.,std::numeric_limits<double>::infinity()}) {
            bool rejected=false;
            try { (void)surfaceObjects(w,p,radius); } catch(const std::invalid_argument&) { rejected=true; }
            require(rejected,"Reject unbounded query");
        }
    });
    test("detail","Trunk collision and module toggle invalidate navigation cache",[] {
        World w; const auto objects=surfaceObjects(w,w.findLand(),180,false);
        const auto tree=std::find_if(objects.begin(),objects.end(),[](const auto& o) { return o.kind==DetailKind::Tree; });
        require(tree!=objects.end(),"Test site contains a tree");
        const double clearance=tree->collisionRadius()+0.3;
        const Vec3 start=normalized(tree->up-north(tree->up)*((clearance+1)/w.config().radius));
        Explorer walker(w); walker.teleport(w,start);
        walker.update(w,{1,0,0,false,false},1.2);
        require(length(walker.up()-tree->up)*w.config().radius>=clearance-0.02,"Cannot walk through trunk");
        require(length(walker.up()-start)*w.config().radius>0.5,"Walking reaches obstacle");
        w.modules().set(Module::SurfaceDetail,false);
        walker.update(w,{1,0,0,false,false},1.2);
        require(dot(walker.up()-tree->up,north(tree->up))>0,"Disabled obstacle no longer blocks movement");
        w.modules().set(Module::SurfaceDetail,true);
        walker.teleport(w,start); walker.flying=true; walker.flightSpeed=5;
        walker.update(w,{1,0,0,false,false},1);
        require(dot(walker.up()-tree->up,north(tree->up))>0,"Free flight passes objects");
    });
    test("weather","Saturation agrees with reference water vapour pressures",[] {
        near(saturationPressure(273.15),610.78,0.01,"Freezing-point vapour pressure");
        near(saturationPressure(293.15),2338.1,3,"20 C vapour pressure");
        near(saturationMixingRatio(293.15,101325),0.01469,0.00003,"20 C mixing ratio");
        require(saturationPressure(303.15)>saturationPressure(293.15),"Warm air has higher saturation pressure");
    });
    test("weather","Condensation conserves water and moist enthalpy",[] {
        double vapour=10,cloud=0,temperature=278;
        constexpr double mass=800,pressure=80000,cp=1004,latent=2.5e6;
        const double energy=cp*mass*temperature+latent*vapour;
        equilibrateMoisture(vapour,cloud,temperature,mass,pressure);
        require(cloud>0 && temperature>278,"Condensation releases latent heat");
        near(vapour+cloud,10,1e-12,"Water conserved by phase change");
        near(cp*mass*temperature+latent*vapour,energy,1e-5,"Moist enthalpy conserved");
        near(vapour,mass*saturationMixingRatio(temperature,pressure),1e-9,"Saturation reached");
    });
    test("weather","Evaporation cools air and stops when liquid runs out",[] {
        double vapour=1,cloud=0.6,temperature=288;
        const double energy=1004*800*temperature+2.5e6*vapour;
        equilibrateMoisture(vapour,cloud,temperature,800,85000);
        near(cloud,0,1e-12,"Available liquid evaporated"); near(vapour,1.6,1e-12,"Vapour gained the liquid");
        require(temperature<288,"Latent heat cools the air");
        near(1004*800*temperature+2.5e6*vapour,energy,1e-5,"Evaporation enthalpy");
    });
    test("weather","Uniform flow transports a packet exactly one cell",[] {
        constexpr int n=16; std::vector<double> water(n*n); water[4*n+3]=7;
        const std::vector<Wind2> wind(n*n,{5,0});
        const auto moved=transportWater(water,wind,n,100,20);
        near(moved[4*n+4],7,1e-12,"Advection direction and metric speed");
        near(std::accumulate(moved.begin(),moved.end(),0.0),7,1e-12,"Packet mass");
        for(int i=0;i<n;++i) water=transportWater(water,wind,n,100,20);
        near(water[4*n+3],7,1e-12,"Periodic traversal without mass loss");
    });
    test("weather","Divergent flow preserves mass and nonnegative columns",[] {
        constexpr int n=16; std::vector<double> water(n*n); std::vector<Wind2> wind(n*n);
        for(std::size_t i=0;i<water.size();++i) {
            water[i]=randomUnit(i+30); wind[i]={16*randomUnit(i+400)-8,16*randomUnit(i+800)-8};
        }
        const double initial=std::accumulate(water.begin(),water.end(),0.0);
        for(int i=0;i<200;++i) water=transportWater(water,wind,n,100,2);
        near(std::accumulate(water.begin(),water.end(),0.0),initial,1e-10,"Conservative face fluxes");
        for(double value:water) require(value>=0 && std::isfinite(value),"Positive water under compression");
        bool rejected=false;
        try { (void)transportWater(water,wind,n,100,100); } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected,"An unstable CFL number must be rejected");
    });
    test("weather","Fixed steps reproduce evolution across frame rates",[] {
        World world; const auto p=world.findLand(); Weather a(world,p,79*86400),b(world,p,79*86400);
        const auto first=a.clouds().front(); const auto start=a.cells().front();
        a.advance(world,p,120);
        for(int i=0;i<480;++i) b.advance(world,p,0.25);
        require(a.steps()==60 && b.steps()==60,"Same number of physical steps");
        for(std::size_t i=0;i<a.cells().size();++i) {
            near(a.cells()[i].vapour,b.cells()[i].vapour,0,"Frame-independent vapour");
            near(a.cells()[i].cloud,b.cells()[i].cloud,0,"Frame-independent cloud");
            near(a.cells()[i].wind.east,b.cells()[i].wind.east,0,"Frame-independent wind");
        }
        require(std::hypot(a.clouds().front().x-first.x,a.clouds().front().z-first.z)>20,"Cloud tracers advect");
        require(std::abs(a.cells().front().wind.east-start.wind.east)>0.01,"Wind responds to forces");
    });
    test("weather","Projected face flow preserves a uniform moisture field",[] {
        constexpr int n=16;
        std::vector<Wind2> faces(n*n);
        double initialEnergy=0;
        for(std::size_t i=0;i<faces.size();++i) {
            faces[i]={12*randomUnit(i)-6,12*randomUnit(i+100)-6};
            initialEnergy+=faces[i].east*faces[i].east+faces[i].north*faces[i].north;
        }
        projectWind(faces,n,100);
        const auto at=[&](int x,int z)->const Wind2& { return faces[static_cast<std::size_t>(((z+n)%n)*n+(x+n)%n)]; };
        double energy=0;
        for(int z=0;z<n;++z) for(int x=0;x<n;++x) {
            const auto c=at(x,z);
            near((c.east-at(x-1,z).east+c.north-at(x,z-1).north)/100,0,1e-12,"Discrete divergence removed");
            energy+=c.east*c.east+c.north*c.north;
        }
        require(energy<=initialEnergy+1e-8,"Pressure projection cannot add kinetic energy");
        std::vector<double> water(n*n,3.0);
        for(int i=0;i<100;++i) water=transportWater(water,faces,n,100,2,true);
        for(const auto v:water) near(v,3,1e-11,"No artificial drying or concentration of a uniform layer");
    });
    test("weather","Integrated sources sinks and water budget remain finite",[] {
        World world; const auto p=world.findLand(); Weather weather(world,p,79*86400);
        for(int i=0;i<60;++i) weather.advance(world,p,60);
        const auto budget=weather.budget();
        require(budget.evaporatedKg>0 && budget.precipitatedKg>0,"Both moisture supply and precipitation occur");
        require(std::abs(budget.residualKg())<budget.initialKg*1e-10,"Closed water accounting including rain");
        for(const auto& c:weather.cells()) {
            require(c.vapour>=0 && c.cloud>=0 && std::isfinite(c.cloud),"Nonnegative finite water");
            require(std::isfinite(c.temperature) && c.temperature>180 && c.temperature<340,"Terrestrial layer temperature");
            require(std::hypot(c.wind.east,c.wind.north)<43,"Bounded wind maintains stable transport");
        }
    });
    test("weather","Wind and cloud switches suspend the right state",[] {
        World world; const auto p=world.findLand(); Weather weather(world,p,79*86400);
        const auto initial=weather.cells();
        world.modules().set(Module::Wind,false); weather.advance(world,p,60);
        require(!weather.windEnabled() && !weather.cloudsEnabled() && weather.steps()==0,"Wind and dependent clouds suspended");
        for(std::size_t i=0;i<initial.size();++i) near(weather.cells()[i].vapour,initial[i].vapour,0,"No hidden moisture integration");
        world.modules().set(Module::Wind,true); world.modules().set(Module::Clouds,false); weather.advance(world,p,20);
        require(weather.windEnabled() && !weather.cloudsEnabled() && weather.steps()==10,"Independent wind continues");
        for(std::size_t i=0;i<initial.size();++i) near(weather.cells()[i].cloud,initial[i].cloud,0,"Cloud water frozen when disabled");
        world.modules().set(Module::Clouds,true); weather.advance(world,p,20);
        require(weather.cloudsEnabled(),"Requested clouds restore");
        world.modules().set(Module::Atmosphere,false); weather.advance(world,p,0);
        require(!weather.windEnabled() && !weather.cloudsEnabled(),"Atmosphere dependency is enforced");
    });
    test("weather","Domain movement preserves overlap and accounts for exchange",[] {
        World world; const auto p=world.findLand(); Weather weather(world,p,79*86400);
        weather.advance(world,p,30); const auto before=weather.sample(p); const auto steps=weather.steps();
        const auto q=normalized(p+east(p)*(6000/world.config().radius));
        weather.advance(world,q,0); const auto after=weather.sample(p); const auto budget=weather.budget();
        require(after.available && weather.sample(q).available,"Overlapping old and new locations remain sampled");
        near(after.cloudWater,before.cloudWater,0.02,"Cloud field survives local-domain movement");
        require(weather.steps()==steps,"Moving the domain does not integrate hidden time");
        require(std::abs(budget.residualKg())<budget.initialKg*1e-10,"New and leaving domain water is accounted for");
    });
    test("hydrology","Lake at rest over uneven bed stays exactly still",[] {
        RunoffGrid grid(10,4); std::vector<double> rain(grid.cells.size());
        for(std::size_t i=0;i<grid.cells.size();++i) {
            auto& c=grid.cells[i]; c.rock=0.3*std::sin(static_cast<double>(i)); c.water=2-c.bed(); c.conductivity=0;
        }
        const auto initial=grid.cells; const double volume=grid.waterVolume();
        grid.advance(30,rain,0,false,false,false,0);
        near(grid.waterVolume(),volume,1e-9,"Closed lake mass");
        for(std::size_t i=0;i<initial.size();++i) near(grid.cells[i].water,initial[i].water,1e-12,"Lake level");
        for(double q:grid.eastFlow) near(q,0,1e-13,"No spurious east current");
        for(double q:grid.northFlow) near(q,0,1e-13,"No spurious north current");
    });
    test("hydrology","Manning uniform flow and dry barrier",[] {
        const double h=0.1,n=0.04,slope=0.01,dx=4;
        const double q=std::pow(h,5.0/3)*std::sqrt(slope)/n;
        near(inertialDischarge(q,10,10-slope*dx,h,h,n,dx,0.25),q,1e-12,"Steady Manning balance");
        near(inertialDischarge(0,0,2,1,0,n,dx,0.25),0,0,"Dry sill above the free surface blocks water");
    });
    test("hydrology","Wet front remains nonnegative and conserves volume",[] {
        RunoffGrid grid(16,2); std::vector<double> rain(grid.cells.size());
        for(int z=0;z<16;++z) for(int x=0;x<16;++x) {
            auto& c=grid.cells[static_cast<std::size_t>(z*16+x)]; c.water=x<5 ? 0.4 : 0; c.conductivity=0;
        }
        const double initial=grid.waterVolume(); grid.advance(20,rain,0,false,false,false,0);
        near(grid.waterVolume(),initial,1e-9,"Wet/dry mass");
        require(grid.cells[8*16+7].water>0,"Water enters the dry downstream reach");
        for(const auto& c:grid.cells) require(c.water>=0 && std::isfinite(c.water),"No negative depth at the front");
    });
    test("hydrology","Rain infiltration drainage and evaporation close the ledger",[] {
        RunoffGrid grid(6,4); std::vector<double> rain(grid.cells.size(),30/3.6e6);
        for(auto& c:grid.cells) { c.soilWater=0.1; c.conductivity=3e-6; }
        const double initial=grid.waterVolume();
        const auto b=grid.advance(120,rain,2e-8,false,false,false,0);
        near(b.rainM3,6*6*16*30/3.6e6*120,1e-10,"Rain in SI units");
        near(grid.waterVolume()+b.evaporatedM3+b.drainedM3-initial-b.rainM3,0,1e-9,"Full water ledger");
        require(b.evaporatedM3>0 && b.drainedM3>0 && grid.cells[0].soilWater>0.1,"Finite infiltration and sinks");
    });
    test("hydrology","Suspended soil uses the same conservative water flux",[] {
        RunoffGrid grid(12,2); std::vector<double> rain(grid.cells.size());
        for(int z=0;z<12;++z) for(int x=0;x<12;++x) {
            auto& c=grid.cells[static_cast<std::size_t>(z*12+x)]; c.conductivity=0;
            c.rock=-0.02*x; c.water=x<4 ? 0.2 : 0; c.sediment=c.water*10;
        }
        const double water=grid.waterVolume(),solid=grid.solidMass();
        grid.advance(30,rain,0,false,false,false,0);
        near(grid.waterVolume(),water,1e-9,"Transported water"); near(grid.solidMass(),solid,1e-6,"Transported solids");
        require(grid.cells[6*12+6].sediment>0,"Sediment travels with the wet front");
        for(const auto& c:grid.cells) require(c.sediment>=0 && c.soil==1,"Positive suspension with bed exchange disabled");
    });
    test("hydrology","Erosion deposition and bedrock conserve solid mass",[] {
        RunoffGrid grid(12,2); std::vector<double> rain(grid.cells.size());
        for(int z=0;z<12;++z) for(int x=0;x<12;++x) {
            auto& c=grid.cells[static_cast<std::size_t>(z*12+x)]; c.rock=-0.04*x;
            c.soil=c.initialSoil=1e-5; c.water=0.15; c.conductivity=0;
        }
        const double mass=grid.solidMass(); const auto b=grid.advance(30,rain,0,true,false,false,0);
        require(b.erodedKg>0,"Flow detaches soil above the critical shear");
        near(grid.solidMass(),mass,1e-7,"Eroded solids are carried or redeposited");
        for(const auto& c:grid.cells) require(c.soil>=0 && c.bed()>=c.rock,"Bedrock is not eroded");
        RunoffGrid dry(2,1); std::vector<double> none(4);
        dry.cells[0].sediment=0.8; const auto old=dry.solidMass();
        const auto settled=dry.advance(1,none,0,true,false,false,0);
        near(settled.depositedKg,0.8,1e-12,"Dry suspended load settles onto bed");
        near(dry.cells[0].soil,1+0.8/RunoffGrid::soilDensity,1e-12,"Deposit raises terrain in metres");
        near(dry.solidMass(),old,1e-9,"Dry settling conserves solids");
    });
    test("hydrology","Open and sea boundaries account for exported mass",[] {
        RunoffGrid grid(8,2); std::vector<double> rain(grid.cells.size());
        for(auto& c:grid.cells) { c.water=0.1; c.sediment=1; c.conductivity=0; }
        const double water=grid.waterVolume(),solid=grid.solidMass();
        const auto b=grid.advance(10,rain,0,false,true,false,0);
        require(b.boundaryM3>0 && b.sedimentOutKg>0,"Open edges export water and sediment");
        near(grid.waterVolume()+b.boundaryM3,water,1e-9,"Open water ledger");
        near(grid.solidMass()+b.sedimentOutKg,solid,1e-6,"Open sediment ledger");
        RunoffGrid coast(4,2); std::vector<double> dry(16);
        for(auto& c:coast.cells) { c.rock=-2; c.conductivity=0; }
        for(int z=0;z<4;++z) coast.cells[static_cast<std::size_t>(z*4)].sea=true;
        const auto fill=coast.advance(10,dry,0,false,false,true,0);
        require(fill.boundaryM3<0,"Sea reservoir supplies a below-sea-level basin");
        near(coast.waterVolume()+fill.boundaryM3,0,1e-8,"Sea input is explicitly measured");
    });
    test("hydrology","Fixed coupling clock is frame independent and pause retains fields",[] {
        World w; w.modules().set(Module::Wind,false); const auto p=w.findLand();
        Simulation a(w,p,0),b(w,p,0); a.setRainExperiment(30); b.setRainExperiment(30);
        a.advance(w,p,20); for(int i=0;i<80;++i) b.advance(w,p,0.25);
        for(std::size_t i=0;i<a.water.grid().cells.size();++i) {
            near(a.water.grid().cells[i].water,b.water.grid().cells[i].water,0,"Frame-independent ground water");
            near(a.water.grid().cells[i].soil,b.water.grid().cells[i].soil,0,"Frame-independent erosion");
        }
        const auto before=a.water.budget(); a.advance(w,p,0);
        near(a.water.budget().storedWaterM3,before.storedWaterM3,0,"Paused water");
        w.modules().set(Module::Hydrology,false); a.advance(w,p,60);
        require(!a.water.enabled() && !a.water.erosionEnabled(),"Water disables dependent erosion");
        near(a.water.budget().storedWaterM3,before.storedWaterM3,0,"Disabled fields retain state");
        w.modules().set(Module::Hydrology,true); a.advance(w,p,0);
        require(a.water.enabled() && a.water.erosionEnabled(),"Modules restore");
        w.modules().set(Module::Terrain,false); a.advance(w,p,0);
        require(!a.water.enabled(),"Water depends on terrain");
        require(std::abs(before.waterResidual())<1e-7 && std::abs(before.solidResidual())<1e-4,"Coupled ledgers");
    });
    test("hydrology","Moving local domain retains overlapping and cached soil",[] {
        World w; w.modules().set(Module::Wind,false); const auto p=w.findLand(); Simulation sim(w,p,0);
        sim.setRainExperiment(80); sim.advance(w,p,60); const auto before=sim.water.sample(p);
        sim.advance(w,normalized(p+east(p)*(80/w.config().radius)),0);
        const auto overlap=sim.water.sample(p);
        near(overlap.depth,before.depth,1e-10,"Exact overlap on the fixed lattice");
        sim.advance(w,normalized(p+east(p)*(400/w.config().radius)),0); sim.advance(w,p,0);
        const auto restored=sim.water.sample(p); near(restored.depth,before.depth,1e-10,"Cached water returns");
        near(restored.bedChange,before.bedChange,1e-10,"Cached soil returns");
        const auto b=sim.water.budget(); require(std::abs(b.waterResidual())<1e-6 && std::abs(b.solidResidual())<1e-4,"Domain ledger includes entering/leaving cells");
    });
    test("hydrology","Weather precipitation reaches the ground once",[] {
        World w; const auto p=w.findLand(); Weather weather(w,p,79*86400); weather.advance(w,p,2);
        Vec3 wet=p; double maximum=0;
        for(int z=0;z<Weather::resolution;++z) for(int x=0;x<Weather::resolution;++x) {
            const auto q=weather.directionAt((x+0.5)*Weather::spacing-Weather::extent/2,(z+0.5)*Weather::spacing-Weather::extent/2);
            const double rain=weather.sample(q).rainRate;
            if(rain>maximum && w.elevation(q)>0) { wet=q; maximum=rain; }
        }
        require(maximum>0,"Test location has actual cloud precipitation");
        Hydrology water(w,wet); double expected=0;
        for(int z=0;z<Hydrology::resolution;++z) for(int x=0;x<Hydrology::resolution;++x)
            if(!water.grid().cells[static_cast<std::size_t>(z*Hydrology::resolution+x)].sea)
                expected+=weather.sample(water.directionAt(water.cellX(x),water.cellZ(z))).rainRate/1000*2*Hydrology::spacing*Hydrology::spacing;
        water.step(w,weather,2);
        near(water.budget().rainM3,expected,1e-9,"Precipitation kg/m2 converted to metres exactly once");
        require(expected>0,"Ground receives weather-driven rainfall");
    });
    test("hydrology","Poles large relocations and invalid inputs are bounded",[] {
        World w; w.modules().set(Module::Wind,false);
        Simulation sim(w,{0,1,0},0); sim.setRainExperiment(10); sim.advance(w,{0,1,0},2);
        sim.advance(w,{0,-1,0},2); sim.advance(w,{-1,0,0},2);
        const auto budget=sim.water.budget(); require(std::isfinite(budget.storedWaterM3),"Finite polar/antipodal state");
        require(std::abs(budget.waterResidual())<1e-6 && std::abs(budget.solidResidual())<1e-4,"Relocation ledger");
        for(double dt:{-1.0,121.0,std::numeric_limits<double>::quiet_NaN()}) {
            bool rejected=false; try { sim.advance(w,{-1,0,0},dt); } catch(const std::invalid_argument&) { rejected=true; }
            require(rejected,"Invalid coupled timestep rejected");
        }
        bool rejected=false; try { sim.setRainExperiment(201); } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected,"Unbounded experiment forcing rejected");
    });
    test("hydrology","Walking uses the same ground height as the runoff mesh",[] {
        World w; w.modules().set(Module::SurfaceDetail,false);
        Explorer explorer(w); Hydrology water(w,explorer.up());
        explorer.teleport(w,explorer.up(),&water); explorer.update(w,{},1,&water);
        near(explorer.altitude(),water.ground(w,explorer.up())+Explorer::eyeHeight,1e-9,"Standing on the local mesh");
        explorer.update(w,{1,0,0,false,false},1,&water);
        require(explorer.altitude()>=water.ground(w,explorer.up())+Explorer::eyeHeight-1e-6,"No walking through ground");
        w.modules().set(Module::Hydrology,false); water.synchronize(w,explorer.up());
        near(water.ground(w,explorer.up()),w.surface(explorer.up()),0,"Disabled hydrology restores procedural collision surface");
    });
    test("weather","Poles pause and invalid timesteps are handled",[] {
        World world;
        for(const auto p:std::vector<Vec3>{{0,1,0},{0,-1,0},{-1,0,0}}) {
            Weather weather(world,p,79*86400); weather.advance(world,p,20);
            const auto air=weather.sample(p); require(air.available && std::isfinite(air.humidity),"Finite polar weather");
            near(dot(air.velocity,p),0,1e-10,"Horizontal wind is tangent to the sphere");
            const auto particle=weather.clouds().front(); const auto count=weather.steps();
            weather.advance(world,p,0);
            near(weather.clouds().front().x,particle.x,0,"Paused tracers stay fixed");
            require(weather.steps()==count,"Pause does not step the model");
            bool rejected=false;
            try { weather.advance(world,p,121); } catch(const std::invalid_argument&) { rejected=true; }
            require(rejected,"Oversized active timestep is rejected, not silently dropped");
        }
    });
}
}
int main(int argc,char** argv) {
    registerTests(); int passed=0,failed=0;
    const std::string suite=argc>1 ? argv[1] : "all";
    for(const auto& t:tests) {
        if(suite!="all" && suite!=t.suite) continue;
        try { t.run(); ++passed; std::cout << "PASS " << t.suite << "/" << t.name << '\n'; }
        catch(const std::exception& error) { ++failed; std::cerr << "FAIL " << t.name << ": " << error.what() << '\n'; }
    }
    std::cout << passed << " passed; " << failed << " failed\n";
    return failed || passed==0 ? 1 : 0;
}
