// toMultiCanny: console version of onGenerateGray() from NLMCanny/MainWindow.cpp.
// Iterates an input directory, for each image computes gray (BGR->GRAY),
// Gaussian k=3, then a 255-level edge map using the same method as
// MainWindow::onGenerateGray() (useNms=false, NLM disabled).
//
// Usage: toMultiCanny <in_dir> <out_dir> [--invert]

#include "Canny.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
const double UPPER_DENSITY = 25.0;

static bool isImageExt(const std::string& extLower)
{
    return extLower == ".png" || extLower == ".jpg" || extLower == ".jpeg" ||
           extLower == ".bmp" || extLower == ".tif" || extLower == ".tiff";
}

static std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Gaussian identical to applyGaussKernel(src, mode=1) -> k=3.
static void gauss3(const cv::Mat& src, cv::Mat& dst)
{
    const int k = 3;
    const double sigma = 0.3 * ((k - 1) * 0.5 - 1) + 0.8;  // = 0.8
    cv::GaussianBlur(src, dst, cv::Size(k, k), sigma);
}

// pct -> value 1..255 mapping identical to valFn in onGenerateGray().
static uchar valFn(double pct)
{
    static const double logBlack = std::log10(UPPER_DENSITY);
    static const double logWhite = std::log10(0.1);
    if (pct >= UPPER_DENSITY) return 0;
    if (pct < 0.1)   pct = 0.1;
    double t = (std::log10(pct) - logBlack) / (logWhite - logBlack);
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    int value = 1 + static_cast<int>(std::round(t * 254.0));
    if (value < 1)   value = 1;
    if (value > 255) value = 255;
    return static_cast<uchar>(value);
}

// Full procedure from onGenerateGray() (useNms==false branch).
static void buildGrayMap(const cv::Mat& src8u, cv::Mat& grayResult)
{
    Canny mc;
    mc.prepare(src8u);

    grayResult = cv::Mat::zeros(src8u.size(), CV_8UC1);

    int zeroStreak = 0;
    cv::Mat e;
    for (int low = 1; low <= 255; ++low) {
        double high = low * 2.5;
        int nz = mc.apply(static_cast<double>(low), high, e);
        if (nz == 0) {
            if (++zeroStreak >= 3) break;
            continue;
        }
        zeroStreak = 0;

        double pct = 100.0 * static_cast<double>(nz) /
                              static_cast<double>(e.total());
        if (pct >= UPPER_DENSITY) continue;
        uchar value = valFn(pct);
        grayResult.setTo(value, e);
    }
}

struct TimeAccum {
    long long readMs = 0;
    long long cannyMs = 0;
    long long writeMs = 0;
};

static bool processOne(const fs::path& inPath, const fs::path& outPath, bool invert,
                       TimeAccum& acc)
{
    using clk = std::chrono::steady_clock;
    auto ms_since = [](clk::time_point a, clk::time_point b) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
    };

    auto tRead0 = clk::now();
    cv::Mat img = cv::imread(inPath.string(), cv::IMREAD_COLOR);
    auto tRead1 = clk::now();
    if (img.empty()) {
        std::cerr << "[skip] failed to read: " << inPath << "\n";
        return false;
    }

    auto tCvt0 = clk::now();
    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    auto tCvt1 = clk::now();

    auto tGauss0 = clk::now();
    cv::Mat blurred;
    gauss3(gray, blurred);
    auto tGauss1 = clk::now();

    auto tCanny0 = clk::now();
    cv::Mat result;
    buildGrayMap(blurred, result);
    auto tCanny1 = clk::now();

    auto tInv0 = clk::now();
    if (!invert) result = 255 - result;   // default: dark lines on white
    auto tInv1 = clk::now();

    const std::vector<int> pngParams = {
        cv::IMWRITE_PNG_COMPRESSION, 7,   // 0..9, lossless (deflate)
        cv::IMWRITE_PNG_STRATEGY, cv::IMWRITE_PNG_STRATEGY_DEFAULT
    };
    auto tWrite0 = clk::now();
    bool okWrite = cv::imwrite(outPath.string(), result, pngParams);
    auto tWrite1 = clk::now();
    if (!okWrite) {
        std::cerr << "[fail] write: " << outPath << "\n";
        return false;
    }

    auto total = ms_since(tRead0, tWrite1);
    acc.readMs  += ms_since(tRead0, tRead1);
    acc.cannyMs += ms_since(tCanny0, tCanny1);
    acc.writeMs += ms_since(tWrite0, tWrite1);
    std::cout << inPath.filename().string() << " -> " << outPath.filename().string()
              << "  [imread=" << ms_since(tRead0, tRead1)
              << "  cvtColor=" << ms_since(tCvt0, tCvt1)
              << "  gauss=" << ms_since(tGauss0, tGauss1)
              << "  canny(prep+256x)=" << ms_since(tCanny0, tCanny1)
              << "  invert=" << ms_since(tInv0, tInv1)
              << "  imwrite(png7)=" << ms_since(tWrite0, tWrite1)
              << "  total=" << total << " ms]\n";
    return true;
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Usage: " << (argc > 0 ? argv[0] : "toMultiCanny")
                  << " <in_dir> <out_dir> [--invert]\n"
                  << "  Default: dark lines on white background.\n"
                  << "  --invert : inverted (light lines on black).\n";
        return 1;
    }
    fs::path inDir  = argv[1];
    fs::path outDir = argv[2];
    bool invert = false;
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--invert") invert = true;
        else {
            std::cerr << "Unknown argument: " << a << "\n";
            return 1;
        }
    }

    if (!fs::is_directory(inDir)) {
        std::cerr << "in_dir is not a directory: " << inDir << "\n";
        return 1;
    }
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        std::cerr << "Failed to create out_dir: " << outDir << " (" << ec.message() << ")\n";
        return 1;
    }

    int ok = 0, fail = 0;
    TimeAccum acc;
    auto tAll0 = std::chrono::steady_clock::now();
    for (const auto& entry : fs::directory_iterator(inDir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = toLower(entry.path().extension().string());
        if (!isImageExt(ext)) continue;

        fs::path outPath = outDir / (entry.path().stem().string() + ".png");
        if (processOne(entry.path(), outPath, invert, acc)) ++ok;
        else ++fail;
    }
    auto tAll1 = std::chrono::steady_clock::now();
    double totalSec = std::chrono::duration<double>(tAll1 - tAll0).count();
    std::cout << "Done. OK: " << ok << ", errors: " << fail
              << "  total=" << totalSec << " s"
              << "  (imread=" << acc.readMs / 1000.0 << " s"
              << "  canny=" << acc.cannyMs / 1000.0 << " s"
              << "  imwrite=" << acc.writeMs / 1000.0 << " s)\n";
    return fail == 0 ? 0 : 2;
}