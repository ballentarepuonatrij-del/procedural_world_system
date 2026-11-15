void registerIceTests() {
    const auto evolve=[](IceColumn& c,double& liquid,double seconds,const IceForcing& forcing) {
        IceExchange sum;
        while(seconds>1e-9) {const double dt=std::min(120.,seconds);const auto e=advanceIce(c,liquid,dt,forcing);sum.frozen+=e.frozen;sum.melted+=e.melted;sum.heat+=e.heat;seconds-=dt;}
        return sum;
    };
    test("ice","Latent heat mass and conductive growth agree",[=] {
        IceForcing f;f.air=263.15;f.emissivity=0;IceColumn c;double liquid=2;
        const auto e=advanceIce(c,liquid,1,f);
        near(c.frozen,100/(1000*latentHeatFusion),1e-15,"One second of 100 W/m2 removes the latent heat of the new ice");
        near(e.heat,-100,1e-8,"Heat flux in joules");near(liquid+c.frozen,2,0,"Mass exactly retained");
        c={};liquid=2;evolve(c,liquid,86400,f);const double first=c.frozen;
        evolve(c,liquid,86400,f);require(c.frozen>first && c.frozen-first<first,"Conducting lid slows subsequent growth");
        // Integrate q=100/(1+10*h/2.2) analytically, h=rho/rhoIce*frozen.
        const double a=10*1000/(2.2*iceDensity),expected=(std::sqrt(1+2*a*100*172800/(1000*latentHeatFusion))-1)/a;
        near(c.frozen,expected,5e-6,"Conductive Stefan growth matches integrated heat flux");
    });
    test("ice","Fresh salt and flowing water freeze at different rates",[=] {
        IceForcing f;f.air=272.15;f.emissivity=0;IceColumn fresh{0,273.15},salt{0,273.15};double a=1,b=1;
        evolve(fresh,a,86400,f);f.salinity=35;evolve(salt,b,86400,f);
        require(fresh.frozen>0 && salt.frozen==0,"Minus one Celsius freezes fresh water but not seawater");
        IceColumn still,flow;double x=1,y=1;f.air=255.15;f.salinity=0;evolve(still,x,86400,f);f.flow=1;evolve(flow,y,86400,f);
        require(flow.frozen<still.frozen*.5,"Turbulent current delays a lid");
    });
    test("ice","Complete freezing cooling and thaw conserve water and enthalpy",[=] {
        IceColumn c{0,280};double liquid=.001;const double initial=iceEnthalpy(c,liquid,0);IceForcing f;f.air=250;
        const auto cold=evolve(c,liquid,7200,f);near(liquid,0,1e-12,"Film fully frozen");require(c.temperature<273.15,"Solid cools below freezing");
        near(iceEnthalpy(c,liquid,0)-initial,cold.heat,1e-7,"Accumulated heat matches enthalpy");
        f.air=290;f.solar=600;const auto warm=evolve(c,liquid,7200,f);
        near(c.frozen,0,1e-12,"Film fully thawed");near(liquid,.001,1e-12,"Liquid mass restored");require(c.temperature>273.15,"Liquid warms after the latent heat is paid");
        near(iceEnthalpy(c,liquid,0)-initial,cold.heat+warm.heat,1e-6,"Freeze thaw cycle closes the energy ledger");
        near(cold.frozen,warm.melted,1e-12,"Phase transfer ledger");
    });
    test("ice","Pause invalid input and floating geometry are bounded",[] {
        IceColumn c{.1,273.15};double liquid=1;advanceIce(c,liquid,0,{});near(c.frozen,.1,0,"Pause");near(liquid,1,0,"Pause liquid");
        for(double time:{-1.,121.,std::numeric_limits<double>::quiet_NaN()}) {bool rejected=false;try {advanceIce(c,liquid,time,{});}catch(const std::invalid_argument&) {rejected=true;}require(rejected,"Bad timestep rejected before mutation");}
        const auto ice=iceProperties(.1,0);near(ice.thickness-ice.freeboard,.1,1e-15,"Floating displacement equals water equivalent");require(ice.supports && ice.cover==1 && ice.transmission>0 && ice.transmission<1,"Solid lid retains some transmission");
        require(!iceProperties(.001,0).supports && iceProperties(.001,0).waveFactor()>0,"Thin film is partial and non supporting");
        near(iceProperties(0,0).waveFactor(),1,0,"Open water unchanged");
    });
    test("ice","Frozen runoff water is retained during flow evaporation and thaw",[=] {
        RunoffGrid grid(8,4);for(auto& c:grid.cells) {c.water=.015;c.soilWater=0;c.conductivity=0;c.rock=-c.soil;}
        const double before=grid.waterVolume();IceForcing cold;cold.air=250;
        for(auto& c:grid.cells) evolve(c.ice,c.water,86400,cold);
        near(grid.waterVolume(),before,1e-10,"Finite puddles retain all their frozen water");
        std::vector<double> rain(64,0);const auto e=grid.advance(10,rain,1e-5,false,false,false,0);
        near(grid.waterVolume()+e.evaporatedM3+e.drainedM3,before,1e-9,"Runoff ledger includes ice");near(e.evaporatedM3,0,0,"Closed frozen surface suppresses evaporation");
        IceForcing warm;warm.air=290;warm.solar=500;
        for(auto& c:grid.cells) evolve(c.ice,c.water,86400,warm);
        near(grid.frozenVolume(),0,1e-10,"Finite puddles thaw completely");near(grid.waterVolume(),before,1e-9,"No phase mass lost through a closed grid");
    });
    test("ice","Local phase budgets cache relocation and module thaw retain water",[] {
        World w;const auto site=w.landscape()->patch(w.findLandmark(Landform::Pond,w.findLand()).position)->anchor;Hydrology water(w,site);Weather weather(w,site,79*86400);
        water.setIceExperiment(IceExperiment::Freeze);water.previewIce(w,weather,72*3600);
        require(water.iceBudget().storedM3>0,"Pond develops ice");near(water.iceBudget().residual(),0,1e-6,"Phase ledger closes");
        const auto wet=liquidAt(w,&water,nullptr,site);require(wet.ice.supports,"Frozen pond supports observer");
        const double before=water.budget().storedWaterM3;water.synchronize(w,normalized(site+east(site)*80/w.config().radius));
        near(water.iceBudget().residual(),0,1e-6,"Moving chart accounts for entering and leaving ice");water.synchronize(w,site);
        near(liquidAt(w,&water,nullptr,site).ice.thickness,wet.ice.thickness,1e-12,"Revisited cell retains ice history");
        w.modules().set(Module::Ice,false);water.synchronize(w,site);near(water.iceBudget().storedM3,0,0,"Disabling ice thaws local stock");near(water.iceBudget().residual(),0,1e-6,"Explicit thaw counted");
        near(water.budget().waterResidual(),0,1e-6,"Finite water budget preserved");near(water.budget().storedWaterM3,before,1e-6,"No water introduced by module toggle");
    });
    test("ice","A fully frozen puddle remains in the visible water cache",[] {
        World w;const auto p=w.findLand();Simulation sim(w,p,79*86400);sim.setRainExperiment(120);sim.advance(w,p,120);
        sim.water.setIceExperiment(IceExperiment::Freeze);sim.water.previewIce(w,sim.weather,5*3600);
        const auto ground=sim.water.sample(p);require(ground.depth<.0005 && ground.frozenDepth>.0005,"Rain puddle becomes solid");
        std::array<SurfaceWaterVertex,3> vertices{};for(auto& v:vertices) {v.available=true;v.up=p;v.altitude=sim.water.ground(w,p);v.spacing=2;}
        const std::array<std::array<std::size_t,3>,1> triangles{{{0,1,2}}};WaterSurfaceCache cache;cache.update(w,sim.water,vertices,triangles,p,east(p),-north(p));
        require(!cache.wetTriangles().empty() && !cache.points()[0].ocean,"Solid puddle is neither dry nor mistaken for ocean");
        near(cache.points()[0].depth,(ground.depth+ground.frozenDepth)*ground.weight,1e-12,"Visible depth retains frozen mass");
    });
    test("ice","Walking swimming ceiling thaw and saved continuation agree",[] {
        StateFixture f;f.world.modules().set(Module::SurfaceDetail,false);const auto p=f.world.landscape()->patch(f.world.findLandmark(Landform::Lake,f.world.findLand()).position)->anchor;
        f.simulation.advance(f.world,p,0);f.simulation.water.setIceExperiment(IceExperiment::Freeze);f.simulation.water.previewIce(f.world,f.simulation.weather,72*3600);
        f.explorer.teleport(f.world,p,&f.simulation.water);const auto water=liquidAt(f.world,&f.simulation.water,nullptr,p);
        require(water.ice.supports && !f.explorer.swimming(),"Teleport stands on thick ice");near(f.explorer.altitude(),water.surfaceLevel+Explorer::eyeHeight,1e-8,"Shared rendered and collision top");
        f.explorer.update(f.world,{1,0,0,false,false},1,&f.simulation.water,&f.simulation.motion);near(length(f.explorer.up()-p)*f.world.config().radius,2.5,.01,"Ice walking retains normal speed");
        f.explorer.dive(f.world,p,2,&f.simulation.water,&f.simulation.motion);
        for(int i=0;i<10;++i) f.explorer.update(f.world,{0,0,1,false,false},.5,&f.simulation.water,&f.simulation.motion);
        require(f.explorer.underwater() && f.explorer.altitude()<=water.level-.19,"Underwater observer cannot rise through solid lid");
        const auto bytes=encodeState(f.world,f.explorer,f.simulation,f.session,11);auto copy=decodeState(bytes);
        require(encodeState(copy->world,copy->explorer,copy->simulation,copy->session,11)==bytes,"Ice temperature phase budget experiment and motion roundtrip");
        for(auto* sim:{&f.simulation,&copy->simulation}) {sim->water.setIceExperiment(IceExperiment::Thaw);sim->water.previewIce(f.world,sim->weather,48*3600);}
        require(encodeState(f.world,f.explorer,f.simulation,f.session,11)==encodeState(copy->world,copy->explorer,copy->simulation,copy->session,11),"Saved ice resumes identical melting");
        require(!liquidAt(f.world,&f.simulation.water,nullptr,p).ice.supports,"Thaw removes support");
        f.explorer.update(f.world,{0,0,1,false,false},1,&f.simulation.water,&f.simulation.motion);
        require(f.explorer.altitude()>water.level-.18,"Thaw releases the underwater ceiling");
    });
}
