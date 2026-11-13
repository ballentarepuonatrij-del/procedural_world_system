void registerMaterialTests() {
    test("materials","Continuous slope climate and snow mixtures stay normalized",[] {
        Sample sample;sample.precipitation=800;sample.elevation=500;
        for(int slope=0;slope<=40;++slope) for(int depth=0;depth<=12;++depth) {
            const auto m=surfaceMaterial(sample,{121,507,813},slope/40.,depth*.01,{},42);
            double sum=0;for(const auto w:m.surface.weights) {require(w>=0 && w<=1,"Bounded material weight");sum+=w;}
            near(sum,1,1e-6,"Partition of unity");
            require(m.albedo.r>0 && m.albedo.r<=1 && m.albedo.g>0 && m.albedo.g<=1 && m.albedo.b>0 && m.albedo.b<=1,"Finite albedo");
        }
        auto flat=surfaceMaterial(sample,{121,507,813},1,0,{},42);
        auto cliff=surfaceMaterial(sample,{121,507,813},0,.5,{},42);
        require(flat.surface.weights[1]>.99 && cliff.surface.weights[0]>.99,"Soil on flats and exposed rock on vertical faces");
        require(cliff.surface.weights[3]==0,"Snow does not coat vertical walls");
        const auto snow=surfaceMaterial(sample,{121,507,813},1,.2,{},42);
        near(snow.surface.weights[3],1,0,"Thick snow covers flat ground");
    });
    test("materials","Material transitions have no biome threshold jumps",[] {
        Sample s;s.elevation=300;
        MaterialSample previous{};bool first=true;
        for(int rain=0;rain<=2000;++rain) {
            s.precipitation=rain;s.biome=rain<280 ? Biome::Desert : rain<800 ? Biome::Grassland : Biome::Forest;
            const auto m=surfaceMaterial(s,{14,500,70},1,0,{},42);
            if(!first) {
                require(std::abs(m.surface.weights[2]-previous.surface.weights[2])<.007,"Smooth sand/soil boundary");
                require(std::abs(m.albedo.g-previous.albedo.g)<.003,"Continuous color across biome labels");
            }
            first=false;previous=m;
        }
        s.sand=1;require(surfaceMaterial(s,{14,500,70},1,0,{},42).surface.weights[2]>.99,"Coastal sand override");
        const auto bare=surfaceMaterial(s,{14,500,70},1,0,{},42,false);
        require(bare.surface.weights[0]>.99,"Disabled biomes keep bare rock");
    });
    test("materials","Wetting darkens and smooths each substrate continuously",[] {
        const Color dry{.6f,.5f,.4f};
        for(std::size_t kind=0;kind<4;++kind) {
            SurfaceMaterial material;material.weights={0,0,0,0};material.weights[kind]=1;
            double lastColor=dry.r,lastRoughness=materialRoughness(material);
            for(int i=0;i<=100;++i) {
                material.wetness=static_cast<float>(i/100.);
                const auto color=wetMaterialColor(dry,material);const auto rough=materialRoughness(material);
                require(color.r<=lastColor+1e-6 && color.r>0,"Monotone wet darkening");
                require(rough<=lastRoughness+1e-6 && rough>=.2,"Monotone bounded wet roughness");
                lastColor=color.r;lastRoughness=rough;
            }
        }
        near(materialWetness(0,0),0,0,"Dry ground");near(materialWetness(1,0),1,0,"Saturated ground");
        near(materialWetness(0,.004),1,0,"Water film wets bare rock");
        require(materialWetness(.75,0)>materialWetness(.5,0),"Soil retains visible moisture after film drains");
    });
    test("materials","Hydrology edge fades and sheltered rock excludes snow",[] {
        Sample s;s.precipitation=300;s.sand=.5;
        GroundSample ground;ground.available=true;ground.saturation=1;
        const auto dry=surfaceMaterial(s,{14,500,70},1,0,{},42);
        auto previous=dry;
        for(int i=0;i<=100;++i) {
            ground.weight=i/100.;const auto wet=surfaceMaterial(s,{14,500,70},1,0,ground,42);
            require(wet.surface.wetness>=previous.surface.wetness,"Smooth wet domain border");previous=wet;
        }
        near(previous.surface.wetness,1,0,"Live saturated ground replaces background");
        const auto cave=surfaceMaterial(s,{14,500,70},1,.5,ground,42,true,true);
        require(cave.surface.weights[0]==1 && cave.surface.weights[3]==0,"Cave stays rock");
        require(cave.surface.wetness<.1,"Masked cave is not flooded by surface wetness");
    });
    test("materials","Packed patterns are deterministic distinct and mip filtered",[] {
        const auto a=makeMaterialTexture(),b=makeMaterialTexture();
        require(a.size()==9 && a.back().size==1,"Complete 256 to 1 mip chain");
        for(std::size_t level=0;level<a.size();++level) {
            require(a[level].rgba==b[level].rgba,"Reproducible procedural texture");
            require(a[level].rgba.size()==static_cast<std::size_t>(a[level].size*a[level].size*4),"Bounded allocation");
        }
        std::array<double,4> mean{},variance{};
        for(std::size_t i=0;i<a[0].rgba.size();++i) mean[i%4]+=a[0].rgba[i]/65536.;
        for(std::size_t i=0;i<a[0].rgba.size();++i) variance[i%4]+=std::pow(a[0].rgba[i]-mean[i%4],2)/65536.;
        require(variance[0]>variance[1] && variance[1]>variance[2] && variance[2]>variance[3],"Rock is strongest, snow softest");
        for(std::size_t c=0;c<4;++c) near(a.back().rgba[c],mean[c],4,"Distant mip tends to the spatial average");
        for(int bad:{0,15,255,2048}) {bool rejected=false;try {(void)makeMaterialTexture(bad);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Invalid texture budget rejected");}
    });
    test("materials","World coordinates survive recentering poles and longitude seam",[] {
        const double radius=6371000;
        for(Vec3 anchor:{direction(.6,2),direction(pi/2,0),direction(-pi/2,0),direction(0,pi),direction(0,-pi)}) {
            const Vec3 p=anchor*(radius+50)+east(anchor)*17;
            for(Vec3 origin:{anchor*radius,(anchor*radius+east(anchor)*23+north(anchor)*7)}) {
                const auto wrapped=materialOrigin(origin)+(p-origin);
                near(std::remainder(wrapped.x-p.x,materialPeriod),0,1e-8,"Stable X phase");
                near(std::remainder(wrapped.y-p.y,materialPeriod),0,1e-8,"Stable Y phase");
                near(std::remainder(wrapped.z-p.z,materialPeriod),0,1e-8,"Stable Z phase");
            }
            Sample s;s.precipitation=600;const auto a=surfaceMaterial(s,p,.9,.03,{},42),b=surfaceMaterial(s,p,.9,.03,{},42);
            require(a.surface.weights==b.surface.weights && a.albedo.r==b.albedo.r,"No observer or time dependence in pattern");
        }
    });
    test("materials","Rendering wet materials leaves water and navigation untouched",[] {
        World w;Explorer explorer(w);Simulation sim(w,explorer.up(),79*86400);
        const auto p=explorer.up();const double height=explorer.altitude();const auto budget=sim.water.budget();
        const auto mesh=makeTerrainMesh(w,sim.water,p,height,79*86400,64);
        for(const auto& v:mesh.terrain) {
            double sum=0;for(float f:v.material.weights) sum+=f;
            near(sum,1,1e-5,"Streamed triangles carry normalized material weights");
            require(v.material.wetness>=0 && v.material.wetness<=1,"Streamed moisture bounded");
        }
        near(explorer.altitude(),height,0,"No collision displacement from material relief");
        near(sim.water.budget().storedWaterM3,budget.storedWaterM3,0,"Rendering does not consume water");
        near(length(explorer.up()-p),0,0,"Rendering does not move explorer");
    });
}
