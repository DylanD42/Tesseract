#include "render/scene_textures.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <vector>

namespace tess {
namespace {

Texture2D UploadTexture(const cv::Mat& bgr) {
    cv::Mat rgba;
    cv::cvtColor(bgr, rgba, cv::COLOR_BGR2RGBA);
    Image im{rgba.data, rgba.cols, rgba.rows, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Texture2D t = LoadTextureFromImage(im);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_ANISOTROPIC_16X);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}

// Tileable noise: blur a 3x3 repeat of the pattern and keep the middle, so repeated textures have no seams.
cv::Mat Noise(int size, double sigma, double blurX, double blurY, unsigned seed) {
    cv::Mat n(size, size, CV_32F);
    cv::RNG rng(seed);
    rng.fill(n, cv::RNG::NORMAL, 0.0, 1.0);
    cv::Mat big;
    cv::repeat(n, 3, 3, big);
    cv::GaussianBlur(big, big, cv::Size(0, 0), blurX, blurY);
    n = big(cv::Rect(size, size, size, size)).clone();
    cv::normalize(n, n, -sigma, sigma, cv::NORM_MINMAX);
    return n;
}

cv::Mat AddNoise(const cv::Mat& bgr, const cv::Mat& n) {
    cv::Mat f; bgr.convertTo(f, CV_32FC3);
    std::vector<cv::Mat> ch; cv::split(f, ch);
    for (auto& c : ch) c += n;
    cv::merge(ch, f);
    cv::Mat out; f.convertTo(out, CV_8UC3);
    return out;
}

} // namespace

void SceneTextures::load() {
    const int S = 512;
    {   // grass: soft two-tone checker (mowed lawn stripes) + noise
        cv::Mat m(S, S, CV_8UC3);
        for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) {
            const bool a = ((x / 128) + (y / 128)) % 2 == 0;
            m.at<cv::Vec3b>(y, x) = a ? cv::Vec3b(60, 150, 80) : cv::Vec3b(52, 132, 70);
        }
        grass = UploadTexture(AddNoise(m, Noise(S, 22, 1.2, 1.2, 1)));
    }
    {   // stone paving: gray slabs with dark grout
        cv::Mat m(S, S, CV_8UC3, cv::Scalar(150, 152, 155));
        cv::RNG rng(5);
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
            const int v = rng.uniform(-18, 18);
            cv::rectangle(m, cv::Rect(c * 128 + 4, r * 128 + 4, 120, 120), cv::Scalar(165 + v, 168 + v, 170 + v), cv::FILLED);
        }
        stone = UploadTexture(AddNoise(m, Noise(S, 14, 1.0, 1.0, 2)));
    }
    {   // wood planks: 8 planks with staggered seams and long grain
        cv::Mat m(S, S, CV_8UC3);
        cv::RNG rng(9);
        for (int p = 0; p < 8; ++p) {
            const int v = rng.uniform(-22, 22);
            m(cv::Rect(0, p * 64, S, 64)).setTo(cv::Scalar(60 + v / 2, 105 + v, 150 + v));
            cv::line(m, cv::Point(0, p * 64), cv::Point(S, p * 64), cv::Scalar(35, 55, 80), 2);
            const int seam = (p * 197) % S;
            cv::line(m, cv::Point(seam, p * 64), cv::Point(seam, p * 64 + 63), cv::Scalar(35, 55, 80), 2);
        }
        wood = UploadTexture(AddNoise(m, Noise(S, 18, 18.0, 0.8, 3)));
    }
    {   // rug: deep red with gold border and a central medallion
        cv::Mat m(S, S, CV_8UC3, cv::Scalar(40, 30, 140));
        cv::rectangle(m, cv::Rect(16, 16, S - 32, S - 32), cv::Scalar(60, 170, 210), 18);
        cv::rectangle(m, cv::Rect(52, 52, S - 104, S - 104), cv::Scalar(90, 60, 30), 6);
        cv::ellipse(m, cv::Point(S / 2, S / 2), cv::Size(120, 80), 0, 0, 360, cv::Scalar(60, 170, 210), 10);
        cv::ellipse(m, cv::Point(S / 2, S / 2), cv::Size(60, 40), 0, 0, 360, cv::Scalar(90, 60, 30), cv::FILLED);
        rug = UploadTexture(AddNoise(m, Noise(S, 10, 0.8, 0.8, 4)));
    }
    {   // checker tiles (classic black/white floor)
        cv::Mat m(S, S, CV_8UC3);
        for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x)
            m.at<cv::Vec3b>(y, x) = ((x / 256) + (y / 256)) % 2 == 0 ? cv::Vec3b(225, 228, 230) : cv::Vec3b(45, 45, 50);
        tiles = UploadTexture(AddNoise(m, Noise(S, 8, 1.0, 1.0, 6)));
    }
}

void SceneTextures::unload() {
    for (Texture2D* t : {&grass, &stone, &wood, &rug, &tiles}) if (t->id) { UnloadTexture(*t); t->id = 0; }
}

} // namespace tess
