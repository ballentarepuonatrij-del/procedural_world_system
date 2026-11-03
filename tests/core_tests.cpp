#include "planet/navigation.hpp"
#include <functional>
#include <iostream>
#include <limits>
#include <map>
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
void registerTests() {
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
        World w;
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
