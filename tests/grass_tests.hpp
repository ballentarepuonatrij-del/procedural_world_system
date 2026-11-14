GrassGround grassTestGround(Vec3 up,Vec3 anchor,double radius) {
    GrassGround g;g.sample.temperature=289;g.sample.precipitation=950;g.sample.elevation=200;g.sample.biome=Biome::Grassland;
    const auto d=up*(radius+200)-anchor*radius;
    g.base={dot(d,east(anchor)),dot(d,anchor),-dot(d,north(anchor))};return g;
}
std::vector<GrassRoot> grassTestPatch(const Config& c,Vec3 anchor) {
    std::vector<GrassRoot> roots;
    for(int z=-14;z<=14;++z) for(int x=-14;x<=14;++x) {
        GrassRoot r;r.up=normalized(anchor+(east(anchor)*x+north(anchor)*z)/c.radius);r.elevation=200;r.height=1;r.suitability=1;
        roots.push_back(r);
    }
    return roots;
}
void registerGrassTests() {
    test("grass","Habitat excludes water steep rock and covered ground",[] {
        auto g=grassTestGround({1,0,0},{1,0,0},6371000);
        const double full=grassCoverage(g,0);require(full>.9,"Temperate wet meadow supports dense grass");
        g.sample.ocean=true;near(grassCoverage(g,0),0,0,"Ocean exclusion");g.sample.ocean=false;
        g.sample.waterDepth=.1;near(grassCoverage(g,0),0,0,"Inland and runoff water exclusion");g.sample.waterDepth=0;
        g.slope=.8;near(grassCoverage(g,0),0,0,"Cliff exclusion");g.slope=0;
        g.supported=false;near(grassCoverage(g,0),0,0,"Cave and solid obstruction exclusion");g.supported=true;
        g.sample.temperature=250;near(grassCoverage(g,0),0,0,"Frozen climate");g.sample.temperature=289;
        g.sample.precipitation=100;near(grassCoverage(g,0),0,0,"Arid climate");g.sample.precipitation=950;
        g.sample.sand=1;require(grassCoverage(g,0)<full*.2,"Sandy substrate is sparse");g.sample.sand=0;
        g.sample.biome=Biome::Rainforest;require(grassCoverage(g,0)<full*.2,"Canopy limits cover");
    });
    test("grass","Root identities persist across motion cube seams and poles",[] {
        World world;const double r=world.config().radius;
        for(const auto p:{Vec3{1,0,0},normalized(Vec3{1,1,1}),Vec3{0,1,0},Vec3{0,-1,0}}) {
            const auto ground=[&](Vec3 up){return grassTestGround(up,p,r);};
            const auto roots=makeGrassRoots(world,p,ground),again=makeGrassRoots(world,p,ground);
            require(roots.size()>1000 && roots.size()<=maximumGrassRoots,"Bounded dense coverage at every orientation");
            require(roots.size()==again.size(),"Repeatable root count");std::map<std::uint64_t,GrassRoot> ids;
            for(std::size_t i=0;i<roots.size();++i) {
                const auto& a=roots[i];require(ids.emplace(a.id,a).second,"No duplicate cube-face roots");
                require(a.id==again[i].id,"Stable identity");near(length(a.up-again[i].up),0,0,"Stable position");
                require(length(a.up-p)*r<=grassRadius && std::isfinite(length(a.base)),"Root radius and finite coordinate");
            }
            const auto moved=normalized(p+east(p)*(3/r));const auto other=makeGrassRoots(world,moved,ground);unsigned common=0;
            for(const auto& b:other) if(const auto a=ids.find(b.id);a!=ids.end()) {++common;near(length(a->second.up-b.up),0,0,"Roots stay in world coordinates");near(a->second.height,b.height,0,"Stable height");}
            require(common*5>roots.size()*4,"Camera motion retains overlapping grass");
        }
    });
    test("grass","Seeds snow modules and growth control bounded blade meshes",[] {
        World world;const Vec3 p{1,0,0};const double r=world.config().radius;
        const auto ground=[&](Vec3 up){return grassTestGround(up,p,r);};
        auto roots=makeGrassRoots(world,p,ground);Flora flora;const auto mesh=makeGrassMesh(roots,p,r,flora);
        require(mesh.shortCount>50 && mesh.tallCount>50,"Both low and high grass appear");
        require(mesh.vertices.size()<=maximumGrassVertices && mesh.vertices.size()%3==0,"Complete bounded triangles");
        for(const auto& v:mesh.vertices) {
            require(std::isfinite(length(v.position)) && std::isfinite(length(v.normal)),"Finite blade geometry");
            near(length(bendGrassPosition(v,{})-v.position),0,1e-12,"Unforced shape");
            if(v.flex==0) near(length(bendGrassPosition(v,{.6,0,.3})-v.position),0,1e-12,"Roots do not slide under force");
            require(bendGrassPosition(v,{100,0,100}).y>=v.root.y-1e-9,"Clamped bending cannot push the blade below its root");
        }
        auto buried=makeGrassRoots(world,p,[&](Vec3 up){auto g=ground(up);g.snow=2;return g;});require(buried.empty(),"Deep snow buries grass");
        const auto heightSum=[](const GrassMesh& m){double h=0;for(const auto& clump:m.clumps) h+=clump.height;return h;};
        flora.setYoung(true);const auto young=makeGrassMesh(roots,p,r,flora);
        require(heightSum(young)<heightSum(mesh)*.1,"Young grass remains short and sparse");
        flora.advanceDays(world,60);require(heightSum(makeGrassMesh(roots,p,r,flora))>heightSum(young)*2,"Growth develops young grass");
        Config c=world.config();++c.seed;World changed(c);const auto different=makeGrassRoots(changed,p,ground);
        require(!different.empty() && different.front().id!=roots.front().id,"Seed changes distribution");
        world.modules().set(Module::SurfaceDetail,false);require(makeGrassRoots(world,p,ground).empty(),"Detail switch removes the cover");
    });
    test("grass","Spring solution is frame independent stable and reversible",[] {
        GrassSpring single{{.2,0,-.4},{.5,0,.1}},split=single;const Vec3 target{.4,0,.7};
        advanceGrassSpring(single,target,.9,1.35);for(int i=0;i<90;++i) advanceGrassSpring(split,target,.01,1.35);
        near(length(single.bend-split.bend),0,2e-14,"Partition-independent displacement");near(length(single.velocity-split.velocity),0,2e-14,"Partition-independent velocity");
        const auto before=single;advanceGrassSpring(single,{},0,1.35);near(length(single.bend-before.bend),0,0,"Pause freezes state");
        advanceGrassSpring(single,{},4,1.35);require(length(single.bend)+length(single.velocity)<1e-8,"Released grass recovers");
        advanceGrassSpring(single,target,120,1.35);near(length(single.bend-target),0,1e-12,"Long steps remain bounded");
        for(double dt:{-1.,121.,std::numeric_limits<double>::quiet_NaN()}) {bool rejected=false;try {advanceGrassSpring(single,{},dt,1);} catch(const std::invalid_argument&) {rejected=true;}require(rejected,"Invalid step rejected");}
    });
    test("grass","Wind load is horizontal stronger for tall grass and bounded",[] {
        near(length(grassWindTarget({},true)),0,0,"No wind no load");
        const auto shortWind=grassWindTarget({3,100,4},false),tallWind=grassWindTarget({3,100,4},true);
        require(length(tallWind)>length(shortWind),"Tall flexible grass bends further");near(tallWind.y,0,0,"Vertical air is excluded");
        near(tallWind.x/tallWind.z,.75,1e-12,"Wind direction is preserved");
        require(length(grassWindTarget({1e4,0,1e4},true))<=.650001,"Extreme wind stays bounded");
    });
    test("grass","Swept player and animal contacts bend grass then release",[] {
        Config c;const Vec3 p{1,0,0};auto roots=grassTestPatch(c,p);GrassField field;field.synchronize(c,p,roots);
        const auto foot=p*(c.radius+200);const auto e=east(p),n=north(p);
        std::array bodies{GrassCollider{foot-e*4,foot+e*4,.45,1.8},GrassCollider{foot+n*7,foot+n*7,.65,1.2}};
        field.advance(.1,bodies);require(field.activeCells()>12 && field.maximumBend()>.1,"Swept path and animal footprint disturb multiple cells");
        require(length(field.sample(foot))>.1 && length(field.sample(foot+n*7))>.1,"Both bodies reach the grass");
        const auto texture=field.texture();field.advance(0,{});require(texture==field.texture(),"Pause preserves contact texture exactly");
        field.advance(4,{});require(field.maximumBend()<1e-8,"Grass recovers after bodies leave");
        field.advance(1,{});require(field.activeCells()==0,"Settled cells stop work");
        near(length(field.sample(foot+n*100)),0,0,"Outside the local field is neutral");
    });
    test("grass","Flying objects and teleports do not flatten an artificial path",[] {
        Config c;const Vec3 p{1,0,0};const auto roots=grassTestPatch(c,p);GrassField field;field.synchronize(c,p,roots);
        const auto foot=p*(c.radius+200),e=east(p);
        std::array body{GrassCollider{foot+p*5-e*4,foot+p*5+e*4,.45,1.8}};
        field.advance(.2,body);near(field.maximumBend(),0,0,"Airborne body clears vegetation");
        body[0]={foot-e*10,foot+e*10,.45,1.8};field.advance(.2,body);
        near(length(field.sample(foot)),0,0,"Teleport has no swept trail");require(length(field.sample(foot+e*10))>.1,"Arrival still bends local grass");
    });
    test("grass","Local field preserves overlap and clears for seed or distant relocation",[] {
        Config c;const Vec3 p{1,0,0};const auto roots=grassTestPatch(c,p);GrassField field;field.synchronize(c,p,roots);
        const auto foot=p*(c.radius+200);const std::array body{GrassCollider{foot,foot,.5,1.8}};
        field.advance(.1,body);const auto before=field.sample(foot);require(length(before)>.1,"Initial contact");
        field.synchronize(c,normalized(p+east(p)*(5/c.radius)),roots);
        near(length(field.sample(foot)-before),0,1e-12,"Moving grid retains overlapping spring state");
        const auto revision=field.revision();++c.seed;field.synchronize(c,p,roots);near(field.maximumBend(),0,0,"Seed reset clears old contacts");require(field.revision()>revision,"Texture invalidation survives reset");
        field.advance(.1,body);field.synchronize(c,normalized(p+north(p)*(1000/c.radius)),{});near(field.maximumBend(),0,0,"Long relocation clears state");
        field.clear();near(length(field.sample(foot)),0,0,"Load/reset clears field");
    });
    test("grass","Terrain integration keeps cover off fast-flight meshes",[] {
        World world;const auto p=world.findLand();Hydrology water(world,p);const double eye=world.surface(p)+Explorer::eyeHeight;
        const auto nearMesh=makeTerrainMesh(world,water,p,eye,79*86400),fastMesh=makeTerrainMesh(world,water,p,eye,79*86400,64);
        require(!nearMesh.grass.empty() && fastMesh.grass.empty(),"Grass is built only at walking detail");
        for(const auto& root:nearMesh.grass) require(std::isfinite(length(root.base)) && root.height>root.snow,"Terrain roots are finite and emerge above snow");
        near(world.surface(p)+Explorer::eyeHeight,eye,0,"Cover never modifies the navigation surface");
    });
}
