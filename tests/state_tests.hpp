// Included in the portable test harness namespace.
struct StateFixture {
    World world;Explorer explorer;SessionState session;Simulation simulation;
    StateFixture():explorer(world),simulation(world,explorer.up(),session.seconds) {explorer.teleport(world,explorer.up(),&simulation.water);}
};
void registerStateTests() {
    test("state","Physical snapshot roundtrip preserves fields and continued evolution",[] {
        StateFixture f;f.simulation.setRainExperiment(8);f.simulation.motion.setWindExperiment(13);f.simulation.flora.advanceDays(f.world,1400);f.simulation.flora.setRate(500);
        f.simulation.advance(f.world,f.explorer.up(),7.35);f.session.seconds+=7.35;
        f.simulation.fauna.advance(f.world,f.explorer.up(),.25,f.explorer.altitude(),true,false);
        f.explorer.look(.73,-.21);f.session.phenomenon=Phenomenon::Rainbow;f.session.phenomenonEpoch=f.session.seconds-2;
        const auto bytes=encodeState(f.world,f.explorer,f.simulation,f.session,1234);auto copy=decodeState(bytes);
        require(bytes.size()<10*1024*1024,"Ordinary saves have a bounded practical size");
        require(encodeState(copy->world,copy->explorer,copy->simulation,copy->session,1234)==bytes,"Every serialized field roundtrips bit for bit");
        for(int i=0;i<4;++i) {
            for(auto* sim:{&f.simulation,&copy->simulation})sim->advance(f.world,f.explorer.up(),.35);
            f.explorer.update(f.world,{1,.2,0,false,false},.1,&f.simulation.water,&f.simulation.motion,&f.simulation.flora);
            copy->explorer.update(copy->world,{1,.2,0,false,false},.1,&copy->simulation.water,&copy->simulation.motion,&copy->simulation.flora);
        }
        require(encodeState(f.world,f.explorer,f.simulation,f.session,1234)==encodeState(copy->world,copy->explorer,copy->simulation,copy->session,1234),"No restart of water, wind, plant or physics clocks");
    });
    test("state","Flight and submerged position orientation and controls are retained",[] {
        StateFixture f;f.explorer.flying=true;f.explorer.flightSpeed=400;f.explorer.update(f.world,{0,0,1,false,false},2,&f.simulation.water,&f.simulation.motion);
        f.explorer.look(1,.45);auto flight=decodeState(encodeState(f.world,f.explorer,f.simulation,f.session,1));
        near(flight->explorer.altitude(),f.explorer.altitude(),0,"Flight height");near(flight->explorer.pitch(),f.explorer.pitch(),0,"Pitch");require(flight->explorer.flying && flight->explorer.flightSpeed==400,"Flight controls");
        const auto coast=f.world.findCoast();f.simulation.advance(f.world,coast.lookToward,0);f.explorer.flying=false;f.explorer.dive(f.world,coast.lookToward,8,&f.simulation.water,&f.simulation.motion);
        auto dive=decodeState(encodeState(f.world,f.explorer,f.simulation,f.session,2));require(dive->explorer.underwater(),"Underwater mode survives");near(dive->explorer.altitude(),f.explorer.altitude(),0,"Depth is not reset to shore");
    });
    test("state","Snow and animal reservoirs survive a save",[] {
        StateFixture f;const auto p=findSnowfield(f.world,f.explorer.up(),f.session.seconds);f.explorer.teleport(f.world,p);f.simulation.advance(f.world,p,0);f.simulation.water.snow.setExperiment(SnowExperiment::Blizzard);f.simulation.advance(f.world,p,10);
        const auto site=findDeerHabitat(f.world,f.world.findLand());require(f.simulation.fauna.focus(f.world,site),"Herd fixture");f.simulation.fauna.advance(f.world,site.home,1,f.world.surface(site.home)+1.75,true,false);
        const auto bytes=encodeState(f.world,f.explorer,f.simulation,f.session,3);const auto copy=decodeState(bytes);
        require(encodeState(copy->world,copy->explorer,copy->simulation,copy->session,3)==bytes,"Snow budgets, cache, drifting and animal decisions are retained");
        require(copy->simulation.water.snow.experiment()==SnowExperiment::Blizzard && !copy->simulation.fauna.herds().empty(),"Experiments and herd membership retained");
    });
    test("state","Disabled modules and nondefault seed are restored",[] {
        Config c;c.seed=119;World w(c);for(unsigned i=0;i<moduleCount;++i)w.modules().set(static_cast<Module>(i),false);
        Explorer e(w);Simulation s(w,e.up(),0);SessionState session;session.seconds=0;
        auto copy=decodeState(encodeState(w,e,s,session,4));require(copy->world.config().seed==119,"Seed retained");
        for(unsigned i=0;i<moduleCount;++i)require(!copy->world.modules().requested(static_cast<Module>(i)),"Disabled module retained");
        copy->simulation.advance(copy->world,copy->explorer.up(),10);
    });
    test("state","Truncated corrupted and incompatible files are rejected",[] {
        StateFixture f;const auto bytes=encodeState(f.world,f.explorer,f.simulation,f.session,5);
        for(int kind=0;kind<5;++kind) {auto bad=bytes;
            if(kind==0)bad.resize(10);
            if(kind==1)bad.back()^=std::byte{1};
            if(kind==2)bad[8]=std::byte{99};
            if(kind==3)bad[0]=std::byte{0};
            if(kind==4)bad.push_back(std::byte{0});
            bool failed=false;try {(void)decodeState(bad);}catch(const std::exception&) {failed=true;}require(failed,"Damaged state rejected");
        }
        near(f.explorer.heading(),0,1e-12,"Failed reads do not mutate live explorer");
    });
    test("state","Atomic replacement backups and recovery keep the previous good slot",[] {
        StateFixture f;const auto dir=std::filesystem::temp_directory_path()/("planet-state-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto path=slotPath(dir,0);saveState(path,f.world,f.explorer,f.simulation,f.session);
        f.explorer.look(.9,.2);saveState(path,f.world,f.explorer,f.simulation,f.session);
        auto current=loadState(path);near(current->explorer.heading(),.9,1e-12,"New snapshot committed");require(!current->recovered,"Primary valid");
        {std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<"interrupted/corrupted file";}
        auto previous=loadState(path);require(previous->recovered,"Recovered backup indicated");near(previous->explorer.heading(),0,1e-12,"Previous snapshot intact");
        require(readSaveMetadata(path).has_value(),"Slot lists recoverable backup");
        bool failed=false;try {saveState(path/"blocked",f.world,f.explorer,f.simulation,f.session);}catch(const std::exception&) {failed=true;}require(failed,"Invalid target rejected");
        near(loadState(path)->explorer.heading(),0,1e-12,"Failure preserves recoverable state");std::filesystem::remove_all(dir);
    });
    test("state","Six manual slots are isolated from quick and automatic slots",[] {
        std::vector<std::filesystem::path> slots;for(unsigned i=0;i<saveSlotCount;++i)slots.push_back(slotPath("saves",i));
        std::sort(slots.begin(),slots.end());require(std::adjacent_find(slots.begin(),slots.end())==slots.end(),"Unique slot paths");require(manualSlotCount==6 && slotPath("saves",quickSlot).filename()=="quick.pls" && slotPath("saves",autoSlot).filename()=="auto.pls","Separate manual quick and automatic slots");
        bool failed=false;try {(void)slotPath("saves",8);}catch(const std::invalid_argument&) {failed=true;}require(failed,"Invalid slot rejected");
    });
    test("state","Display controls default to identity and persist independently",[] {
        auto p=parsePreferences("version=1\nvolume=37\n");require(p.gamma==100 && p.brightness==100,"Old settings preserve original image");near(displayIntensity(.25,100,100),.25,0,"Identity");near(displayIntensity(.25,200,100),.5,1e-12,"Gamma lifts midtones");near(displayIntensity(.25,100,150),.375,1e-12,"Brightness scales exposure");
        p.gamma=175;p.brightness=130;const auto copy=parsePreferences(serializePreferences(p));require(copy.gamma==175 && copy.brightness==130 && copy.volume==37,"Persistent independent display and audio settings");
        for(const auto key:{"gamma=0","brightness=151","gamma=nan"}) {bool failed=false;try {(void)parsePreferences(std::string("version=1\n")+key);}catch(const std::invalid_argument&) {failed=true;}require(failed,"Invalid saved display control rejected");}
    });
    test("state","Default window is eighty percent and centered within monitor work area",[] {
        const auto a=defaultWindowLayout(0,0,1920,1040);require(a.width==1536 && a.height==832 && a.x==192 && a.y==104,"Taskbar excluded");
        const auto b=defaultWindowLayout(-1280,40,1280,984);require(b.width==1024 && b.height==787 && b.x==-1152 && b.y==138,"Negative monitor coordinates and reserved top edge");
        const auto c=defaultWindowLayout(0,0,1024,768);require(c.width==819 && c.height==614,"Small display retains eighty percent");
    });
}
