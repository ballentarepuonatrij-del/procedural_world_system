// Included in the core test harness namespace.
void registerPhenomenaTests() {
    test("phenomena","Magnus dew point and refractive angles match reference values",[] {
        near(dewPoint(293.15,.5),282.41,.06,"20 C and 50 percent RH");
        near(dewPoint(263.15,1),263.15,1e-10,"Saturation dew point");
        near(degrees(primaryRainbowRadius(1.333)),42.08,.1,"Primary liquid-water rainbow");
        near(degrees(haloRadius(1.31)),21.84,.1,"Hexagonal ice minimum deviation");
        require(primaryRainbowRadius(1.331)>primaryRainbowRadius(1.343),"Rainbow red is outside violet");
        require(haloRadius(1.306)<haloRadius(1.317),"Halo red is inside violet");
    });
    test("phenomena","Rainbow needs illuminated liquid rain and a low Sun",[] {
        Config c;const auto p=direction(0,0);PhenomenaForcing f;f.rainMmH=6;
        const auto time=solarHourTime(c,p,79*86400,8);
        require(atmosphericPhenomena(c,p,time,f).rainbow>.8,"Low Sun and rain");
        near(atmosphericPhenomena(c,p,solarHourTime(c,p,time,12),f).rainbow,0,0,"Midday bow below horizon");
        near(atmosphericPhenomena(c,p,solarHourTime(c,p,time,0),f).rainbow,0,0,"No sunlight at night");
        auto dry=f;dry.rainMmH=0;near(atmosphericPhenomena(c,p,time,dry).rainbow,0,0,"No rain");
        auto frozen=f;frozen.temperature=260;near(atmosphericPhenomena(c,p,time,frozen).rainbow,0,0,"Snow cannot make a liquid rainbow");
        f.sunCloudWater=2;require(atmosphericPhenomena(c,p,time,f).rainbow<1e-8,"Opaque cloud blocks direct illumination");
    });
    test("phenomena","Halo depends on upper ice and direct sunlight",[] {
        Config c;const auto p=direction(0,0);const auto t=solarHourTime(c,p,79*86400,10);PhenomenaForcing f;
        f.temperature=300;f.upperIce=1;require(atmosphericPhenomena(c,p,t,f).halo>.8,"Warm ground permits cold upper crystals");
        f.upperIce=0;near(atmosphericPhenomena(c,p,t,f).halo,0,0,"No crystals");
        f.upperIce=1;f.cloudWater=3;require(atmosphericPhenomena(c,p,t,f).halo<1e-8,"Opaque cloud masks halo");
    });
    test("phenomena","Low winter Sun and calm crystals produce a pillar",[] {
        Config c;const auto p=direction(0,0);const auto t=solarHourTime(c,p,79*86400,6.2);PhenomenaForcing f;
        f.temperature=253;f.lowIce=1;f.windSpeed=1;
        require(atmosphericPhenomena(c,p,t,f).pillar>.8,"Cold calm low ice");
        f.windSpeed=12;near(atmosphericPhenomena(c,p,t,f).pillar,0,0,"Turbulence destroys aligned plates");
        f.windSpeed=1;near(atmosphericPhenomena(c,p,solarHourTime(c,p,t,12),f).pillar,0,0,"High Sun");
        f.lowIce=0;near(atmosphericPhenomena(c,p,t,f).pillar,0,0,"No low ice");
    });
    test("phenomena","Auroral oval is magnetic and visible in both dark hemispheres",[] {
        Config c;const auto pole=magneticPole(c.seed);PhenomenaForcing f;f.geomagneticActivity=.8;
        for(double sign:{-1.,1.}) {
            const auto p=pole*(sign*std::sin(radians(64)))+east(pole)*std::cos(radians(64));
            const auto night=solarHourTime(c,p,79*86400,0),day=solarHourTime(c,p,night,12);
            near(degrees(magneticLatitude(p,c.seed)),sign*64,1e-9,"Seeded magnetic frame");
            require(atmosphericPhenomena(c,p,night,f).aurora>.7,"Night oval");
            near(atmosphericPhenomena(c,p,day,f).aurora,0,0,"Daylight hides aurora");
            auto cloudy=f;cloudy.cloudWater=2;require(atmosphericPhenomena(c,p,night,cloudy).aurora<1e-8,"Cloud extinction");
        }
        const auto low=east(pole);require(atmosphericPhenomena(c,low,solarHourTime(c,low,79*86400,0),f).aurora<1e-10,"No ordinary equatorial aurora");
    });
    test("phenomena","Fog requires saturation and weak mixing and thins with height",[] {
        Config c;const auto p=direction(0,0);const auto t=solarHourTime(c,p,79*86400,0);PhenomenaForcing f;f.humidity=1;f.wetness=1;
        const auto base=atmosphericPhenomena(c,p,t,f);require(base.fog>.9 && base.visibility<100,"Dense near-ground fog");
        f.humidity=.8;near(atmosphericPhenomena(c,p,t,f).fog,0,0,"Unsaturated air");
        f.humidity=1;f.windSpeed=15;near(atmosphericPhenomena(c,p,t,f).fog,0,0,"Strong mixing");
        f.windSpeed=1;f.aboveGround=1000;require(atmosphericPhenomena(c,p,t,f).fog<.00002,"Above shallow fog");
    });
    test("phenomena","Dust is suppressed by weak wind moisture rain snow and absent source",[] {
        Config c;const auto p=direction(0,0);PhenomenaForcing f;f.dustSource=1;f.humidity=.1;f.windSpeed=25;
        require(atmosphericPhenomena(c,p,0,f).dust>.9,"Dry bare windy ground");
        for(int i=0;i<6;++i) {auto blocked=f;
            if(i==0) blocked.windSpeed=7;
            if(i==1) blocked.humidity=.9;
            if(i==2) blocked.rainMmH=3;
            if(i==3) blocked.snowDepth=.1;
            if(i==4) blocked.dustSource=0;
            if(i==5) blocked.wind=false;
            near(atmosphericPhenomena(c,p,0,blocked).dust,0,0,"Dust suppression "+std::to_string(i));
        }
    });
    test("phenomena","Optical meshes preserve angular radii and unit rays",[] {
        Config c;const auto p=direction(0,0);const auto t=solarHourTime(c,p,79*86400,8);const auto sun=orbit(c,t).sunDirection;
        for(bool bow:{false,true}) {
            PhenomenaState s;s.enabled=true;if(bow) s.rainbow=1;else s.halo=1;
            const auto mesh=atmosphericSky(c,p,2,t,0,s);require(!mesh.empty() && mesh.size()%3==0 && mesh.size()<=8064,"Bounded complete rings");
            for(const auto& v:mesh) {
                near(length(v.direction),1,1e-12,"Unit ray");const double angle=degrees(std::acos(clamp(dot(v.direction,bow ? -sun : sun),-1.,1.)));
                require(angle>(bow ? 40 : 21) && angle<(bow ? 43 : 24),"Physical angular extent");
                require(std::isfinite(v.alpha) && v.alpha>=0 && v.alpha<=1,"Finite opacity");
                if(dot(v.direction,p)<-.001) near(v.alpha,0,0,"Below geometric horizon clipped");
            }
        }
    });
    test("phenomena","Auroral curtain animates deterministically and pauses exactly",[] {
        Config c;const auto pole=magneticPole(c.seed),p=pole*std::sin(radians(64))+east(pole)*std::cos(radians(64));
        PhenomenaState s;s.enabled=true;s.aurora=.8;s.activity=.8;s.magneticLatitude=radians(64);
        const auto a=atmosphericSky(c,p,2,0,20,s),b=atmosphericSky(c,p,2,0,20,s),moving=atmosphericSky(c,p,2,0,30,s);
        require(!a.empty() && a.size()<=4800 && a.size()==b.size(),"Bounded shell curtain");
        for(std::size_t i=0;i<a.size();++i) {near(length(a[i].direction-b[i].direction),0,0,"Pause ray");near(a[i].alpha,b[i].alpha,0,"Pause opacity");}
        require(a.size()!=moving.size() || length(a.front().direction-moving.front().direction)>.00001,"Active curtain changes");
    });
    test("phenomena","Distant flash illuminates a finite sky patch and depends on clouds module",[] {
        Config c;const auto p=direction(0,0);PhenomenaForcing f;f.flashes.push_back({normalized(north(p)+p*.08),.8});
        const auto night=atmosphericPhenomena(c,p,solarHourTime(c,p,79*86400,0),f);
        const auto day=atmosphericPhenomena(c,p,solarHourTime(c,p,79*86400,12),f);
        require(night.flashes.size()==1 && day.flashes[0].intensity<night.flashes[0].intensity,"Daylight attenuation");
        require(atmosphericSky(c,p,2,0,0,night).size()==192,"One smooth 64-triangle glow");
        f.clouds=false;require(atmosphericPhenomena(c,p,0,f).flashes.empty(),"No cloud phenomenon with Clouds off");
    });
    test("phenomena","Dust sprites are bounded seeded advected and stationary when paused",[] {
        Config c;const auto p=direction(0,0);const Wind2 wind{20,5};
        const auto a=dustParticles(c,p,200,12,wind,1),b=dustParticles(c,p,200,12,wind,1),moving=dustParticles(c,p,200,12.01,wind,1);
        require(!a.empty() && a.size()<=180 && a.size()==b.size(),"Fixed local particle budget");
        for(std::size_t i=0;i<a.size();++i) {near(length(a[i].position-b[i].position),0,0,"Paused sample identical");require(a[i].radius<=7 && a[i].opacity<=.16 && a[i].opacity>=0,"Finite bounded sprites");}
        require(a.size()!=moving.size() || length(a.front().position-moving.front().position)>.1,"Wind advects dust");
        require(dustParticles(c,p,0,0,wind,0).empty(),"No residual dust after disabling");
        for(const auto polar:{direction(pi/2,0),direction(-pi/2,0),direction(0,pi)}) for(const auto& v:dustParticles(c,polar,0,100,wind,1)) require(std::isfinite(length(v.position)),"Poles and longitude seam finite");
    });
    test("phenomena","Invalid physical input is rejected",[] {
        const auto reject=[](auto f){bool threw=false;try {f();} catch(const std::invalid_argument&) {threw=true;}require(threw,"Invalid forcing rejected");};
        reject([] {dewPoint(293,-1);});reject([] {primaryRainbowRadius(2);});reject([] {haloRadius(0);});
        reject([] {PhenomenaForcing f;f.windSpeed=std::numeric_limits<double>::quiet_NaN();(void)atmosphericPhenomena({},direction(0,0),0,f);});
        reject([] {PhenomenaForcing f;f.flashes.resize(33);(void)atmosphericPhenomena({},direction(0,0),0,f);});
        reject([] {(void)dustParticles({},direction(0,0),0,0,{},-1);});
    });
    test("phenomena","Natural sampling respects modules and does not alter water budgets",[] {
        World w;const auto p=w.findLand();Simulation sim(w,p,79*86400);const auto before=sim.water.budget();
        const auto f=phenomenaForcing(w,sim,p,w.surface(p)+1.75,79*86400);
        require(f.enabled && f.flashes.empty(),"No invented distant lightning in an unadvanced world");
        const auto state=atmosphericPhenomena(w.config(),p,79*86400,f);require(state.visibility>=1 && state.visibility<=100000,"Finite natural visibility");
        const double noon=solarHourTime(w.config(),p,79*86400,12);
        const auto surface=phenomenaForcing(w,sim,p,w.surface(p)+1.75,noon);
        require(surface.humidity<.95 && atmosphericPhenomena(w.config(),p,noon,surface).fog==0,"Saturated cloud layer does not imply ground fog at noon");
        near(sim.water.budget().waterResidual(),before.waterResidual(),0,"Optics do not insert water");
        w.modules().set(Module::Clouds,false);const auto clear=phenomenaForcing(w,sim,p,w.surface(p)+1.75,79*86400,Phenomenon::Rainbow);
        near(atmosphericPhenomena(w.config(),p,79*86400,clear).rainbow,0,0,"Cloud dependency");
        w.modules().set(Module::Wind,false);const auto calm=phenomenaForcing(w,sim,p,w.surface(p)+1.75,79*86400,Phenomenon::Dust);
        near(atmosphericPhenomena(w.config(),p,79*86400,calm).dust,0,0,"Wind dependency");
        w.modules().set(Module::Atmosphere,false);require(!w.modules().enabled(Module::Phenomena),"Atmosphere prerequisite");
        require(!atmosphericPhenomena(w.config(),p,0,phenomenaForcing(w,sim,p,2,0)).enabled,"Entire module disabled");
    });
    test("phenomena","Rare-event routes find visible conditions across seeds and seasons",[] {
        for(std::uint64_t seed:{42ULL,119ULL}) {
            Config c;c.seed=seed;World w(c);Simulation sim(w,w.findLand(),170*86400);
            for(unsigned i=1;i<8;++i) {
                const auto kind=static_cast<Phenomenon>(i);const auto site=findPhenomenon(w,kind,170*86400);
                sim.setClock(site.seconds);sim.advance(w,site.position,0);
                const auto f=phenomenaForcing(w,sim,site.position,w.surface(site.position)+1.75,site.seconds,kind);
                const auto state=atmosphericPhenomena(c,site.position,site.seconds,f);
                const double value=kind==Phenomenon::Rainbow ? state.rainbow : kind==Phenomenon::Halo ? state.halo : kind==Phenomenon::Pillar ? state.pillar : kind==Phenomenon::Aurora ? state.aurora : kind==Phenomenon::Fog ? state.fog : kind==Phenomenon::Dust ? state.dust : state.flashes.empty() ? 0 : state.flashes[0].intensity;
                require(value>.1,"Visible preview "+std::string(phenomenonName(kind))+" seed "+std::to_string(seed));
                if(i<=5) {
                    const auto mesh=atmosphericSky(c,site.position,w.surface(site.position)+1.75,site.seconds,site.seconds,state);
                    const auto right=normalized(cross(site.look,site.position)),up=normalized(cross(right,site.look));
                    const bool visible=std::any_of(mesh.begin(),mesh.end(),[&](const auto& v) {const double depth=dot(v.direction,site.look);return v.alpha>.002 && depth>0 && std::abs(dot(v.direction,right))<depth*std::tan(radians(36))*1.6 && std::abs(dot(v.direction,up))<depth*std::tan(radians(36));});
                    require(visible,"Effect is in the initial view: "+std::string(phenomenonName(kind)));
                }
            }
        }
    });
    test("phenomena","New preference defaults and explicit switch survive serialization",[] {
        auto old=parsePreferences("version=1\nsnow=0\n");require(old.modules.requested(Module::Phenomena),"Old settings enable new module");require(!old.modules.requested(Module::Snow),"Existing choices remain intact");
        old.modules.set(Module::Phenomena,false);
        const auto roundtrip=parsePreferences(serializePreferences(old));require(!roundtrip.modules.requested(Module::Phenomena),"Explicit saved opt-out");
    });
}
