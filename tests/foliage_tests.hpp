SurfaceObject foliageTestPlant(Species species,DetailKind kind,double height=2) {
    SurfaceObject o;o.id=490;o.species=species;o.kind=kind;o.up={0,1,0};o.height=height;o.radius=height*.35;o.elevation=200;
    o.plant={1000,1,1,GrowthStage::Mature,0};o.evergreen=coniferSpecies(species);return o;
}
void registerFoliageTests() {
    test("foliage","Cutout shoots have a rooted stem, porous leaf silhouettes and a solid material tile",[] {
        const auto atlas=makeLeafAtlas();
        require(atlas.size()==leafAtlasSize*leafAtlasSize,"Complete atlas");
        std::array<unsigned,3> coverage{};
        for(const auto kind:{LeafSpray::Broad,LeafSpray::Narrow,LeafSpray::Needles}) {
            const auto uv=leafCoordinates(kind,0,0);
            const int x0=static_cast<int>(uv.u*leafAtlasSize),y0=static_cast<int>(uv.v*leafAtlasSize);
            auto alpha=[&](int x,int y){return atlas[(y0+y)*leafAtlasSize+x0+x][3];};
            require(alpha(leafTileSize/2,0)>86,"Stem reaches its attachment");
            for(int y=0;y<leafTileSize;++y) {
                int solid=0;for(int x=0;x<leafTileSize;++x) solid+=alpha(x,y)>86;
                if(y>4 && y<leafTileSize*.85) require(solid>0 && solid<leafTileSize-2,"Sky is visible between leaf edges");
                coverage[static_cast<unsigned>(kind)]+=static_cast<unsigned>(solid);
            }
        }
        require(coverage[0]!=coverage[1] && coverage[1]!=coverage[2] && coverage[0]!=coverage[2],"Three distinct shoot silhouettes");
        for(int y=leafTileSize;y<leafAtlasSize;++y) for(int x=leafTileSize;x<leafAtlasSize;++x)
            require(atlas[y*leafAtlasSize+x]==LeafPixel{255,255,255,255},"Bark and other materials remain opaque");
    });
    test("foliage","Every tree detail level carries its leaves on branching wood",[] {
        for(const auto species:{Species::Oak,Species::Willow,Species::Birch,Species::Acacia,Species::Beech,Species::Alder,
                                Species::TropicalTree,Species::Kapok,Species::StranglerFig,Species::Pine,Species::Spruce,Species::Fir,Species::Redwood}) {
            const auto o=foliageTestPlant(species,DetailKind::Tree,10);
            for(const auto detail:{0,1,2}) {
                std::vector<Triangle> shadows,wood;std::vector<Vec3> roots;
                bool lowBranch=false,highBranch=false;
                for(const auto& face:detailMesh(o,{},o.up,{1,0,0},detail==0,detail==2,&shadows)) {
                    const auto& t=face.triangle;
                    if(face.uv[0].u<.5f || face.uv[0].v<.5f) {
                        require(t.twoSided && !face.castsShadow && face.flutter.x==0 && length(face.flutter)>0,
                            "Two-sided masked foliage flutters around a fixed attachment");
                        if(roots.empty() || length(t.a-roots.back())>1e-9) roots.push_back(t.a);
                    } else {
                        require(face.color.r>=face.color.g,"All green canopy surfaces use a leaf mask");
                        wood.push_back(t);
                        for(const auto p:{t.a,t.b,t.c}) if(std::hypot(p.x,p.z)>o.radius*.21) {
                            lowBranch|=p.y<o.height*.78;highBranch|=p.y>=o.height*.78;
                        }
                    }
                }
                require(roots.size()>8 && lowBranch && highBranch && !shadows.empty(),"Wood and foliage span multiple crown heights");
                for(const auto root:roots) {
                    double nearest=1e9;
                    for(const auto& t:wood) {
                        const std::array corners{t.a,t.b,t.c};
                        for(std::size_t i=0;i<3;++i) {
                            const auto a=corners[i],edge=corners[(i+1)%3]-a;
                            const double k=clamp(dot(root-a,edge)/dot(edge,edge),0.,1.);
                            nearest=std::min(nearest,length(root-a-edge*k));
                        }
                    }
                    require(nearest<o.height*.044,"Each visible shoot starts on a woody limb");
                }
            }
            auto dead=o;dead.plant.stage=GrowthStage::Dead;std::vector<Triangle> shadows;
            const auto bare=detailMesh(dead,{},dead.up,{1,0,0},false,true,&shadows);
            require(!bare.empty() && shadows.empty(),"Dead wood has no canopy shadow");
            for(const auto& f:bare) require(f.uv[0].u>.5f && f.uv[0].v>.5f,"Dead trees retain only bare wood");
        }
    });
    test("foliage","Visibility bounds include every bent triangle on chunk boundaries",[] {
        std::vector<FoliageVertex> vertices(foliageBatchVertices+6);
        for(std::size_t i=0;i<vertices.size();++i) {
            auto& v=vertices[i];v.position={i<foliageBatchVertices ? 8. : -32.,static_cast<double>(i%13),4};
            v.height=20;v.flex=static_cast<float>(i%13)/12;v.flutter=.13f;v.leafPhase=static_cast<float>(i%7);
        }
        const auto batches=foliageBatches(vertices);
        require(batches.size()==2 && batches[0].count==foliageBatchVertices && batches[1].count==6,"Complete triangle blocks");
        for(std::size_t i=0;i<vertices.size();++i) for(double force:{0.,.13,.65}) {
            const auto& b=batches[i/foliageBatchVertices];
            const auto p=bendFoliagePosition(vertices[i],normalized(Vec3{-1,.1,1})*force,1.,1.);
            require(length(p-b.center)<=b.sweptRadius(force,1.),"Wind and contact stay inside the conservative bound");
        }
    });
    test("foliage","Leaf clusters and needle sprays are deterministic attached geometry",[] {
        for(auto species:{Species::Oak,Species::Willow,Species::Pine,Species::Heather,Species::Palm}) {
            const auto o=foliageTestPlant(species,species==Species::Heather ? DetailKind::Shrub : DetailKind::Tree);
            const auto a=detailMesh(o,{},o.up,{1,0,0},false,true),b=detailMesh(o,{},o.up,{1,0,0},false,true);
            require(!a.empty() && a.size()==b.size(),"Stable geometry count");std::size_t flexible=0;
            for(std::size_t i=0;i<a.size();++i) {
                near(length(a[i].triangle.a-b[i].triangle.a),0,0,"Fixed seed reproduces leaves");
                const auto& f=a[i];require(std::isfinite(length(cross(f.triangle.b-f.triangle.a,f.triangle.c-f.triangle.a))),"Finite leaf surface");
                if(length(f.flutter)>0) {++flexible;require(f.triangle.twoSided,"Leaves are two-sided");}
            }
            require(flexible>8,"Species has flexible leaf geometry");
        }
        auto dead=foliageTestPlant(Species::Oak,DetailKind::Tree);dead.plant.stage=GrowthStage::Dead;
        require(!flexiblePlant(dead),"Dead tree has no living canopy motion");
        for(const auto& face:detailMesh(dead,{},dead.up,{1,0,0},false,true)) require(!face.microFoliage,"Dead branches have no new leaves");
        auto rock=foliageTestPlant(Species::None,DetailKind::Rock);require(!flexiblePlant(rock),"Rock never bends");
    });
    test("foliage","Wind follows the spherical tangent and stiffness limits large trees",[] {
        auto small=foliageTestPlant(Species::Heather,DetailKind::Shrub),large=foliageTestPlant(Species::Oak,DetailKind::Tree,30);
        for(auto up:{Vec3{1,0,0},Vec3{0,1,0},normalized(Vec3{1,1,1})}) {
            small.up=large.up=up;const auto a=foliageRoot(small,6371000),b=foliageRoot(large,6371000);
            const auto v=foliageWindTarget(a,east(up)*10+up*70);
            near(dot(v,up),0,1e-12,"No vertical wind load");require(dot(v,east(up))>0,"Bends downwind");
            require(length(v)>length(foliageWindTarget(b,east(up)*10)),"Tall woody stems resist wind");
            require(length(foliageWindTarget(a,east(up)*1e6))<=.32,"Storm displacement is bounded");
            near(length(foliageWindTarget(a,{})),0,0,"Calm load is zero");
        }
    });
    test("foliage","Swept bodies displace nearby crowns and avoid trunks above the body",[] {
        auto shrub=foliageRoot(foliageTestPlant(Species::Heather,DetailKind::Shrub),1000);
        auto tree=foliageRoot(foliageTestPlant(Species::Oak,DetailKind::Tree,20),1000);tree.id++;
        FoliageField field;std::array roots{shrub,tree};field.synchronize(roots);
        const auto foot=shrub.position;const std::array bodies{GrassCollider{foot-Vec3{4,0,0},foot+Vec3{4,0,0},.45,1.8}};
        field.advance(.2,.2,{},foot,bodies);require(length(field.bend(0))>.1,"Swept player bends the shrub");
        near(length(field.bend(1)),0,0,"Passing beneath a canopy does not shake it");
        const auto before=field.bend(0);field.advance(0,0,{},foot,{});near(length(field.bend(0)-before),0,0,"Stationary pause is exact");
        field.advance(4,4,{},foot,{});require(length(field.bend(0))<1e-8,"Released branches spring back");
        const std::array animal{GrassCollider{foot,foot,.65,1.1}};field.advance(0,.2,{},foot,animal);
        require(field.contacts()==1 && field.maximumContact()>.1,"Independent actor/contact clock works on pause");
    });
    test("foliage","Flight and teleports cannot sweep an artificial corridor",[] {
        const auto root=foliageRoot(foliageTestPlant(Species::Heather,DetailKind::Shrub),1000);
        FoliageField field;field.synchronize(std::span(&root,1));const auto p=root.position;
        std::array bodies{GrassCollider{p-Vec3{10,0,0},p+Vec3{10,0,0},.45,1.8}};
        field.advance(.2,.2,{},p,bodies);near(length(field.bend(0)),0,0,"Long relocation has no trail");
        bodies[0]={p+Vec3{-4,5,0},p+Vec3{4,5,0},.45,1.8};field.advance(.2,.2,{},p,bodies);
        near(length(field.bend(0)),0,0,"High flying actor misses low plants");
        bodies[0]={p+Vec3{50,0,0},p,.45,1.8};field.advance(.2,.2,{},p,bodies);
        require(length(field.bend(0))>.1,"Arrival can contact a local shrub");
    });
    test("foliage","Streaming retains springs by identity and generation and remains bounded",[] {
        auto a=foliageRoot(foliageTestPlant(Species::Heather,DetailKind::Shrub),1000),b=a;b.id++;
        FoliageField field;std::array roots{a,b};field.synchronize(roots);
        const std::array body{GrassCollider{a.position,a.position,.5,1.8}};
        field.advance(.2,.2,{10,0,0},a.position,body);const auto before=field.bend(0);
        std::swap(roots[0],roots[1]);field.synchronize(roots);near(length(field.bend(1)-before),0,0,"Mesh reordering retains disturbance");
        roots[1].generation++;field.synchronize(roots);near(length(field.bend(1)),0,0,"Replacement generation has fresh springs");
        std::vector<FoliageRoot> many(maximumFoliagePlants+1,a);bool rejected=false;
        try {field.synchronize(many);} catch(const std::invalid_argument&) {rejected=true;}require(rejected,"Spring cap is enforced");
        field.clear();require(field.roots().empty() && field.contacts()==0,"Load/reset clears transient forces");
    });
    test("foliage","Roots remain fixed and forcing has stable finite limits",[] {
        FoliageVertex v;v.position={1,0,2};v.height=30;v.normal={0,1,0};
        near(length(bendFoliagePosition(v,{.5,0,.5},1,1)-v.position),0,0,"Anchored trunk base never moves");
        v.flex=1;v.flutter=.05f;
        require(std::isfinite(length(bendFoliagePosition(v,{1000,0,1000},1,1))),"Large force stays finite");
        FoliageField one,split;const auto root=foliageRoot(foliageTestPlant(Species::Oak,DetailKind::Tree,10),1000);
        one.synchronize(std::span(&root,1));split.synchronize(std::span(&root,1));
        one.advance(1,1,{10,0,0},root.position,{});for(int i=0;i<100;++i) split.advance(.01,.01,{10,0,0},root.position,{});
        near(length(one.bend(0)-split.bend(0)),0,1e-12,"Wind spring is independent of frame partition");
        for(double dt:{-1.,121.,std::numeric_limits<double>::quiet_NaN()}) {
            bool rejected=false;try {one.advance(dt,0,{},root.position,{});} catch(const std::invalid_argument&) {rejected=true;}require(rejected,"Invalid timestep rejected");
        }
    });
    test("foliage","Scene integration caps leaves and preserves cached crown shadows",[] {
        World world;const auto anchor=findGrove(world).position;Hydrology water(world,anchor);Flora flora;
        const auto terrain=makeTerrainMesh(world,water,anchor,world.surface(anchor)+Explorer::eyeHeight,79*86400);
        const auto mesh=makePlantMesh(terrain.plants,terrain.anchor,terrain.east,terrain.north,world.config().radius,flora);
        require(!mesh.foliage.empty() && mesh.foliage.size()<=maximumFoliagePlants,"Grove has bounded moving plants");
        require(mesh.leafVertices>0 && mesh.leafVertices<=maximumLeafVertices,"Nearby individual leaves stay in budget");
        std::size_t count=0;for(const auto& r:mesh.ranges) {require(r.first==count,"No lost triangles in draw ranges");count+=r.count;}
        require(count==mesh.moving.size(),"Every static and dynamic vertex belongs to a plant");
        for(const auto& v:mesh.moving) require(v.slot<static_cast<float>(mesh.foliage.size()) && std::isfinite(length(v.position)),"Slots address the matching snapshot");
        require(!mesh.casters.empty(),"Simplified canopy shadows are retained");
    });
}
