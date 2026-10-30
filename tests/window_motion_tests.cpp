#include "window_motion.h"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace tesseract;
namespace {
void require(bool condition,const char* message) {if (!condition) throw std::runtime_error(message);}
void near(double a,double b,const char* message) {require(std::abs(a-b)<1e-7,message);}
std::array<double,3> settle(WindowMotion& motion,const WindowFeel& feel,std::array<double,2> face) {
    std::array<double,3> result{};
    for (std::uint64_t i=1;i<=300;++i) {
        if (i%2) motion.observe(face,(i+1)/2);
        result=motion.update(1.0/60,30,feel);
    }
    return result;
}
}
int main() {
    const auto path=std::filesystem::current_path()/"window_feel_test.tmp";
    try {
        WindowFeel feel;
        WindowMotion center;
        const auto centered=settle(center,feel,{0.5,0.5});
        near(centered[0],0,"Centered face must not move camera X");
        near(centered[1],0,"Centered face must not move camera Y");
        near(centered[2],50,"Default camera depth must remain fixed relative to screen width");
        WindowMotion moved;
        const auto offset=settle(moved,feel,{0.7,0.3});
        require(offset[0]<0&&offset[1]>0,"Face axes must map into display coordinates");
        near(offset[2],centered[2],"Changing face location must never pump camera depth");
        near(offset[0],-1.8,"Default lateral sensitivity must match the bounded window mapping");
        near(offset[1],1.2,"Vertical movement must have the intended lower gain");
        const auto held=moved.update(0.1,30,feel);
        moved.observe({0,1},150); // Same sample ID as the final accepted observation.
        const auto duplicate=moved.update(0.1,30,feel);
        near(held[0],duplicate[0],"Repeated snapshots must not be treated as new camera observations");
        moved.recenter();
        std::array<double,3> neutral{};
        for (int i=0;i<150;++i) neutral=moved.update(1.0/60,30,feel);
        near(neutral[0],0,"Recenter must remove the current lateral offset");
        near(neutral[1],0,"Recenter must remove the current vertical offset");
        WindowMotion fast,slow;
        fast.setMouse({0.8,0.2});slow.setMouse({0.8,0.2});
        std::array<double,3> at120{},at60{};
        for (int i=0;i<20;++i) at120=fast.update(1.0/120,30,feel);
        for (int i=0;i<10;++i) at60=slow.update(1.0/60,30,feel);
        near(at120[0],at60[0],"Render smoothing must be independent of frame rate");
        near(at120[1],at60[1],"Vertical smoothing must be independent of frame rate");
        WindowMotion extreme;
        feel.sensitivity=3;
        const auto limited=settle(extreme,feel,{-5,5});
        require(std::abs(limited[0])<=30*0.15&&std::abs(limited[1])<=30*0.10,
                "Extreme tracking input must stay inside a modest camera motion range");
        feel.smoothing=0.9;feel.roomDepth=10;feel.gridDensity=20;
        saveWindowFeel(path,feel);
        const auto loaded=loadWindowFeel(path);
        near(loaded.smoothing,0.9,"Smoothing must survive save/load");
        require(loaded.gridDensity==20,"Room settings must survive save/load");
        bool rejected=false;
        try {WindowFeel bad;bad.smoothing=1;validateWindowFeel(bad);}
        catch (const std::invalid_argument&) {rejected=true;}
        require(rejected,"Frozen smoothing must be rejected");
        std::filesystem::remove(path);
        std::cout << "Window motion tests passed: fixed depth, bounded motion, smoothing, recenter and settings\n";
        return 0;
    } catch (const std::exception& e) {
        std::filesystem::remove(path);std::cerr << e.what() << '\n';return 1;
    }
}
