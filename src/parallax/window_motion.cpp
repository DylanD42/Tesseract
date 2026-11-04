#include "window_motion.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace tesseract {
void validateWindowFeel(const WindowFeel& s) {
    if (!std::isfinite(s.sensitivity)||s.sensitivity<0.2||s.sensitivity>3||
        !std::isfinite(s.smoothing)||s.smoothing<0.3||s.smoothing>0.95||
        !std::isfinite(s.roomDepth)||s.roomDepth<2||s.roomDepth>12||
        !std::isfinite(s.gridOpacity)||s.gridOpacity<0.1||s.gridOpacity>0.9||
        s.gridDensity<4||s.gridDensity>24)
        throw std::invalid_argument("Invalid window motion or room settings");
}
void saveWindowFeel(const std::filesystem::path& path,const WindowFeel& s) {
    validateWindowFeel(s);
    std::ofstream out(path);
    out << "TESSERACT_WINDOW_FEEL 1\n" << s.sensitivity << ' ' << s.smoothing << ' '
        << s.roomDepth << ' ' << s.gridOpacity << ' ' << s.gridDensity << '\n';
    out.flush();
    if (!out) throw std::runtime_error("Cannot save view settings beside the executable");
}
WindowFeel loadWindowFeel(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::string magic;int version=0;WindowFeel s;
    if (!(in>>magic>>version>>s.sensitivity>>s.smoothing>>s.roomDepth>>s.gridOpacity>>s.gridDensity)||
        magic!="TESSERACT_WINDOW_FEEL"||version!=1)
        throw std::runtime_error("Invalid window view settings file");
    validateWindowFeel(s);
    return s;
}
void WindowMotion::observe(std::array<double,2> center,std::uint64_t sampleId) {
    if (!std::isfinite(center[0])||!std::isfinite(center[1])||sampleId==lastSample_) return;
    lastSample_=sampleId;
    latest_={std::clamp(center[0],0.0,1.0),std::clamp(center[1],0.0,1.0)};
    mouse_=false;pendingSample_=true;
}
void WindowMotion::setMouse(std::array<double,2> normalizedMouse) {
    latest_={std::clamp(normalizedMouse[0],0.0,1.0),std::clamp(normalizedMouse[1],0.0,1.0)};
    mouse_=true;
}
void WindowMotion::recenter() {neutral_=latest_;detected_={};pendingSample_=false;}
std::array<double,3> WindowMotion::update(double dt,double width,const WindowFeel& settings) {
    if (!std::isfinite(dt)||dt<0||!std::isfinite(width)||width<=0)
        throw std::invalid_argument("Invalid render interval or window width");
    dt=std::min(dt,0.2);
    elapsedSinceSample_+=dt;
    if (mouse_) {
        detected_={(latest_[0]-0.5)*0.2,(0.5-latest_[1])*(2.0/15)};
    } else if (pendingSample_) {
        // Detection-stage smoothing, time corrected around a 30 Hz camera.
        const double blend=1-std::pow(0.6,std::min(elapsedSinceSample_,0.2)*30);
        detected_[0]+=blend*(-(latest_[0]-neutral_[0])*0.2-detected_[0]);
        detected_[1]+=blend*(-(latest_[1]-neutral_[1])*(2.0/15)-detected_[1]);
        elapsedSinceSample_=0;pendingSample_=false;
    }
    // Normalize UI smoothing to a 60 Hz equivalent so high-refresh monitors
    // feel the same instead of changing the amount of filtering.
    const double blend=1-std::pow(settings.smoothing,dt*60);
    for (int i=0;i<2;++i) smoothed_[i]+=blend*(detected_[i]-smoothed_[i]);
    const double x=std::clamp(smoothed_[0]*settings.sensitivity,-0.15,0.15);
    const double y=std::clamp(smoothed_[1]*settings.sensitivity,-0.10,0.10);
    return {x*width,y*width,width*(5.0/3)};
}
}
