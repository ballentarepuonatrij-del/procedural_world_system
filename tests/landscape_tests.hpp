// Included inside the core test harness namespace.
void registerLandscapeTests() {
    test("landscape","Prescribed basins have level water, closed banks and distinct scales",[] {
        World w;const auto home=w.findLand();double pond=0,lake=0;
        for(auto kind:{Landform::Wetland,Landform::Pond,Landform::Lake}) {
            const auto place=w.findLandmark(kind,home);const auto p=w.landscape()->patch(place.position);
            require(p && p->kind==kind,"A real procedural site is selected");
            require(p->level>w.config().seaLevel+20,"Inland levels are independent of the ocean");
            for(int i=0;i<16;++i) {
                const double angle=tau*i/16;const auto q=p->directionAt(std::cos(angle)*p->width*.55,std::sin(angle)*p->length*.55);
                const auto water=liquidAt(w,nullptr,nullptr,q);
                if(water.present) {require(!water.ocean && !water.river,"Fresh standing water");near(water.level,p->level,1e-9,"Common reference water level");}
                const auto outer=p->directionAt(std::cos(angle)*p->width*1.24,std::sin(angle)*p->length*1.24);
                const double scale=1.20/p->outline(outer);const auto rim=p->directionAt(std::cos(angle)*p->width*1.24*scale,std::sin(angle)*p->length*1.24*scale);
                require(w.elevation(rim)>p->level,"The basin rim contains its reference water");
            }
            if(kind==Landform::Pond) pond=p->width;
            if(kind==Landform::Lake) lake=p->width;
            if(kind==Landform::Wetland) require(p->width>1000,"An extensive wetland spans kilometres");
            for(int i=0;i<180;++i) {
                const double a=tau*i/180,scale=1.40/p->outline(p->directionAt(std::cos(a)*p->width,std::sin(a)*p->length));
                const auto q=p->directionAt(std::cos(a)*p->width*scale,std::sin(a)*p->length*scale);
                require(p->apply(q,{12345,0,0,Landform::Lowland}).elevation<12344,"Lobed transition is not clipped by a rectangular shortcut");
            }
        }
        require(lake>pond*3,"Ponds and lakes have different extents");
    });
    test("landscape","Freshwater swimming and independent module restoration share the basin",[] {
        World w;const auto place=w.findLandmark(Landform::Lake,w.findLand());const auto patch=w.landscape()->patch(place.position);const auto p=patch->anchor;
        Hydrology ground(w,p);const auto liquid=liquidAt(w,&ground,nullptr,p);
        require(liquid.depth>3 && !liquid.ocean,"A swimmable fresh lake");
        Explorer e(w);e.dive(w,p,2,&ground,nullptr);require(e.swimming() && e.underwater(),"Dive uses the lake surface");
        near(waterPressure(101325,2,false),101325+1000*standardGravity*2,1e-7,"Freshwater hydrostatic density");
        w.modules().set(Module::Ocean,false);w.modules().set(Module::Hydrology,false);w.modules().set(Module::Rivers,false);ground.synchronize(w,p);
        near(liquidAt(w,&ground,nullptr,p).level,patch->level,1e-9,"Lakes do not require Ocean, local runoff or Rivers");
        w.modules().set(Module::InlandWaters,false);require(!liquidAt(w,&ground,nullptr,p).present,"Disabling inland waters removes the reservoir");
        bool rejected=false;try {(void)w.findLandmark(Landform::Lake,p);} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected,"A disabled lake route cannot silently land at an unrelated location");
        w.modules().set(Module::InlandWaters,true);near(liquidAt(w,&ground,nullptr,p).level,patch->level,1e-9,"The seed restores the same reservoir");
    });
    test("landscape","Bay, cliff and connected sand spit have their intended coastal geometry",[] {
        World w;const auto home=w.findLand();
        for(auto kind:{Landform::Bay,Landform::Cliff,Landform::Spit}) {
            const auto place=w.findLandmark(kind,home);const auto p=w.landscape()->patch(place.position);require(p && p->kind==kind,"Coastal route selects the requested kind");
            if(kind==Landform::Bay) {require(w.elevation(p->directionAt(0,p->length*.35))<p->level,"Bay penetrates inland");require(w.elevation(p->directionAt(0,-p->length*.9))<p->level,"Bay opens to the sea");}
            if(kind==Landform::Cliff) require(w.elevation(p->directionAt(0,100))-w.elevation(p->directionAt(0,-180))>30,"A steep coastal escarpment");
            if(kind==Landform::Spit) {
                const std::array<Vec3,5> path{{{-.95,0,.38},{-.62,0,-.06},{-.05,0,-.43},{.64,0,-.54},{.85,0,-.35}}};
                for(std::size_t j=1;j<path.size();++j) for(int i=0;i<=20;++i) {
                    const auto a=path[j-1]*(1-i/20.)+path[j]*(i/20.);const auto r=w.relief(p->directionAt(a.x*p->width,a.z*p->length));
                    require(r.elevation>p->level+.5 && r.sand>.5,"Continuous dry sandy ridge joins the mainland");
                }
            }
            const auto outside=p->directionAt(p->width*1.51,0);
            near(p->apply(outside,{123,0,0,Landform::Lowland}).elevation,123,0,"Feature blends completely back to the background");
        }
    });
    test("landscape","Sea grottos have a reachable mouth and a protected interior",[] {
        World w;const auto place=w.findLandmark(Landform::Grotto,w.findLand());const auto c=w.caveSite(place.position);require(c && c->coastal,"A marine cave has a real cavity");
        Explorer e(w);e.teleport(w,place.position);
        for(const auto q:{Vec3{0,0,-70},Vec3{0,0,-25},Vec3{3,0,22},Vec3{-9,0,65},Vec3{4,0,108}}) walkTo(e,w,c->directionAt(q.x,q.z));
        require(e.underground(),"The explorer enters the coastal cliff");
        const auto column=c->column(e.up());require(column.valid() && e.altitude()<column.ceiling && e.altitude()>column.floor,"Camera stays between floor and roof");
        const double roof=w.elevation(e.up());w.modules().set(Module::Caves,false);
        require(!w.caveSite(e.up()),"Cave switch closes the cavity");near(w.elevation(e.up()),roof,1e-8,"The coastline remains when caves are disabled");
    });
    test("landscape","Wetland plant niches distinguish water depth and salt water",[] {
        Sample s;s.elevation=100;s.temperature=289;s.precipitation=1200;s.wetness=1;s.inland=true;
        for(auto species:{Species::Willow,Species::Alder,Species::Reed,Species::Cattail,Species::Sedge,Species::Sphagnum,Species::WaterLily,Species::Pondweed}) {
            s.waterDepth=species==Species::Sphagnum ? 0 : species==Species::WaterLily || species==Species::Pondweed ? 1 : .15;s.freshwater=s.waterDepth>0;
            require(plantSuitability(species,s,.05,.3,0)>0,"Species accepts its wetland niche");
            s.ocean=true;require(plantSuitability(species,s,.05,.3,0)==0,"Freshwater vegetation excludes marine salinity");s.ocean=false;
            const auto young=plantState(species,71,0,1,true),grown=plantState(species,71,plantTraits(species).maturityDays,1,true);
            require(grown.size>young.size,"New species grow under the existing biological clock");
            SurfaceObject object;object.species=species;object.id=71;object.kind=plantTraits(species).woody ? DetailKind::Tree : DetailKind::Grass;
            object.height=plantTraits(species).height;object.radius=object.height*plantTraits(species).widthRatio;object.plant=grown;object.waterDepth=s.waterDepth;object.flowers.bloom=1;
            const auto mesh=detailMesh(object,{}, {0,1,0},{1,0,0},false);require(!mesh.empty(),"Visible species geometry");
            for(const auto& f:mesh) require(std::isfinite(length(f.triangle.a)) && length(cross(f.triangle.b-f.triangle.a,f.triangle.c-f.triangle.a))>1e-10,"Finite nondegenerate plant faces");
        }
        require(flowerTraits(Species::Sphagnum).form==FlowerForm::None,"Moss does not produce flowers");
        s.freshwater=false;s.sand=1;s.wetness=0;require(plantSuitability(Species::Marram,s,.1,.2,0)>0,"Dune grass occupies dry sand");
        s.sand=0;require(plantSuitability(Species::Marram,s,.1,.2,0)==0,"Dune grass is not scattered throughout inland meadows");
    });
    test("landscape","Wetland vegetation forms stable diverse sites and floating leaves match water",[] {
        World w;const auto place=w.findLandmark(Landform::Wetland,w.findLand());const auto sites=surfaceSites(w,place.position,100);
        std::array<bool,speciesCount> observed{};for(const auto& s:sites) observed[static_cast<unsigned>(s.species)]=true;
        require(std::count(observed.begin()+static_cast<unsigned>(Species::Willow),observed.end(),true)>=6,"Multiple distinct wetland archetypes coexist");
        const auto larger=surfaceSites(w,place.position,120);
        for(const auto& p:sites) {const auto it=std::find_if(larger.begin(),larger.end(),[&](const auto& q){return p.id==q.id;});require(it!=larger.end(),"Observer radius does not respawn vegetation");near(length(p.up-it->up),0,0,"Fixed roots");}
        const auto lily=std::find_if(sites.begin(),sites.end(),[](const auto& s){return s.species==Species::WaterLily;});require(lily!=sites.end(),"Floating-leaved plants occupy shallow pools");
        near(lily->elevation+lily->waterDepth,w.relief(lily->up).waterLevel,1e-9,"Root depth places mature leaves on the reference plane");
    });
    test("landscape","Shared landscape tiles remain deterministic under concurrent generation",[] {
        World w;const auto place=w.findLandmark(Landform::Wetland,w.findLand());std::array<Vec3,80> points{};std::array<double,80> expected{};
        for(std::size_t i=0;i<points.size();++i) {points[i]=normalized(place.position+east(place.position)*(static_cast<double>(i)*510/w.config().radius));expected[i]=w.elevation(points[i]);}
        const auto sample=[points,expected](World copy,int offset) {
            for(int k=0;k<600;++k) {const auto i=static_cast<std::size_t>((k*17+offset)%80);near(copy.elevation(points[i]),expected[i],0,"Concurrent tile reads preserve the seed");}
        };
        auto a=std::async(std::launch::async,sample,w,0),b=std::async(std::launch::async,sample,w,31);a.get();b.get();
        Config other;other.seed=73;World changed(other);const auto old=w.landscape()->patch(place.position),next=changed.landscape()->patch(place.position);
        require(!next || old->id!=next->id,"A different seed cannot reuse the previous tile");
    });
    test("landscape","Local runoff exchanges with lake reservoirs through its boundary budget",[] {
        World w;const auto place=w.findLandmark(Landform::Pond,w.findLand());Simulation simulation(w,place.position,79*86400);
        simulation.setRainExperiment(30);simulation.advance(w,place.position,20);
        const auto budget=simulation.water.budget();near(budget.waterResidual(),0,1e-5,"Rain, soil, runoff and reservoir exchange conserve water");
        unsigned reservoir=0;for(const auto& c:simulation.water.grid().cells) if(c.reservoir) {++reservoir;require(c.reservoirLevel>c.bed(),"Wet reservoir cells use the prescribed plane");}
        require(reservoir>0,"Local shore grid actually intersects a lake");
        const auto mesh=makeTerrainMesh(w,simulation.water,place.position,w.surface(place.position)+1.75,79*86400);
        require(!mesh.waterTriangles.empty(),"Standing water has renderable triangles");
        bool wet=false;for(const auto& v:mesh.waterVertices) if(v.inland && v.level>v.altitude) {wet=true;near(v.level,w.relief(v.up).waterLevel,1e-8,"Render and physics reference the same reservoir");}
        require(wet,"CPU scene includes wet basin vertices");
    });
}
