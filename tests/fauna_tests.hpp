// Included in the core test harness namespace.
struct FaunaFixture {
    World world;
    DeerHabitat site;
    Vec3 observer;
    double altitude;
    Fauna animals;
    FaunaFixture() {
        static const World reference;
        static const auto habitat=findDeerHabitat(reference,reference.findLand());
        world=reference;site=habitat;
        observer=normalized(site.home+site.outward*(110/world.config().radius));
        altitude=world.surface(observer)+Explorer::eyeHeight;
        require(animals.focus(world,site),"A complete herd fits the habitat");
        animals.advance(world,observer,0,altitude,true,false);
    }
    void advance(double seconds) {animals.advance(world,observer,seconds,altitude,true,false);}
};
void registerFaunaTests() {
    test("fauna","Seeded freshwater habitats support stable groups of three to seven",[] {
        for(std::uint64_t seed:{0ULL,42ULL,73ULL,119ULL}) {
            Config config;config.seed=seed;World w(config);
            const auto site=findDeerHabitat(w,w.findLand()),same=findDeerHabitat(w,w.findLand());
            require(site.id==same.id,"Habitat identity is deterministic");
            near(length(site.home-same.home),0,0,"Habitat location is deterministic");
            const auto water=liquidAt(w,nullptr,nullptr,site.water);
            require(water.present && !water.ocean,"A real freshwater source is required");
            Fauna a,b;require(a.focus(w,site) && b.focus(w,site),"Dry clear ground supports the whole herd");
            const auto& first=a.herds().front();const auto count=first.members.size();
            require(count>=3 && count<=7,"Requested group size");
            for(std::size_t i=0;i<count;++i) {
                const auto& d=first.members[i];const auto& copy=b.herds().front().members[i];
                require(d.id==copy.id && d.male==copy.male && d.size==copy.size,"Stable individual identity and appearance");
                near(length(d.up-copy.up),0,0,"Stable initial positions");
                const auto liquid=liquidAt(w,nullptr,nullptr,d.up);require(!liquid.ocean && liquid.depth<.16,"Deer start on dry ground");
                for(std::size_t j=0;j<i;++j) require(length(d.up-first.members[j].up)*config.radius>=2.3,"No overlapping spawn bodies");
            }
        }
    });
    test("fauna","Undisturbed herds graze rest and actually reach drinking water",[] {
        FaunaFixture f;std::array<bool,5> seen{};double peakThirst=0;bool quenched=false,folded=false,ate=false;
        const auto initial=f.animals.herds().front().members;
        for(int i=0;i<2400;++i) {
            f.advance(.1);const auto& h=f.animals.herds().front();peakThirst=std::max(peakThirst,h.thirst);
            for(const auto& d:h.members) {
                seen[static_cast<unsigned>(d.activity)]=true;
                folded|=d.activity==DeerActivity::Resting && d.foldLegs>.9;
                ate|=d.activity==DeerActivity::Grazing && d.lowerHead>.9;
                if(d.activity==DeerActivity::Drinking) {
                    const auto sip=normalized(d.up-h.habitat.outward*(1.5/f.world.config().radius));
                    const auto water=liquidAt(f.world,nullptr,nullptr,sip);
                    require(water.depth>.01 && !water.ocean,"Drinking reaches actual fresh water");
                    require(d.elevation-water.level<.5 && d.speed<.2,"Accessible water while standing still");
                }
            }
            quenched|=peakThirst>.75 && h.thirst<.15;
        }
        require(seen[0] && seen[1] && seen[2] && seen[3] && !seen[4],"All peaceful activities occur without a false alarm");
        require(quenched && folded && ate,"Drinking reduces thirst, resting folds legs and feeding lowers the head");
        require(length(initial.front().up-f.animals.herds().front().members.front().up)*f.world.config().radius>2,"Grazing animals move between feeding positions");
    });
    test("fauna","Nearby observer triggers a cohesive flight over safe terrain",[] {
        FaunaFixture f;const double radius=f.world.config().radius;
        f.observer=normalized(f.site.home-f.site.outward*(22/radius));f.altitude=f.world.surface(f.observer)+1.75;
        double initial=1e30;for(const auto& d:f.animals.herds().front().members) initial=std::min(initial,length(d.up-f.observer)*radius);
        for(int i=0;i<150;++i) {
            f.animals.advance(f.world,f.observer,.1,f.altitude);
            const auto& h=f.animals.herds().front();Vec3 center{};for(const auto& d:h.members) center+=d.up;center=normalized(center);
            for(const auto& d:h.members) {
                require(d.activity==DeerActivity::Fleeing,"Whole herd responds to the observer");
                require(d.speed<=12.000001,"Finite plausible running speed");
                require(length(d.up-center)*radius<70,"Fleeing animals remain a group");
                require(length(d.up-d.previous)*radius<=1.20001,"No teleportation during escape");
                const auto liquid=liquidAt(f.world,nullptr,nullptr,d.up);
                require(!liquid.ocean && liquid.depth<.16,"Escape avoids deep water");
                near(d.elevation,f.world.surface(d.up),1e-6,"Animals follow the terrain");
                for(const auto& o:h.obstacles) if(o.elevation+o.collisionHeight()>d.elevation+.25)
                    require(length(d.up-o.up)*radius>=o.collisionRadius()+.65-1e-6,"Escape avoids solid vegetation and rocks");
            }
        }
        double final=1e30;for(const auto& d:f.animals.herds().front().members) final=std::min(final,length(d.up-f.observer)*radius);
        require(final>initial+70,"Observer distance increases during escape");
        f.advance(100);require(f.animals.herds().front().activity!=DeerActivity::Fleeing,"Herd calms after danger has passed");
    });
    test("fauna","Fixed steps produce identical motion with different frame partitions",[] {
        FaunaFixture a,b;a.advance(20);for(int i=0;i<800;++i) b.advance(.025);
        require(a.animals.steps()==200 && b.animals.steps()==200,"Same number of physical steps");
        const auto& x=a.animals.herds().front();const auto& y=b.animals.herds().front();
        near(x.thirst,y.thirst,0,"Frame-independent thirst");
        for(std::size_t i=0;i<x.members.size();++i) {
            near(length(x.members[i].up-y.members[i].up),0,0,"Frame-independent position");
            near(x.members[i].stride,y.members[i].stride,0,"Frame-independent gait");
        }
        const auto before=a.animals.visible(a.observer);a.advance(.05);const auto after=a.animals.visible(a.observer);
        require(a.animals.steps()==200 && before.size()==after.size(),"Render interpolation does not advance behaviour");
        require(length(before.front().up-after.front().up)>0,"Interpolation smooths the fixed update rate");
    });
    test("fauna","Module pause distance and height suspend animals without respawning",[] {
        FaunaFixture f;f.advance(2);const auto before=f.animals.herds().front().members.front();const auto count=f.animals.steps();
        f.advance(0);require(f.animals.steps()==count,"Simulation pause freezes animals");
        f.world.modules().set(Module::Fauna,false);f.advance(30);
        require(f.animals.visible(f.observer).empty() && f.animals.steps()==count,"Disabled animals are invisible and do not simulate");
        f.world.modules().set(Module::Fauna,true);f.advance(0);
        require(f.animals.herds().front().members.front().id==before.id,"Module switch preserves individual identity");
        near(length(f.animals.herds().front().members.front().up-before.up),0,0,"No respawn after re-enabling");
        f.animals.advance(f.world,f.observer,30,f.altitude+500);
        require(f.animals.steps()==count && f.animals.visible(f.observer).empty(),"High-altitude flight suspends local animals");
        f.animals.advance(f.world,f.observer,30,f.altitude,false);
        require(f.animals.steps()==count,"Globe or underground view suspends local animals");
        const auto away=normalized(f.observer+east(f.observer)*(4000/f.world.config().radius));
        f.animals.advance(f.world,away,5,f.world.surface(away)+1.75);
        near(length(f.animals.herds().front().members.front().up-before.up),0,0,"Distant herd remains dormant");
        f.advance(0);require(!f.animals.visible(f.observer).empty(),"Cached herd returns when revisited");
        f.world.modules().set(Module::Terrain,false);f.advance(0);require(!f.animals.enabled(),"Animals require a terrain surface");
    });
    test("fauna","Cache and active population remain bounded across repeated visits",[] {
        FaunaFixture f;
        for(unsigned i=0;i<45;++i) {auto site=f.site;site.id=hash64(i+127);require(f.animals.focus(f.world,site),"Synthetic visit has valid ground");}
        require(f.animals.herds().size()==Fauna::cacheLimit,"Bounded retained herds");f.advance(.1);
        std::size_t active=0;for(const auto& h:f.animals.herds()) if(h.active) ++active;
        require(active==Fauna::maximumHerds && f.animals.visible(f.observer).size()<=21,"At most three groups simulate nearby");
        Config c;c.seed=73;World changed(c);f.animals.advance(changed,f.observer,0,10000);
        require(f.animals.herds().empty() && f.animals.steps()==0,"Changing seed discards old habitat and population");
    });
    test("fauna","Procedural articulated meshes and moving shadows are finite",[] {
        DeerPose p;p.id=73;p.size=1;p.male=true;
        for(const auto activity:{DeerActivity::Grazing,DeerActivity::Resting,DeerActivity::Drinking,DeerActivity::Fleeing}) {
            p.activity=activity;p.foldLegs=activity==DeerActivity::Resting ? 1 : 0;
            p.lowerHead=activity==DeerActivity::Grazing || activity==DeerActivity::Drinking ? 1 : 0;
            p.speed=activity==DeerActivity::Fleeing ? 11 : 0;
            for(double stride:{0.,.23,.65}) {
                p.stride=stride;const auto mesh=deerMesh(p),coarse=deerMesh(p,true);
                require(!coarse.empty() && coarse.size()<mesh.size(),"Distance LOD reduces the geometry");
                std::vector<Triangle> triangles;double volume=0;
                for(const auto& f:mesh) {
                    const auto& t=f.triangle;require(std::isfinite(length(t.a)) && length(cross(t.b-t.a,t.c-t.a))>1e-10,"Finite nondegenerate faces");
                    volume+=dot(t.a,cross(t.b,t.c))/6;triangles.push_back(t);
                }
                require(volume>0,"Outward mesh winding supports closed shadow volumes");
                const auto shadows=shadowVolume(triangles,normalized(Vec3{1,2,3}),150,.015);
                require(!shadows.empty(),"Animated geometry casts shadows");
                for(const auto& t:shadows) require(std::isfinite(length(t.a)+length(t.b)+length(t.c)),"Finite shadow geometry");
            }
        }
    });
    test("fauna","Invalid times and observer coordinates are rejected before updating",[] {
        World w;Fauna f;
        for(double seconds:{-1.,3601.,std::numeric_limits<double>::quiet_NaN()}) {
            bool rejected=false;try {f.advance(w,{1,0,0},seconds,10);} catch(const std::invalid_argument&) {rejected=true;}
            require(rejected,"Invalid time is rejected");
        }
        bool rejected=false;try {f.advance(w,{},1,10);} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected && f.herds().empty(),"Invalid observer cannot populate animals");
        w.modules().set(Module::Fauna,false);rejected=false;
        try {(void)findDeerHabitat(w,{1,0,0});} catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"Disabled visit reports a clear failure");
    });
}
