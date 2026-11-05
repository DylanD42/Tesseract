#include "headtracking.h"
#include "settings.h"
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace tesseract {
namespace {
using namespace settings;
struct Shared {
    std::mutex mutex;
    Snapshot snapshot;
    std::atomic<bool> quit{false};
};
bool openCamera(int index,cv::VideoCapture& camera,cv::Mat& frame) {
    for (int backend:{static_cast<int>(cv::CAP_DSHOW),static_cast<int>(cv::CAP_MSMF),static_cast<int>(cv::CAP_ANY)}) {
        camera.release();
        if (!camera.open(index,backend)) continue;
        // Keep the driver's native format; virtual cameras can use fixed resolutions.
        for (int attempt=0;attempt<30;++attempt) {
            if (camera.read(frame)&&!frame.empty()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    camera.release();
    return false;
}
void detectFace(const cv::Mat& frame,cv::Ptr<cv::FaceDetectorYN>& detector,Snapshot& snapshot) {
    snapshot.faceCenterValid=false;
    const double scale=std::min(1.0,static_cast<double>(kDetectWidth)/frame.cols);
    cv::Mat small,detections;
    cv::resize(frame,small,cv::Size(),scale,scale,cv::INTER_AREA);
    detector->setInputSize(small.size());
    detector->detect(small,detections);
    snapshot.faceCount=detections.rows;
    snapshot.faceScore=0;
    if (detections.empty()) {snapshot.status="No face detected";return;}
    int best=0;
    for (int row=1;row<detections.rows;++row)
        if (detections.at<float>(row,2)*detections.at<float>(row,3)>
            detections.at<float>(best,2)*detections.at<float>(best,3)) best=row;
    const float* face=detections.ptr<float>(best);
    const double x=face[0]/scale,y=face[1]/scale,w=face[2]/scale,h=face[3]/scale;
    snapshot.faceCenter={(x+w/2)/frame.cols,(y+h/2)/frame.rows};
    snapshot.faceCenterValid=true;
    snapshot.faceScore=face[14];
    snapshot.status="Face-center tracking";
    ++snapshot.faceSampleId;
}
void trackingWorker(cv::VideoCapture& capture,cv::Ptr<cv::FaceDetectorYN>& detector,Shared& shared) {
    Snapshot snapshot;
    cv::Mat frame;
    while (!shared.quit) {
        if (capture.read(frame)&&!frame.empty()) {
            detectFace(frame,detector,snapshot);
        } else {
            snapshot.faceCenterValid=false;
            snapshot.status="Camera read failed";
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        {std::lock_guard<std::mutex> lock(shared.mutex);shared.snapshot=snapshot;}
    }
}
}
struct HeadTracking::Impl {
    cv::VideoCapture capture;
    cv::Ptr<cv::FaceDetectorYN> detector;
    Shared shared;
    std::thread worker;
    Impl(const std::filesystem::path& directory,int index) {
        if (index<0||index>9) throw std::invalid_argument("Camera index must be between 0 and 9");
        namespace fs=std::filesystem;
        const char* filename="face_detection_yunet_2023mar.onnx";
        auto model=directory/"custom"/filename;
        if (!fs::exists(model)) model=fs::current_path()/"custom"/filename;
        if (!fs::exists(model)) throw std::runtime_error("Missing YuNet model in custom beside the executable or working directory");
        detector=cv::FaceDetectorYN::create(model.string(),"",cv::Size(kDetectWidth,kDetectWidth*3/4),
                    kYuNetScoreThreshold,kYuNetNmsThreshold,kYuNetTopK);
        if (detector.empty()) throw std::runtime_error("YuNet model failed to load");
        cv::Mat frame;
        bool opened=false;
        opened=openCamera(index,capture,frame);
        if (!opened) throw std::runtime_error("Could not open camera. Start virtual camera output or select another index.");
        std::cout << "Using camera " << index << " via " << capture.getBackendName()
                  << " (" << frame.cols << 'x' << frame.rows << ")\n";
        worker=std::thread([this] {
            try {trackingWorker(capture,detector,shared);}
            catch (const std::exception& error) {
                {std::lock_guard<std::mutex> lock(shared.mutex);
                 shared.snapshot.faceCenterValid=false;shared.snapshot.status=error.what();}
                std::cerr << "Tracking stopped: " << error.what() << '\n';
                shared.quit=true;
            }
        });
    }
    ~Impl() {
        shared.quit=true;
        if (worker.joinable()) worker.join();
        capture.release();
    }
};
HeadTracking::HeadTracking(const std::filesystem::path& directory,int index):impl_(std::make_unique<Impl>(directory,index)) {}
HeadTracking::~HeadTracking()=default;
Snapshot HeadTracking::snapshot() const {
    std::lock_guard<std::mutex> lock(impl_->shared.mutex);
    return impl_->shared.snapshot;
}
bool HeadTracking::stopRequested() const {return impl_->shared.quit;}
}
