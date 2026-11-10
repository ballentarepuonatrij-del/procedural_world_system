void registerBiomeTests() {
    test("biomes","Forest habitats survive seasonal weather changes",[] {
        World w;unsigned seasonal=0;std::array<unsigned,14> counts{};
        for(int i=0;i<1200;++i) {
            const auto p=point(i,1200);const auto annual=w.climatology(p);++counts[static_cast<unsigned>(annual.biome)];
            const auto winter=w.sample(p,0),summer=w.sample(p,w.config().year*.5);
            require(winter.biome==annual.biome && summer.biome==annual.biome,"Climate biome must not switch every season");
            if(std::abs(winter.temperature-summer.temperature)>10) ++seasonal;
        }
        require(seasonal>100,"Actual weather still has seasons");
        for(auto b:{Biome::Rainforest,Biome::TemperateRainforest,Biome::BorealForest,Biome::AlpineMeadow})
            require(counts[static_cast<unsigned>(b)]>0,"Each added habitat occurs on the planet");
    });
    test("biomes","Massifs reach high elevations with continuous neighbouring slopes",[] {
        World w;double highest=0;Vec3 peak{};unsigned valleys=0;
        for(int i=0;i<6000;++i) {
            const auto p=point(i,6000);const auto r=w.relief(p);
            require(std::isfinite(r.elevation) && r.elevation<9000,"Terrestrial elevation bound");
            if(r.elevation>highest) {highest=r.elevation;peak=p;}
            if(r.type==Landform::MountainValley) ++valleys;
            if(r.elevation>4000) near(w.elevation(normalized(p+east(p)*(.1/w.config().radius))),r.elevation,2,"No mountain cracks at walking scale");
        }
        require(highest>7500 && valleys>10,"Extensive ranges include high peaks and broad valleys");
        w.modules().set(Module::Landforms,false);
        require(w.elevation(peak)<highest-800,"Disabling landforms removes massif shaping");
    });
    test("biomes","Mountain routes expose high summits from dry walkable slopes",[] {
        World w;
        for(auto type:{Landform::Mountains,Landform::MountainValley}) {
            const auto route=w.findLandmark(type,w.findLand());const double h=w.elevation(route.position);
            require(route.type==type && w.surface(route.position)-h<.05,"Route lands on dry mountain terrain");
            require(w.elevation(route.lookToward)>h+800,"Substantial vertical relief is visible from the destination");
            for(auto axis:{east(route.position),-east(route.position),north(route.position),-north(route.position)})
                require(std::abs(w.elevation(normalized(route.position+axis*(30/w.config().radius)))-h)/30<.41,"Viewpoint avoids abrupt cliffs");
        }
    });
    test("biomes","Dense forests have distinct canopies and tropical liana hosts",[] {
        World w;
        for(auto biome:{Biome::Rainforest,Biome::TemperateRainforest,Biome::BorealForest,Biome::Forest}) {
            const auto route=findForest(w,biome);require(w.climatology(route.position).biome==biome,"Tour enters requested habitat");
            unsigned trees=0,understory=0,vines=0;double tallest=0;
            for(const auto& o:surfaceObjects(w,route.position,100)) {
                if(o.kind==DetailKind::Tree) ++trees;
                if(o.species>=Species::Spruce && o.kind!=DetailKind::Tree) ++understory;
                if(o.lianas()) ++vines;
                tallest=std::max(tallest,o.height);
            }
            require(trees>40 && understory>0,"Canopy and new understory coexist");
            require(tallest>(biome==Biome::Rainforest ? 45 : biome==Biome::TemperateRainforest ? 50 : 25),"Large trees are real geometry");
            require(biome==Biome::Rainforest ? vines>0 : vines==0,"Hanging lianas belong to tropical trees");
        }
    });
    test("biomes","New flora respects temperature, freshwater and treeline",[] {
        Sample s;s.elevation=300;s.temperature=300;s.precipitation=2200;s.biome=Biome::Rainforest;
        for(auto sp:{Species::Kapok,Species::StranglerFig,Species::Palm,Species::Bamboo,Species::Monstera,Species::Bromeliad})
            require(plantSuitability(sp,s,.1,.2,0)>0,"Warm humid tropical habitat supports its plants");
        require(plantSuitability(Species::Redwood,s,.1,.2,0)==0,"Redwood does not replace the tropical canopy");
        s.temperature=287;s.precipitation=1400;s.biome=Biome::TemperateRainforest;
        for(auto sp:{Species::Redwood,Species::Beech,Species::Fir,Species::ForestMoss})
            require(plantSuitability(sp,s,.1,.2,0)>0,"Cool humid forest has its own flora");
        s.freshwater=true;s.waterDepth=1;
        for(unsigned i=static_cast<unsigned>(Species::Spruce);i<speciesCount;++i)
            require(plantSuitability(static_cast<Species>(i),s,.1,.2,0)==0,"Forest roots cannot spawn underwater");
        s.freshwater=false;s.elevation=3500;s.temperature=275;s.biome=Biome::AlpineMeadow;
        for(unsigned i=1;i<speciesCount;++i) if(treeSpecies(static_cast<Species>(i)))
            require(plantSuitability(static_cast<Species>(i),s,.1,.2,0)==0,"Alpine meadow remains above the forest line");
    });
    test("biomes","New plant meshes have bounded LOD and finite shadow geometry",[] {
        for(unsigned i=static_cast<unsigned>(Species::Spruce);i<speciesCount;++i) {
            const auto sp=static_cast<Species>(i);const auto& t=plantTraits(sp);
            SurfaceObject o;o.id=719;o.species=sp;o.kind=treeSpecies(sp) ? DetailKind::Tree : DetailKind::Grass;
            o.height=t.height;o.radius=t.height*t.widthRatio;o.evergreen=coniferSpecies(sp);o.plant=plantState(sp,o.id,0,1);
            const auto nearMesh=detailMesh(o,{},Vec3{0,1,0},Vec3{1,0,0},false),farMesh=detailMesh(o,{},Vec3{0,1,0},Vec3{1,0,0},true);
            require(!nearMesh.empty() && farMesh.size()<nearMesh.size() && nearMesh.size()<2000,"Bounded plant geometry simplifies with distance");
            std::vector<Triangle> casters;
            for(const auto& f:nearMesh) {
                require(std::isfinite(length(f.triangle.a)) && length(cross(f.triangle.b-f.triangle.a,f.triangle.c-f.triangle.a))>1e-10,"Finite nondegenerate triangles");
                casters.push_back(f.triangle);
            }
            const auto shadow=shadowVolume(casters,normalized(Vec3{1,1,.5}));require(!shadow.empty() && shadow.size()<10000,"Plant casts a bounded shadow volume");
        }
    });
    test("biomes","Forest roots and growth retain identity across moving queries",[] {
        World w;const auto p=findForest(w,Biome::Rainforest).position;
        const auto sites=surfaceSites(w,p,100),shifted=surfaceSites(w,normalized(p+east(p)*(12/w.config().radius)),100);
        std::map<std::uint64_t,SurfaceObject> byId;for(const auto& s:shifted) require(byId.emplace(s.id,s).second,"Unique layered forest cells");
        Flora seedlings;seedlings.setYoung(true);Flora mature=seedlings;mature.advanceDays(w,25000);unsigned matched=0,growing=0;
        for(const auto& site:sites) if(byId.contains(site.id)) {
            const auto& other=byId.at(site.id);near(length(site.up-other.up),0,0,"Root position is observer independent");
            require(site.species==other.species,"Species is stable across the dense layer");++matched;
            if(site.kind==DetailKind::Tree && plantTraits(site.species).lifespanDays>40000) {
                const auto young=grownObject(site,&seedlings),old=grownObject(site,&mature);
                require(old.height>young.height && old.collisionRadius()>young.collisionRadius(),"Visual tree and solid trunk grow together");++growing;
            }
        }
        require(matched>100 && growing>15,"Overlapping forest regions preserve ample growing trees");
    });
    test("biomes","Worker plant snapshots preserve geometry after live growth changes",[] {
        World w;const auto p=findForest(w,Biome::Rainforest).position;const Vec3 e=east(p),n=-north(p);const double radius=w.config().radius;
        std::vector<PlantSite> sites;for(const auto& object:surfaceSites(w,p,80)) {
            const Vec3 v=object.up*(radius+object.elevation)-p*radius;
            sites.push_back({object,{dot(v,e),dot(v,p),dot(v,n)},0});
        }
        Flora flora;const auto expected=makePlantMesh(sites,p,e,n,radius,flora);
        auto job=std::async(std::launch::async,[sites,p,e,n,radius,flora]{return makePlantMesh(sites,p,e,n,radius,flora);});
        sites.clear();flora.setYoung(true);flora.advanceDays(w,20000);const auto actual=job.get();
        require(actual.vertices.size()==expected.vertices.size() && actual.casters.size()==expected.casters.size(),"Snapshot owns both solid and shadow geometry");
        near(actual.heightSum,expected.heightSum,0,"Telemetry describes the displayed snapshot");
        require(actual.trees>30 && actual.lianaHosts>0,"Dense tropical mesh includes its canopy");
        for(std::size_t i=0;i<actual.vertices.size();++i) near(length(actual.vertices[i].position-expected.vertices[i].position),0,0,"Background mesh matches the reference");
    });
    test("biomes","Forest navigation honours disabled modules",[] {
        World w;w.modules().set(Module::SurfaceDetail,false);bool rejected=false;
        try {(void)findForest(w,Biome::Rainforest);} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected && surfaceSites(w,w.findLand(),100).empty(),"Disabled surface detail has no hidden forest");
        w.modules().set(Module::SurfaceDetail,true);w.modules().set(Module::Biomes,false);rejected=false;
        try {(void)findForest(w,Biome::Rainforest);} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected,"Forest tour explains its biome dependency");
        require(!forestType("desert") && forestType("jungle")==Biome::Rainforest,"Explicit route names");
    });
    test("biomes","Sparse tropical islands remain visitable without a giant-tree requirement",[] {
        Config config;config.seed=119;World w(config);const auto place=findForest(w,Biome::Rainforest);
        require(w.climatology(place.position).biome==Biome::Rainforest,"Route stays in the requested real habitat");
        require(w.surface(place.position)-w.elevation(place.position)<.05,"Island route reaches dry ground");
        unsigned trees=0,vines=0;for(const auto& o:surfaceObjects(w,place.position,100)) {if(o.kind==DetailKind::Tree) ++trees;if(o.lianas()) ++vines;}
        require(trees>25 && vines>0,"An existing tropical canopy does not need an exceptional tallest tree to be visitable");
    });
}
