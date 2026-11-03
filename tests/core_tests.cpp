#include "planet/navigation.hpp"
#include "planet/weather.hpp"
#include "planet/hydrology.hpp"
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
void registerTests() {
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
            require(std::isfinite(h) && h>-6000 && h<8000,"Terrestrial terrain range");
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
                require(!s.ocean,"No underwater decorations");
                near(o.elevation,w.elevation(o.up),1e-8,"Objects meet the collision surface");
                if(o.kind!=DetailKind::Rock) {
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
