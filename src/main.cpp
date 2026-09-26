#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/dnn.hpp>

#include <windows.h>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace std;
using namespace cv;

// ------------------------------------------------------------
// Find the directory containing Tesseract.exe
// ------------------------------------------------------------

filesystem::path getExecutableDirectory()
{
    char buffer[MAX_PATH];

    GetModuleFileNameA(
        NULL,
        buffer,
        MAX_PATH
    );

    return filesystem::path(buffer).parent_path();
}

// ------------------------------------------------------------
// Face detection
// ------------------------------------------------------------

void detectAndDraw(
    Mat& img,
    Ptr<FaceDetectorYN> detector
//    CascadeClassifier& faceCascade,
//    CascadeClassifier& eyeCascade
)
{
    detector->setInputSize(img.size());
    //Mat gray;
    Mat faces;
    detector->detect(img, faces);

    //cvtColor(img, gray, COLOR_BGR2GRAY);
    //equalizeHist(gray, gray);
    //vector<Rect> faces;
    /*
    faceCascade.detectMultiScale(
        gray,
        faces,
        1.1,                // Scale factor
        3,                  // Minimum neighbors
        CASCADE_SCALE_IMAGE,
        Size(80, 80)        // Minimum face size
    );

    for (const Rect& face : faces)
    {
        // ----------------------------------------------------
        // Draw face rectangle
        // ----------------------------------------------------

        rectangle(
            img,
            face,
            Scalar(0, 255, 0),
            2
        );

        // ----------------------------------------------------
        // Calculate center of head
        // ----------------------------------------------------

        Point headCenter(
            face.x + face.width / 2,
            face.y + face.height / 2
        );

        circle(
            img,
            headCenter,
            5,
            Scalar(0, 0, 255),
            FILLED
        );

        // ----------------------------------------------------
        // Display head coordinates
        // ----------------------------------------------------

        string positionText =
            "X: " + to_string(headCenter.x) +
            " Y: " + to_string(headCenter.y);

        putText(
            img,
            positionText,
            Point(face.x, face.y - 10),
            FONT_HERSHEY_SIMPLEX,
            0.6,
            Scalar(0, 255, 0),
            2
        );

        // ----------------------------------------------------
        // Eye detection
        // ----------------------------------------------------

        if (!eyeCascade.empty())
        {
            Mat faceROI = gray(face);

            vector<Rect> eyes;

            eyeCascade.detectMultiScale(
                faceROI,
                eyes,
                1.1,
                3,
                CASCADE_SCALE_IMAGE,
                Size(20, 20)
            );

            for (const Rect& eye : eyes)
            {
                Point eyeCenter(
                    face.x + eye.x + eye.width / 2,
                    face.y + eye.y + eye.height / 2
                );

                int radius =
                    cvRound((eye.width + eye.height) * 0.25);

                circle(
                    img,
                    eyeCenter,
                    radius,
                    Scalar(255, 0, 0),
                    2
                );
            }
        }
    }
*/

    for (int i = 0; i < faces.rows; ++i){
        int x = cvRound(faces.at<float>(i, 0));
        int y = cvRound(faces.at<float>(i, 1));
        int width = cvRound(faces.at<float>(i, 2));
        int height = cvRound(faces.at<float>(i, 3));

        Rect face(
            x,
            y,
            width,
            height
        );
        rectangle(
            img,
            face,
            Scalar(0, 255, 0),
            2
        );
        Point headCenter(
            x + width / 2,
            y + height / 2
        );
        circle(
            img,
            headCenter,
            5,
            Scalar(0, 0, 255),
            FILLED
        );
        putText(
            img,
            "X: " + to_string(headCenter.x) + " Y: " + to_string(headCenter.y),
            Point(x, y - 10),
            FONT_HERSHEY_SIMPLEX,
            0.6,
            Scalar(0, 255, 0),
            2
        );
    }
    // Number of detected faces
    string faceCount =
        "Faces detected: " + to_string(faces.rows);

    putText(
        img,
        faceCount,
        Point(20, 30),
        FONT_HERSHEY_SIMPLEX,
        0.7,
        Scalar(255, 255, 255),
        2
    );
}

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------

int main()
{
    cout << "Starting Tesseract Head Tracker...\n";

    // --------------------------------------------------------
    // Find Haar cascade files
    // --------------------------------------------------------

    filesystem::path exeDirectory =
        getExecutableDirectory();
/*
    filesystem::path faceCascadePath =
        exeDirectory /
        "data" /
        "haarcascades" /
        "haarcascade_frontalface_default.xml";

    filesystem::path eyeCascadePath =
        exeDirectory /
        "data" /
        "haarcascades" /
        "haarcascade_eye_tree_eyeglasses.xml";
*/
filesystem::path modelPath =
    exeDirectory /
    ".." /
    ".." /
    ".." /
    ".." / 
    "custom" /
    "face_detection_yunet_2026may.onnx";


    //cout << "Executable directory:\n";
    //cout << exeDirectory << "\n\n";

    //cout << "Face cascade:\n";
    //cout << faceCascadePath << "\n\n";
    cout << "YuNet model:\n";
    cout << modelPath << "\n\n";
    // --------------------------------------------------------
    // Load classifiers
    // --------------------------------------------------------

    CascadeClassifier faceCascade;
    CascadeClassifier eyeCascade;

    if (!filesystem::exists(modelPath))
    {
        cerr << "ERROR: YuNet model file does not exist:\n";
        cerr << modelPath << "\n";
        cout << "Exists: " << filesystem::exists(modelPath) << "\n";
        return 1;
    }
    /*
    if (!faceCascade.load(faceCascadePath.string()))
    {
        cerr << "ERROR: Could not load face cascade:\n";
        cerr << faceCascadePath << "\n";

        return 1;
    }
    */
    cout << "Face cascade loaded.\n";
    /*
    if (!eyeCascade.load(eyeCascadePath.string()))
    {
        cerr << "WARNING: Could not load eye cascade:\n";
        cerr << eyeCascadePath << "\n";
        cerr << "Face tracking will continue without eyes.\n";
    }
    else
    {
        cout << "Eye cascade loaded.\n";
    }
    */

    // --------------------------------------------------------
    // Open webcam
    // --------------------------------------------------------


    Ptr<FaceDetectorYN> detector = FaceDetectorYN::create(
        modelPath.string(),
        "",
        Size(320, 320),
        0.9f,
        0.3f,
        500
    );

    if (detector.empty())
    {
        cerr << "ERROR: Could not create YuNet face detector.\n";
        return 1;
    }

    cout << "Opening webcam...\n";
    cout << "YuNet model loaded successfully.\n";


VideoCapture camera;
Mat frame;

// Try DirectShow first
cout << "Trying DirectShow...\n";

camera.open(0, CAP_DSHOW);

if (camera.isOpened())
{
    Sleep(1000);

    bool gotFrame = false;

    for (int i = 0; i < 30; ++i)
    {
        if (camera.grab() && camera.retrieve(frame))
        {
            if (!frame.empty())
            {
                gotFrame = true;
                break;
            }
        }

        Sleep(100);
    }

    if (!gotFrame)
    {
        cout << "DirectShow opened but could not get frames.\n";
        camera.release();
    }
}

// If DirectShow failed, try MSMF
if (!camera.isOpened())
{
    cout << "Trying Media Foundation...\n";

    camera.open(0, CAP_MSMF);

    if (camera.isOpened())
    {
        Sleep(1000);

        bool gotFrame = false;

        for (int i = 0; i < 30; ++i)
        {
            if (camera.grab() && camera.retrieve(frame))
            {
                if (!frame.empty())
                {
                    gotFrame = true;
                    break;
                }
            }

            Sleep(100);
        }

        if (!gotFrame)
        {
            cout << "MSMF opened but could not get frames.\n";
            camera.release();
        }
    }
}

if (!camera.isOpened())
{
    cerr << "ERROR: Could not initialize webcam.\n";
    return 1;
}

cout << "Camera opened successfully!\n";
cout << "Press ESC or Q to quit.\n";    
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

        // Mirror the camera like a normal webcam preview
        flip(frame, frame, 1);

        // Detect face + eyes
        detectAndDraw(
            frame,
            detector
        );

        // Show result
        imshow(
            "Tesseract Head Tracker",
            frame
        );

        char key =
            static_cast<char>(waitKey(1));

        if (
            key == 27 ||
            key == 'q' ||
            key == 'Q'
        )
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