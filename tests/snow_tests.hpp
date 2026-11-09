// Included in the core test harness namespace.
struct SnowFixture {
    World world;Vec3 site;double clock=79*86400;
    SnowFixture() {
        static const World base;static const Vec3 location=findSnowfield(base,base.findLand(),79*86400);
        world=base;site=location;
    }
};
void registerSnowTests() {
    test("snow","Precipitation phase and reference cover respect temperature and water",[] {
        near(snowFraction(260,.8),1,0,"Cold precipitation is solid");
        near(snowFraction(282,1),0,0,"Warm precipitation is liquid");
        require(snowFraction(274,.4)>snowFraction(274,1),"Evaporative cooling favours snow in drier air");
        for(int i=0;i<40;++i) require(snowFraction(270+i*.2,.8)>=snowFraction(270+(i+1)*.2,.8),"Continuous monotone transition");
        Sample s;s.temperature=265;s.precipitation=900;
        require(referenceSnowDepth(s)>.2,"Cold wet ground supports a reference pack");
        s.ocean=true;near(referenceSnowDepth(s),0,0,"No unmodelled sea ice");
        s.ocean=false;s.freshwater=true;near(referenceSnowDepth(s),0,0,"Open lakes are not frozen by the snow module");
        s.freshwater=false;s.temperature=290;near(referenceSnowDepth(s),0,0,"Warm reference terrain is bare");
    });
    test("snow","Snowfall accumulates water equivalent with a closed mass ledger",[] {
        SnowGrid grid(8,4);SnowForcing f;f.temperature=263.15;f.humidity=1;f.precipitation=1/3.6e6;
        SnowExchange budget;
        for(int i=0;i<30;++i) {const auto b=grid.advance(120,f);budget.snowfallM3+=b.snowfallM3;budget.meltM3+=b.meltM3;budget.exportM3+=b.exportM3;}
        near(grid.waterVolume(),.001*64*16,1e-10,"One millimetre of water equivalent after an hour");
        near(grid.waterVolume(),budget.snowfallM3-budget.meltM3-budget.exportM3,1e-10,"Mass is not invented by depth conversion");
        for(const auto& c:grid.cells) require(c.depth>.008 && c.depth<.012 && c.density()>65 && c.density()<200,"Fresh low-density snow has greater geometrical depth");
    });
    test("snow","Thaw and sublimation remove measured water without negative reservoirs",[] {
        SnowGrid grid(4,8);for(auto& c:grid.cells) {c.swe=.1;c.depth=.4;}
        const double before=grid.waterVolume();SnowForcing f;f.temperature=278.15;f.humidity=1;
        const auto thaw=grid.advance(120,f);
        near(thaw.meltM3,4e-3/86400*5*120*16*64,1e-10,"Temperature-index melt in water-equivalent units");
        near(grid.waterVolume()+thaw.meltM3,before,1e-10,"Melt leaves the snow reservoir once");
        f.temperature=260;f.humidity=.1;f.wind={4,0};const double old=grid.waterVolume();const auto dry=grid.advance(120,f);
        require(dry.sublimatedM3>0 && dry.meltM3==0,"Cold dry air sublimates without liquid melt");
        near(grid.waterVolume()+dry.sublimatedM3+dry.exportM3,old,1e-10,"Sublimation ledger");
    });
    test("snow","Wind redistributes snow into lee deposits with explicit boundary export",[] {
        SnowGrid grid(32,4);for(int z=0;z<32;++z) for(int x=0;x<32;++x) {
            auto& c=grid.cells[static_cast<std::size_t>(z*32+x)];
            c.bed=10*std::exp(-std::pow((x-13)/4.,2));c.depth=.4;c.swe=.08;
        }
        const double initial=grid.waterVolume();SnowForcing f;f.temperature=260;f.humidity=1;f.wind={24,0};
        double exported=0,moved=0;
        for(int i=0;i<300;++i) {const auto b=grid.advance(2,f);exported+=b.exportM3;moved+=b.redistributedM3;}
        near(grid.waterVolume()+exported,initial,1e-8,"Blowing and deposition conserve water including outflow");
        require(moved>1 && exported>0,"Snow travels rather than receiving a decorative depth offset");
        double windward=0,lee=0;
        // Compare mirrored slopes of the ridge (crest x=13), rather than its
        // distant downwind plain where the flow has already recovered.
        for(int z=0;z<32;++z) for(int x=9;x<13;++x) {windward+=grid.cells[static_cast<std::size_t>(z*32+x)].swe;lee+=grid.cells[static_cast<std::size_t>(z*32+26-x)].swe;}
        require(lee>windward*1.1,"Sheltered lee gains snow relative to the exposed side");
        require(lee>.08*128 && windward<.08*128,"The lee gains absolute mass while the exposed slope loses it");
        for(const auto& c:grid.cells) require(c.swe>=0 && c.depth>=0 && c.airborne>=0 && std::isfinite(c.depth),"Stable positive reservoirs at high wind");
    });
    test("snow","Ground blizzards lift existing snow without fresh precipitation",[] {
        SnowFixture f;Simulation sim(f.world,f.site,f.clock);sim.water.snow.setExperiment(SnowExperiment::Drifting);
        sim.advance(f.world,f.site,60);const auto a=sim.water.snow.atmosphere(f.world,sim.weather,f.site,f.world.surface(f.site));
        require(a.fallingMmH==0 && a.blowing>0 && a.visibility<2000,"Wind alone produces drifting snow and reduced visibility");
        const auto b=sim.water.snow.budget();require(b.snowfallM3==0 && b.redistributedM3>0,"Existing pack supplies the drift");
        near(b.residual(),0,1e-8,"Local snow mass ledger");
    });
    test("snow","Shared clock preserves snow and melt across frame partitions and module switches",[] {
        SnowFixture f;Simulation a(f.world,f.site,f.clock),b(f.world,f.site,f.clock);
        a.water.snow.setExperiment(SnowExperiment::Thaw);b.water.snow.setExperiment(SnowExperiment::Thaw);
        a.advance(f.world,f.site,20);for(int i=0;i<80;++i) b.advance(f.world,f.site,.25);
        near(a.water.snow.budget().storedM3,b.water.snow.budget().storedM3,0,"Frame-independent snow physics");
        near(a.water.budget().meltM3,b.water.budget().meltM3,0,"Frame-independent melt supplied to runoff");
        require(a.water.budget().meltM3>0,"Melt reaches the active runoff domain");
        near(a.water.budget().waterResidual(),0,1e-8,"Runoff counts melt as an explicit source");
        const auto before=a.water.snow.budget().storedM3;const auto steps=a.water.snow.steps();a.advance(f.world,f.site,0);
        near(a.water.snow.budget().storedM3,before,0,"Pause retains the pack");
        f.world.modules().set(Module::Snow,false);a.advance(f.world,f.site,10);require(a.water.snow.steps()==steps,"Disabled snow freezes");
        f.world.modules().set(Module::Snow,true);a.advance(f.world,f.site,0);near(a.water.snow.budget().storedM3,before,0,"Re-enabling preserves snow");
        f.world.modules().set(Module::Hydrology,false);a.advance(f.world,f.site,10);
        require(a.water.snow.steps()>steps && !a.water.enabled(),"Snow does not require the runoff module");
    });
    test("snow","Moving snow domain preserves overlap and records entering reservoirs",[] {
        SnowFixture f;Snow snow(f.world,f.site,f.clock);Weather weather(f.world,f.site,f.clock);
        snow.setExperiment(SnowExperiment::Fine);snow.step(f.world,weather,120);const auto initial=snow.sample(f.site);
        for(double distance:{80.,600.,0.}) {
            const auto p=normalized(f.site+east(f.site)*(distance/f.world.config().radius));snow.synchronize(f.world,p);
            near(snow.budget().residual(),0,1e-8,"Moving-domain exchange is accounted");
        }
        near(snow.sample(f.site).swe,initial.swe,1e-12,"Cached physical snow returns unchanged");
        snow.setObserved(false);const auto steps=snow.steps();snow.step(f.world,weather,120);require(snow.steps()==steps,"High flight / hidden domain suspends work");
    });
    test("snow","Terrain snapshots contain snow geometry without freezing open water",[] {
        SnowFixture f;Hydrology water(f.world,f.site,f.clock);
        const auto covered=makeTerrainMesh(f.world,water,f.site,f.world.surface(f.site)+1.75,f.clock,16);
        f.world.modules().set(Module::Snow,false);water.synchronize(f.world,f.site);
        const auto bare=makeTerrainMesh(f.world,water,f.site,f.world.surface(f.site)+1.75,f.clock,16);
        require(covered.terrain.size()==bare.terrain.size(),"Snow preserves the terrain topology");
        double lift=0,white=0;
        for(std::size_t i=0;i<covered.terrain.size();++i) {
            const auto& a=covered.terrain[i];const auto& b=bare.terrain[i];lift=std::max(lift,length(a.position-b.position));white+=a.color.r-b.color.r;
            require(std::isfinite(length(a.normal)),"Snowdrift normals remain finite");
        }
        require(lift>.1 && white>0,"Visible depth and snow albedo");
        require(covered.waterVertices.size()==bare.waterVertices.size(),"Water mesh remains present");
        for(std::size_t i=0;i<bare.waterVertices.size();++i) near(covered.waterVertices[i].altitude,bare.waterVertices[i].altitude,0,"Snow does not lift a lake bed");
    });
    test("snow","Deep snow supports and slows a walking observer",[] {
        SnowFixture f;f.world.modules().set(Module::SurfaceDetail,false);Hydrology water(f.world,f.site,f.clock);
        Explorer snowy(f.world);snowy.teleport(f.world,f.site,&water);
        near(snowy.altitude(),water.ground(f.world,f.site)+water.snow.walkingDepth(f.site)+Explorer::eyeHeight,1e-8,"Observer sinks a bounded amount into soft snow");
        snowy.update(f.world,{1,0,0,false,false},4,&water);
        f.world.modules().set(Module::Snow,false);water.synchronize(f.world,f.site);
        Explorer bare(f.world);bare.teleport(f.world,f.site,&water);bare.update(f.world,{1,0,0,false,false},4,&water);
        require(length(snowy.up()-f.site)<length(bare.up()-f.site)*.9,"Snow slows travel compared with bare ground");
    });
    test("snow","Large flakes fine crystals and suspended snow have bounded deterministic visuals",[] {
        SnowFixture f;Snow snow(f.world,f.site,f.clock);Weather weather(f.world,f.site,f.clock);const double height=f.world.surface(f.site)+1.75;
        snow.setExperiment(SnowExperiment::Fine);const auto fine=snow.particles(f.world,weather,f.site,height);
        snow.setExperiment(SnowExperiment::Large);const auto large=snow.particles(f.world,weather,f.site,height),paused=snow.particles(f.world,weather,f.site,height);
        require(!fine.empty() && large.size()>fine.size() && large.size()<=2600,"Bounded precipitation visual budget");
        unsigned big=0;for(std::size_t i=0;i<large.size();++i) {
            big+=large[i].coarse ? 1U : 0U;near(length(large[i].up-paused[i].up),0,0,"Pause retains particle positions");
            require(std::isfinite(large[i].altitude) && large[i].radius>0 && large[i].opacity>=0,"Finite flakes");
        }
        require(big>static_cast<double>(large.size())*.8,"Near-freezing precipitation favours large aggregates");
        snow.animate(f.world,weather,f.site,1);const auto moving=snow.particles(f.world,weather,f.site,height);
        require(!moving.empty() && length(moving.front().up-large.front().up)>1e-9,"Wind transports the falling flakes");
        require(snow.particles(f.world,weather,f.site,height+1000).empty(),"No near-ground particles above their layer");
    });
    test("snow","Invalid forcing and grid states fail before mass changes",[] {
        SnowGrid grid(4,8);SnowForcing f;
        for(double seconds:{-1.,121.,std::numeric_limits<double>::quiet_NaN()}) {
            bool rejected=false;try {(void)grid.advance(seconds,f);} catch(const std::invalid_argument&) {rejected=true;}
            require(rejected,"Invalid intervals are rejected");
        }
        grid.cells[0].swe=-.1;bool rejected=false;try {(void)grid.advance(1,f);} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected,"Negative snow is rejected");
        World w;w.modules().set(Module::Snow,false);rejected=false;
        try {(void)findSnowfield(w,w.findLand(),0);} catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"Disabled snow route gives a useful error");
    });
}
