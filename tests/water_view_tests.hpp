void registerWaterViewTests() {
    test("water_view","Frustum retains crossing triangles and rejects shared outside planes",[] {
        ViewFrustum view({},Vec3{0,0,-1},Vec3{0,1,0},pi/2,1,1,100);
        require(view.outside({0,0,-10})==0,"Visible point");
        const std::array points{Vec3{-11,0,-10},Vec3{11,0,-10},Vec3{0,-11,-10},Vec3{0,11,-10},Vec3{0,0,1},Vec3{0,0,-101}};
        for(std::size_t i=0;i<points.size();++i) require((view.outside(points[i])&(1U<<i))!=0,"Each clipping plane rejects its exterior");
        const auto a=view.outside({-30,-15,-10}),b=view.outside({30,-15,-10}),c=view.outside({0,30,-10});
        require(a && b && c && !(a&b&c),"Triangle crossing the entire viewport survives even with every vertex outside");
        require((view.outside({-20,0,-10})&view.outside({-21,1,-10})&view.outside({-22,-1,-10}))!=0,"Invisible triangle rejected");
        require(view.outside({11,0,-10},1)==0,"Possible displacement across the edge prevents premature culling");
        require(view.outside({0,0,-.5},.6)==0,"Wave may cross the near plane");
    });
    test("water_view","View basis handles moving cameras tall windows poles and look directions",[] {
        const Vec3 eye{510,1400,-220},forward=normalized(Vec3{.3,-.8,.4}),up{0,1,0};
        ViewFrustum a(eye,forward,up,radians(72),1.6,.08,400000);
        const Vec3 shift{7500,-5300,1980};ViewFrustum b(eye+shift,forward,up,radians(72),1.6,.08,400000);
        for(int i=0;i<1000;++i) {
            const auto p=eye+point(i,1000)*400;
            require(a.outside(p)==b.outside(p+shift),"Translation preserves the view volume");
        }
        for(auto observer:{Vec3{0,1,0},Vec3{0,-1,0},direction(.4,pi)}) {
            const auto right=east(observer),forwardAtPole=north(observer);
            ViewFrustum v({},forwardAtPole,observer,radians(72),.5,.25,100);
            require(v.outside(forwardAtPole*10)==0,"Polar camera looks along its tangent");
            require(v.outside(forwardAtPole*10+right*8)!=0,"Portrait viewport has a narrow horizontal field");
        }
        bool rejected=false;try {ViewFrustum bad({},Vec3{0,0,1},Vec3{0,1,0},pi/2,0,1,100);}catch(const std::invalid_argument&) {rejected=true;}
        require(rejected,"Invalid projection rejected");
    });
    test("water_view","Wave displacement bound covers every resolved spectrum and depth",[] {
        World world;const auto p=world.findCoast().lookToward;Weather weather(world,p,79*86400);
        for(double speed:{0.,4.,15.,40.}) {
            WaterMotion motion(world,p);motion.setWindExperiment(speed);motion.synchronize(world,weather,p);
            const double bound=2*motion.significantHeight();
            for(int i=0;i<40;++i) {
                motion.advanceClock(.39);const auto q=normalized(p+east(p)*((i-20)*3/world.config().radius));
                for(bool ocean:{false,true}) for(double depth:{.01,.2,3.,500.}) for(double wavelength:{0.,4.,16.,48.,72.})
                    require(std::abs(motion.waves(q,depth,ocean,wavelength).height)<=bound+1e-12,"Conservative bound covers phase filtering and shallow water");
            }
        }
    });
    test("water_view","Cached water retains dry shoreline neighbours and distinct water bodies",[] {
        World world;const auto p=world.findLand();Hydrology water(world,p);
        std::array<SurfaceWaterVertex,6> vertices{};
        for(auto& v:vertices) {v.up=p;v.available=true;v.spacing=2;v.altitude=10;}
        vertices[0].altitude=-5; // sea
        vertices[1].inland=true;vertices[1].level=12; // lake
        vertices[2].inland=true;vertices[2].level=9; // dry shore
        vertices[3].river=true;vertices[3].altitude=-10; // separate river ribbon
        vertices[4].inland=true;vertices[4].level=10;
        vertices[5].inland=true;vertices[5].level=9;
        const std::array triangles{std::array<std::size_t,3>{0,1,2},std::array<std::size_t,3>{3,4,5}};
        WaterSurfaceCache cache;cache.update(world,water,vertices,triangles,p,east(p),-north(p));
        require(cache.points()[0].ocean && !cache.points()[1].ocean,"Ocean and lake use different levels");
        near(cache.points()[0].depth,5,0,"Sea depth");near(cache.points()[1].depth,2,0,"Lake depth");near(cache.points()[2].depth,-1,0,"Dry shore signed depth");
        require(cache.points()[3].depth<0,"River is not also drawn as lake/sea water");
        require(cache.wetTriangles()==std::vector<std::size_t>{0},"Completely dry and river-only triangle skipped");
        require(cache.wetVertices()==std::vector<std::size_t>({0,1,2}),"Dry shore neighbour retained for clipping");
        near(cache.points()[1].base.y,12,1e-8,"Lake vertices sit on the shared water level");
        require(!cache.update(world,water,vertices,triangles,p,east(p),-north(p)) && cache.rebuilds()==1,"Stationary water is sampled once");
    });
    test("water_view","Hydrology steps invalidate depth without freezing waves or changing the world",[] {
        World world;Explorer explorer(world);const auto p=explorer.up();Simulation sim(world,p,79*86400);
        const double height=explorer.altitude();const std::array<SurfaceWaterVertex,3> vertices{{{p,{},world.elevation(p),true,false,2},{p,{},world.elevation(p),true,false,2},{p,{},world.elevation(p),true,false,2}}};
        const std::array<std::array<std::size_t,3>,1> triangles{{{0,1,2}}};WaterSurfaceCache cache;
        const auto update=[&]{return cache.update(world,sim.water,vertices,triangles,p,east(p),-north(p));};
        update();const auto budget=sim.water.budget();const auto clock=sim.motion.seconds();
        sim.motion.advanceClock(.4);require(!update() && sim.motion.seconds()>clock,"Wave clock advances without invalidating hydrostatic samples");
        near(sim.water.budget().storedWaterM3,budget.storedWaterM3,0,"Sampling does not consume water");near(explorer.altitude(),height,0,"Sampling does not change collision height");
        sim.setRainExperiment(120);sim.advance(world,p,120);require(update(),"Rain and erosion step refresh the cache");
        const auto ground=sim.water.sample(p);require(ground.depth>.0005 && !cache.wetTriangles().empty(),"New puddle becomes visible");
        near(cache.points()[0].depth,ground.depth*ground.weight,0,"Cache follows live runoff exactly");
        require(!update() && cache.rebuilds()==2,"Repeated frames do not repeat the hydrology query");
        sim.water.synchronize(world,normalized(p+east(p)*(400/world.config().radius)));require(update(),"Relocating the domain refreshes depth");
        world.modules().set(Module::Hydrology,false);sim.water.synchronize(world,p);require(update(),"Disabled hydrology invalidates held water");
        require(cache.wetTriangles().empty(),"No stale puddle after disabling hydrology");
    });
    test("water_view","Mesh replacement and world reload invalidate equal-size snapshots",[] {
        World world;const auto p=world.findLand();Hydrology water(world,p);WaterSurfaceCache cache;
        std::array<SurfaceWaterVertex,3> vertices{};for(auto& v:vertices) {v.up=p;v.available=true;v.inland=true;v.altitude=10;v.level=11;}
        const std::array<std::array<std::size_t,3>,1> triangles{{{0,1,2}}};
        const auto update=[&]{return cache.update(world,water,vertices,triangles,p,east(p),-north(p));};
        update();for(auto& v:vertices) v.altitude=12;
        cache.invalidate();require(update() && cache.wetTriangles().empty(),"Same vertex count with a new mesh is not a cache hit");
        cache.invalidate();require(update(),"Explicit world load refreshes equal counters");
        world.modules().set(Module::Ocean,false);require(update(),"Ocean state is part of the key");
        Config config;config.seed=119;World other(config);Hydrology otherWater(other,p);
        require(cache.update(other,otherWater,vertices,triangles,p,east(p),-north(p)),"New seed with matching counters cannot reuse old samples");
        const std::array<std::array<std::size_t,3>,1> invalid{{{0,1,9}}};cache.invalidate();bool rejected=false;
        try {cache.update(other,otherWater,vertices,invalid,p,east(p),-north(p));}catch(const std::invalid_argument&) {rejected=true;}
        require(rejected,"Invalid indices rejected before culling");
    });
}
