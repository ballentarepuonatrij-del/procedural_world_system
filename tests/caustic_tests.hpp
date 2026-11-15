void registerCausticTests() {
    test("caustics","Snell refraction preserves angles and total internal reflection",[] {
        const auto ray=refractLight({.6,0,-.8},{0,0,1},1,1.333);
        near(ray.x,.6/1.333,1e-12,"Snell sine ratio");near(length(ray),1,1e-12,"Unit transmitted ray");require(ray.z<0,"Ray enters water");
        near(length(refractLight({.9,0,-std::sqrt(.19)},{0,0,1},1.333,1)),0,0,"Beyond critical angle there is total internal reflection");
    });
    test("caustics","Flat water is uniform ripples focus light and energy is conserved",[] {
        CausticMap map;const auto sun=normalized(Vec3{.3,.2,1});map.update(sun,12,0,false,42);
        for(auto v:map.intensity()) near(v,1,2e-6,"Flat receiver receives uniform flux");
        map.update(sun,12,1,false,42);double sum=0;float peak=0;for(auto v:map.intensity()) {require(std::isfinite(v) && v>=0,"Finite nonnegative flux");sum+=v;peak=std::max(peak,v);}
        near(sum/(CausticMap::size*CausticMap::size),1,1e-6,"Splatting conserves incident energy");require(peak>1.5,"Ripple curvature focuses rays");
        const auto before=map.intensity();map.update(sun,12,1,false,42);require(before==map.intensity(),"Paused time is deterministic");map.update(sun,12.4,1,false,42);require(before!=map.intensity(),"Water pattern animates");
        map.update(sun,12,1,true,42);const auto frozen=map.intensity();map.update(sun,200,1,true,42);require(frozen==map.intensity(),"Frozen optical surface stays stationary");
    });
    test("caustics","Night dry ground deep water mud and opaque ice suppress caustics",[] {
        CausticMap map;map.update({0,0,-1},0,1,false,1);for(auto value:map.intensity()) near(value,0,0,"No sunlight at night");
        near(causticTransmission(-1,1,0,1),0,0,"Above water");near(causticTransmission(30,1,0,1),0,0,"Deep water");near(causticTransmission(2,0,0,1),0,0,"Opaque lid");near(causticTransmission(2,1,0,0),0,0,"Night");
        require(causticTransmission(2,1,20,1)<causticTransmission(2,1,0,1)*.01,"Turbidity attenuates submerged light");
    });
    test("caustics","Optical mask follows water height phase change and module switches",[] {
        World w;const auto p=w.landscape()->patch(w.findLandmark(Landform::Pond,w.findLand()).position)->anchor;Simulation s(w,p,79*86400);
        const auto open=waterOpticsMap(w,s.water);require(open.wet && !open.hasIce,"Temperate pond is liquid");
        s.water.setIceExperiment(IceExperiment::Freeze);s.water.previewIce(w,s.weather,72*3600);const auto frozen=waterOpticsMap(w,s.water);
        require(frozen.hasIce && frozen.pixels!=open.pixels,"Phase change invalidates receiver optics");
        const std::size_t index=(32*64+32)*4;const auto& c=s.water.grid().cells[32*64+32];
        near(frozen.levelBase+frozen.pixels[index]/65535.*frozen.levelRange,c.reservoirLevel-c.ice.frozen,frozen.levelRange/65535.,"Map uses the underside of floating ice");
        require(frozen.pixels[index+2]<open.pixels[index+2],"Ice absorbs transmitted light");
        w.modules().set(Module::Ice,false);s.water.synchronize(w,p);require(!waterOpticsMap(w,s.water).hasIce,"No stale lid after module toggle");
        w.modules().set(Module::InlandWaters,false);w.modules().set(Module::Rivers,false);w.modules().set(Module::Ocean,false);w.modules().set(Module::Hydrology,false);s.water.synchronize(w,p);
        require(!waterOpticsMap(w,s.water).wet,"No optics from disabled reservoirs");
    });
}
