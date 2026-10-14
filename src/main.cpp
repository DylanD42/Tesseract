#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <dlib/image_processing.h>
#include <dlib/image_processing/frontal_face_detector.h>
#include <dlib/opencv.h>

#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>

using namespace std;
using namespace cv;

constexpr int initialPoseFrameCount = 30;

struct FacePositionState
{
    Vec3d initialPosition = Vec3d(0.0, 0.0, 0.0);
    Vec3d currentPosition = Vec3d(0.0, 0.0, 0.0);
    Vec3d relativePosition = Vec3d(0.0, 0.0, 0.0);
    Vec3d initialPositionSum = Vec3d(0.0, 0.0, 0.0);
    int initialPositionSamples = 0;

    void reset()
    {
        initialPosition = Vec3d(0.0, 0.0, 0.0);
        relativePosition = Vec3d(0.0, 0.0, 0.0);
        initialPositionSum = Vec3d(0.0, 0.0, 0.0);
        initialPositionSamples = 0;
    }
};

// ------------------------------------------------------------
// Find the directory containing Tesseract.exe
// ------------------------------------------------------------

filesystem::path getExecutableDirectory()
{
    char buffer[MAX_PATH];

    GetModuleFileNameA(
        NULL,
        buffer,
        MAX_PATH);

    return filesystem::path(buffer).parent_path();
}

bool openCamera(int index, VideoCapture &camera, Mat &frame)
{
    const int backends[] = {CAP_DSHOW, CAP_MSMF};

    for (int backend : backends)
    {
        camera.release();

        if (!camera.open(index, backend))
        {
            continue;
        }

        for (int attempt = 0; attempt < 10; ++attempt)
        {
            if (camera.read(frame) && !frame.empty())
            {
                return true;
            }

            Sleep(100);
        }
    }

    camera.release();
    return false;
}

vector<dlib::rectangle> findFaces(
    const Mat &frame,
    dlib::frontal_face_detector &faceDetector)
{
    dlib::cv_image<dlib::bgr_pixel> dlibFrame(frame);
    return faceDetector(dlibFrame);
}

Mat createApproximateCameraMatrix(const Size &imageSize)
{
    constexpr double assumedHorizontalFovDegrees = 70.0;
    const double horizontalFovRadians = assumedHorizontalFovDegrees * CV_PI / 180.0;
    const double focalLength = imageSize.width / (2.0 * tan(horizontalFovRadians / 2.0));

    return (Mat_<double>(3, 3) << focalLength, 0.0, imageSize.width / 2.0,
            0.0, focalLength, imageSize.height / 2.0,
            0.0, 0.0, 1.0);
}

void detectAndDrawDlib(
    Mat &image,
    dlib::frontal_face_detector &faceDetector,
    const dlib::shape_predictor &landmarkPredictor,
    const Mat &cameraMatrix,
    const Mat &distortionCoefficients,
    FacePositionState &position)
{
    dlib::cv_image<dlib::bgr_pixel> dlibImage(image);
    const auto faces = faceDetector(dlibImage);
    const vector<Point3d> modelPoints = {
        Point3d(0.0, 0.0, 0.0),
        Point3d(0.0, -63.0, -12.0),
        Point3d(-34.0, 32.0, -12.0),
        Point3d(34.0, 32.0, -12.0),
        Point3d(-28.0, -28.0, -8.0),
        Point3d(28.0, -28.0, -8.0)};
    const int poseLandmarkIndexes[] = {30, 8, 36, 45, 48, 54};

    if (!faces.empty())
    {
        const auto face = *max_element(
            faces.begin(),
            faces.end(),
            [](const dlib::rectangle &left, const dlib::rectangle &right)
            {
                return left.area() < right.area();
            });
        const auto landmarks = landmarkPredictor(dlibImage, face);
        rectangle(
            image,
            Point(static_cast<int>(face.left()), static_cast<int>(face.top())),
            Point(static_cast<int>(face.right()), static_cast<int>(face.bottom())),
            Scalar(0, 255, 0),
            2);

        for (unsigned long pointIndex = 0; pointIndex < landmarks.num_parts(); ++pointIndex)
        {
            const auto &point = landmarks.part(pointIndex);
            circle(
                image,
                Point(static_cast<int>(point.x()), static_cast<int>(point.y())),
                1,
                Scalar(0, 255, 255),
                FILLED);
        }

        vector<Point2d> imagePoints;
        for (int pointIndex : poseLandmarkIndexes)
        {
            const auto &point = landmarks.part(pointIndex);
            imagePoints.emplace_back(point.x(), point.y());
        }

        Mat rotationVector;
        Mat translationVector;
        if (solvePnP(
                modelPoints,
                imagePoints,
                cameraMatrix,
                distortionCoefficients,
                rotationVector,
                translationVector,
                false,
                SOLVEPNP_ITERATIVE))
        {
            position.currentPosition = Vec3d(
                translationVector.at<double>(0),
                translationVector.at<double>(1),
                translationVector.at<double>(2));

            if (position.initialPositionSamples < initialPoseFrameCount)
            {
                position.initialPositionSum += position.currentPosition;
                ++position.initialPositionSamples;
                if (position.initialPositionSamples == initialPoseFrameCount)
                {
                    position.initialPosition =
                        position.initialPositionSum / initialPoseFrameCount;
                }
            }
            else
            {
                position.relativePosition =
                    position.currentPosition - position.initialPosition;
            }

            string positionText;
            if (position.initialPositionSamples < initialPoseFrameCount)
            {
                positionText = "Setting reference: hold still " +
                               to_string(position.initialPositionSamples) + "/" +
                               to_string(initialPoseFrameCount);
            }
            else
            {
                ostringstream positionTextStream;
                positionTextStream << fixed << setprecision(1)
                                   << "dX: " << position.relativePosition[0] << " mm  "
                                   << "dY: " << position.relativePosition[1] << " mm  "
                                   << "dZ: " << position.relativePosition[2] << " mm  "
                                   << "Z: " << position.currentPosition[2] << " mm";
                positionText = positionTextStream.str();
            }

            putText(
                image,
                positionText,
                Point(static_cast<int>(face.left()), static_cast<int>(face.top()) - 10),
                FONT_HERSHEY_SIMPLEX,
                0.55,
                Scalar(0, 255, 0),
                2);
        }
    }

    putText(
        image,
        "Faces detected: " + to_string(faces.size()),
        Point(20, 30),
        FONT_HERSHEY_SIMPLEX,
        0.7,
        Scalar(255, 255, 255),
        2);
    putText(
        image,
        position.initialPositionSamples < initialPoseFrameCount
            ? "Hold still to set starting position"
            : "Relative movement | R: reset starting position",
        Point(20, 58),
        FONT_HERSHEY_SIMPLEX,
        0.6,
        Scalar(255, 255, 255),
        2);
}

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------

int main()
{
    cout << "Starting Tesseract Head Tracker...\n";

    // --------------------------------------------------------
    // Resolve runtime files
    // --------------------------------------------------------

    filesystem::path exeDirectory =
        getExecutableDirectory();
    filesystem::path landmarkModelPath =
        exeDirectory /
        ".." /
        ".." /
        ".." /
        ".." /
        "custom" /
        "shape_predictor_68_face_landmarks.dat";

    cout << "Dlib landmark model:\n"
         << landmarkModelPath << "\n\n";
    if (!filesystem::exists(landmarkModelPath))
    {
        cerr << "ERROR: Dlib 68-point predictor model was not found:\n"
             << landmarkModelPath << "\n"
             << "Place shape_predictor_68_face_landmarks.dat in the repository's custom folder.\n";
        return 1;
    }

    dlib::frontal_face_detector faceDetector = dlib::get_frontal_face_detector();
    dlib::shape_predictor landmarkPredictor;
    try
    {
        dlib::deserialize(landmarkModelPath.string()) >> landmarkPredictor;
    }
    catch (const exception &error)
    {
        cerr << "ERROR: Could not load the dlib landmark model: " << error.what() << "\n";
        return 1;
    }

    // --------------------------------------------------------
    // Open webcam
    // --------------------------------------------------------

    cout << "Opening webcam...\n";
    cout << "Dlib face detector and landmark model loaded successfully.\n";

    VideoCapture camera;
    Mat frame;
    int cameraIndex = 0;
    bool cameraSelected = false;
    bool quitRequested = false;
    cout << "Searching camera indexes for a face. Show your face to the camera; press Q or Esc to cancel.\n";

    while (!cameraSelected && !quitRequested)
    {
        for (int candidateIndex = 0; candidateIndex < 10 && !cameraSelected && !quitRequested; ++candidateIndex)
        {
            cout << "Checking camera index " << candidateIndex << "...\n";
            if (!openCamera(candidateIndex, camera, frame))
            {
                continue;
            }

            int consecutiveFaceFrames = 0;
            bool declinedCamera = false;
            for (int attempt = 0; attempt < 30; ++attempt)
            {
                if (!camera.read(frame) || frame.empty())
                {
                    break;
                }

                const auto faces = findFaces(frame, faceDetector);
                consecutiveFaceFrames = faces.empty() ? 0 : consecutiveFaceFrames + 1;

                Mat preview = frame.clone();
                putText(
                    preview,
                    "Checking camera " + to_string(candidateIndex) + " for a face | Q: quit",
                    Point(20, 30),
                    FONT_HERSHEY_SIMPLEX,
                    0.6,
                    Scalar(0, 255, 255),
                    2);
                imshow("Searching for webcam", preview);

                char key = static_cast<char>(waitKey(1));
                if (key == 'q' || key == 'Q' || key == 27)
                {
                    quitRequested = true;
                    break;
                }

                if (consecutiveFaceFrames >= 3)
                {
                    cout << "Face found on camera index " << candidateIndex
                         << ". Press Enter to use it, N to keep cycling, or Q to quit.\n";

                    while (true)
                    {
                        if (!camera.read(frame) || frame.empty())
                        {
                            declinedCamera = true;
                            break;
                        }

                        Mat confirmation = frame.clone();
                        putText(
                            confirmation,
                            "Face found on camera " + to_string(candidateIndex) +
                                " | Enter: use | N: next | Q: quit",
                            Point(20, 30),
                            FONT_HERSHEY_SIMPLEX,
                            0.6,
                            Scalar(0, 255, 255),
                            2);
                        imshow("Searching for webcam", confirmation);

                        char confirmationKey = static_cast<char>(waitKey(1));
                        if (confirmationKey == 13 || confirmationKey == 10)
                        {
                            cameraSelected = true;
                            cameraIndex = candidateIndex;
                            cout << "Selected camera index " << cameraIndex << ".\n";
                            break;
                        }
                        if (confirmationKey == 'n' || confirmationKey == 'N')
                        {
                            declinedCamera = true;
                            break;
                        }
                        if (confirmationKey == 'q' || confirmationKey == 'Q' || confirmationKey == 27)
                        {
                            quitRequested = true;
                            break;
                        }
                    }

                    break;
                }
            }

            if (!cameraSelected)
            {
                camera.release();
            }

            if (declinedCamera)
            {
                cout << "Continuing camera scan.\n";
            }
        }

        if (!cameraSelected && !quitRequested)
        {
            cout << "No face found; scanning camera indexes again.\n";
            Sleep(500);
        }
    }

    destroyWindow("Searching for webcam");
    if (quitRequested)
    {
        camera.release();
        destroyAllWindows();
        return 0;
    }

    Mat cameraMatrix = createApproximateCameraMatrix(frame.size());
    Mat distortionCoefficients = Mat::zeros(5, 1, CV_64F);

    cout << "Selected camera index " << cameraIndex << ".\n";
    cout << "Using an approximate 70-degree horizontal field of view.\n";
    cout << "Hold still while the starting position is averaged. Press R to recenter, ESC or Q to quit.\n";
    FacePositionState facePosition;
    // --------------------------------------------------------
    // Camera loop
    // --------------------------------------------------------

    while (true)
    {
        if (!camera.read(frame))
        {
            cerr << "ERROR: Could not read camera frame.\n";
            break;
        }

        if (frame.empty())
        {
            cerr << "ERROR: Empty camera frame.\n";
            break;
        }

        // Detect face + eyes
        detectAndDrawDlib(
            frame,
            faceDetector,
            landmarkPredictor,
            cameraMatrix,
            distortionCoefficients,
            facePosition);

        // Show result
        imshow(
            "Tesseract Head Tracker",
            frame);

        char key =
            static_cast<char>(waitKey(1));

        if (key == 'r' || key == 'R')
        {
            facePosition.reset();
            cout << "Hold still to set a new starting position.\n";
        }

        if (
            key == 27 ||
            key == 'q' ||
            key == 'Q')
        {
            break;
        }
    }

    // --------------------------------------------------------
    // Cleanup
    // --------------------------------------------------------

    camera.release();
    destroyAllWindows();

    cout << "\nTesseract stopped.\n";

    return 0;
}