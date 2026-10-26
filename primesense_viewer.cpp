#define NOMINMAX
#include <Windows.h>
#include <OpenNI.h>
#include <PS1080.h>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

enum class ViewMode {
    Depth,
    Infrared,
    Color
};

const int kToolbarHeight = 72;

struct ViewerControls {
    int clickedMode = -1;
    int minimumDepth = 500;
    int maximumDepth = 5000;
    bool draggingMinimum = false;
    bool draggingMaximum = false;
    int canvasWidth = 640;
    int previewHeight = 0;
    bool depthSlidersVisible = false;
};

const char* modeName(ViewMode mode) {
    switch (mode) {
    case ViewMode::Depth:
        return "Depth";
    case ViewMode::Infrared:
        return "Infrared";
    case ViewMode::Color:
        return "Color";
    }
    return "Unknown";
}

openni::SensorType sensorType(ViewMode mode) {
    switch (mode) {
    case ViewMode::Depth:
        return openni::SENSOR_DEPTH;
    case ViewMode::Infrared:
        return openni::SENSOR_IR;
    case ViewMode::Color:
        return openni::SENSOR_COLOR;
    }
    return openni::SENSOR_DEPTH;
}

bool supportsDisplayFormat(ViewMode mode, openni::PixelFormat format) {
    switch (mode) {
    case ViewMode::Depth:
        return format == openni::PIXEL_FORMAT_DEPTH_1_MM ||
               format == openni::PIXEL_FORMAT_SHIFT_9_2;
    case ViewMode::Infrared:
        return format == openni::PIXEL_FORMAT_GRAY16 ||
               format == openni::PIXEL_FORMAT_GRAY8;
    case ViewMode::Color:
        return format == openni::PIXEL_FORMAT_RGB888;
    }
    return false;
}

void selectHighestResolution(openni::VideoStream& stream, ViewMode mode) {
    const openni::Array<openni::VideoMode>& supportedModes =
        stream.getSensorInfo().getSupportedVideoModes();
    std::vector<openni::VideoMode> candidates;

    for (int i = 0; i < supportedModes.getSize(); ++i) {
        const openni::VideoMode& candidate = supportedModes[i];
        if (!supportsDisplayFormat(mode, candidate.getPixelFormat())) {
            continue;
        }
        candidates.push_back(candidate);
    }

    if (candidates.empty()) {
        std::cout << "No supported " << modeName(mode)
                  << " mode matched the viewer's pixel formats; using the "
                     "driver default."
                  << std::endl;
        return;
    }

    const int preferredArea = mode == ViewMode::Color
        ? 320 * 240
        : 640 * 480;
    std::sort(candidates.begin(), candidates.end(),
        [preferredArea](const openni::VideoMode& left,
                        const openni::VideoMode& right) {
            const int leftArea = left.getResolutionX() *
                                 left.getResolutionY();
            const int rightArea = right.getResolutionX() *
                                  right.getResolutionY();
            const bool leftPreferred = leftArea <= preferredArea;
            const bool rightPreferred = rightArea <= preferredArea;
            if (leftPreferred != rightPreferred) {
                return leftPreferred;
            }
            if (leftArea != rightArea) {
                return leftPreferred ? leftArea > rightArea
                                     : leftArea < rightArea;
            }
            return left.getFps() > right.getFps();
        });

    for (const openni::VideoMode& candidate : candidates) {
        if (stream.setVideoMode(candidate) != openni::STATUS_OK) {
            std::cerr << "Could not select " << modeName(mode) << " mode "
                      << candidate.getResolutionX() << "x"
                      << candidate.getResolutionY() << " @ "
                      << candidate.getFps() << " FPS: "
                      << openni::OpenNI::getExtendedError() << std::endl;
            continue;
        }

        const openni::VideoMode selected = stream.getVideoMode();
        std::cout << modeName(mode) << " stream: "
                  << selected.getResolutionX() << "x"
                  << selected.getResolutionY() << " @ "
                  << selected.getFps() << " FPS" << std::endl;
        return;
    }

    const openni::VideoMode selected = stream.getVideoMode();
    std::cerr << "Could not set a supported " << modeName(mode)
              << " mode; retaining driver default "
              << selected.getResolutionX() << "x"
              << selected.getResolutionY() << " @ "
              << selected.getFps() << " FPS." << std::endl;
}

bool startStream(openni::Device& device, ViewMode mode,
                 openni::VideoStream& stream) {
    const openni::Status createStatus = stream.create(device, sensorType(mode));
    if (createStatus != openni::STATUS_OK) {
        std::cerr << modeName(mode) << " sensor is unavailable: "
                  << openni::OpenNI::getExtendedError() << std::endl;
        return false;
    }

    selectHighestResolution(stream, mode);

    const openni::Status startStatus = stream.start();
    if (startStatus != openni::STATUS_OK) {
        std::cerr << "Could not start " << modeName(mode) << " stream: "
                  << openni::OpenNI::getExtendedError() << std::endl;
        stream.destroy();
        return false;
    }
    return true;
}

void reportDeviceInformation(const openni::Device& device) {
    std::cout << "Camera: " << device.getDeviceInfo().getVendor()
              << " " << device.getDeviceInfo().getName() << std::endl;

    if (device.isPropertySupported(openni::DEVICE_PROPERTY_FIRMWARE_VERSION)) {
        char firmware[128] = {};
        int size = sizeof(firmware);
        const openni::Status result = device.getProperty(
            openni::DEVICE_PROPERTY_FIRMWARE_VERSION, firmware, &size);
        if (result == openni::STATUS_OK) {
            std::cout << "Camera firmware: " << firmware << std::endl;
        } else {
            std::cout << "Camera firmware: supported, but read failed ("
                      << result << ")" << std::endl;
        }
    } else {
        std::cout << "Camera firmware: not exposed by driver." << std::endl;
    }

    if (device.isPropertySupported(openni::DEVICE_PROPERTY_HARDWARE_VERSION)) {
        int version = 0;
        if (device.getProperty(openni::DEVICE_PROPERTY_HARDWARE_VERSION,
                               &version) == openni::STATUS_OK) {
            std::cout << "Camera hardware version: " << version << std::endl;
        }
    }
}

void reportStreamProperties(openni::VideoStream& stream, ViewMode mode) {
    std::cout << modeName(mode) << " camera properties:";

    float horizontalFov = 0.0f;
    if (stream.getProperty(openni::STREAM_PROPERTY_HORIZONTAL_FOV,
                           &horizontalFov) == openni::STATUS_OK) {
        std::cout << " horizontal FOV="
                  << horizontalFov * 180.0 / CV_PI << " deg;";
    }

    float verticalFov = 0.0f;
    if (stream.getProperty(openni::STREAM_PROPERTY_VERTICAL_FOV,
                           &verticalFov) == openni::STATUS_OK) {
        std::cout << " vertical FOV="
                  << verticalFov * 180.0 / CV_PI << " deg;";
    }

    int numberOfFrames = 0;
    if (stream.getProperty(openni::STREAM_PROPERTY_NUMBER_OF_FRAMES,
                           &numberOfFrames) == openni::STATUS_OK) {
        std::cout << " frame count=" << numberOfFrames << ";";
    }

    const int booleanProperties[] = {
        openni::STREAM_PROPERTY_AUTO_EXPOSURE,
        openni::STREAM_PROPERTY_AUTO_WHITE_BALANCE
    };
    const char* booleanNames[] = {"auto-exposure", "auto-white-balance"};
    for (int i = 0; i < 2; ++i) {
        if (!stream.isPropertySupported(booleanProperties[i])) {
            continue;
        }
        OniBool enabled = FALSE;
        const openni::Status result =
            stream.getProperty(booleanProperties[i], &enabled);
        std::cout << " " << booleanNames[i] << "=";
        if (result == openni::STATUS_OK) {
            std::cout << (enabled == TRUE ? "on" : "off");
        } else {
            std::cout << "read failed (" << result << ")";
        }
        std::cout << ";";
    }

    const int integerProperties[] = {
        openni::STREAM_PROPERTY_EXPOSURE,
        openni::STREAM_PROPERTY_GAIN
    };
    const char* integerNames[] = {"exposure", "gain"};
    for (int i = 0; i < 2; ++i) {
        if (!stream.isPropertySupported(integerProperties[i])) {
            continue;
        }
        int value = 0;
        const openni::Status result =
            stream.getProperty(integerProperties[i], &value);
        std::cout << " " << integerNames[i] << "=";
        if (result == openni::STATUS_OK) {
            std::cout << value;
        } else {
            std::cout << "read failed (" << result << ")";
        }
        std::cout << ";";
    }

    std::cout << std::endl;
}

void reportTemperature(const openni::Device& device) {
    if (!device.isPropertySupported(
            XN_MODULE_PROPERTY_TEC_FAST_CONVERGENCE_STATUS)) {
        std::cout << "Camera temperature: not exposed by this firmware."
                  << std::endl;
        return;
    }

    XnTecFastConvergenceData status = {};
    const openni::Status result = device.getProperty(
        XN_MODULE_PROPERTY_TEC_FAST_CONVERGENCE_STATUS, &status);
    if (result != openni::STATUS_OK) {
        std::cout << "Camera temperature: property advertised, read failed ("
                  << result << ")." << std::endl;
        return;
    }

    std::cout << "Camera TEC temperature: "
              << static_cast<double>(status.m_MeasuredTemperature) / 100.0
              << " C (set point "
              << static_cast<double>(status.m_SetPointTemperature) / 100.0
              << " C)." << std::endl;
}

cv::Mat makeDisplayFrame(const openni::VideoFrameRef& frame, ViewMode mode,
                         int minimumDepth, int maximumDepth) {
    const int width = frame.getWidth();
    const int height = frame.getHeight();
    if (width <= 0 || height <= 0 || frame.getData() == nullptr) {
        return {};
    }

    if (mode == ViewMode::Color) {
        if (frame.getVideoMode().getPixelFormat() != openni::PIXEL_FORMAT_RGB888) {
            std::cerr << "Unsupported color pixel format: "
                      << frame.getVideoMode().getPixelFormat() << std::endl;
            return {};
        }
        cv::Mat rgb(height, width, CV_8UC3,
                    const_cast<void*>(frame.getData()));
        cv::Mat bgr;
        cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
        return bgr;
    }

    if (mode == ViewMode::Infrared) {
        const openni::PixelFormat pixelFormat =
            frame.getVideoMode().getPixelFormat();
        cv::Mat grayscale;
        if (pixelFormat == openni::PIXEL_FORMAT_GRAY16) {
            cv::Mat source(height, width, CV_16UC1,
                           const_cast<void*>(frame.getData()));
            cv::normalize(source, grayscale, 0, 255, cv::NORM_MINMAX,
                          CV_8UC1);
        } else if (pixelFormat == openni::PIXEL_FORMAT_GRAY8) {
            cv::Mat source(height, width, CV_8UC1,
                           const_cast<void*>(frame.getData()));
            grayscale = source.clone();
        } else {
            std::cerr << "Unsupported pixel format for Infrared: "
                      << pixelFormat << std::endl;
            return {};
        }

        cv::Mat colorized;
        cv::applyColorMap(grayscale, colorized, cv::COLORMAP_JET);
        return colorized;
    }

    if (frame.getVideoMode().getPixelFormat() !=
        openni::PIXEL_FORMAT_DEPTH_1_MM &&
        frame.getVideoMode().getPixelFormat() !=
        openni::PIXEL_FORMAT_SHIFT_9_2) {
        std::cerr << "Unsupported 16-bit pixel format for " << modeName(mode)
                  << ": " << frame.getVideoMode().getPixelFormat()
                  << std::endl;
        return {};
    }

    cv::Mat source(height, width, CV_16UC1,
                   const_cast<void*>(frame.getData()));
    cv::Mat grayscale;
    if (mode == ViewMode::Depth) {
        cv::Mat clipped;
        cv::max(source, minimumDepth, clipped);
        cv::min(clipped, maximumDepth, clipped);
        clipped.convertTo(grayscale, CV_8UC1,
                          255.0 / (maximumDepth - minimumDepth),
                          -minimumDepth * 255.0 /
                              (maximumDepth - minimumDepth));
    }

    cv::Mat colorized;
    cv::applyColorMap(grayscale, colorized, cv::COLORMAP_JET);
    return colorized;
}

cv::Mat makeViewerCanvas(const cv::Mat& frame, ViewMode mode,
                         ViewerControls& controls, double framesPerSecond,
                         std::uint64_t frameCount) {
    const int footerHeight = mode == ViewMode::Depth ? 60 : 0;
    const int maxPreviewWidth = std::max(320, GetSystemMetrics(SM_CXSCREEN) - 40);
    const int maxPreviewHeight = std::max(
        120, std::min(450, GetSystemMetrics(SM_CYSCREEN) -
                               kToolbarHeight - footerHeight - 50));
    const double scale = std::min(
        2.0, std::min(static_cast<double>(maxPreviewWidth) / frame.cols,
                      static_cast<double>(maxPreviewHeight) / frame.rows));
    cv::Mat preview;
    cv::resize(frame, preview, cv::Size(), scale, scale,
               scale < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR);

    const int canvasWidth = std::max(320, preview.cols);
    const int previewX = (canvasWidth - preview.cols) / 2;
    controls.canvasWidth = canvasWidth;
    controls.previewHeight = preview.rows;
    controls.depthSlidersVisible = mode == ViewMode::Depth;
    cv::Mat canvas(preview.rows + kToolbarHeight + footerHeight,
                   canvasWidth, CV_8UC3,
                   cv::Scalar(35, 38, 44));
    preview.copyTo(canvas(cv::Rect(previewX, kToolbarHeight,
                                   preview.cols, preview.rows)));

    const ViewMode modes[] = {
        ViewMode::Depth, ViewMode::Infrared, ViewMode::Color
    };
    const char* labels[] = {"Depth", "IR", "Color"};
    const int buttonWidth = 82;
    const int buttonHeight = 32;

    for (int i = 0; i < 3; ++i) {
        const cv::Rect button(10 + i * (buttonWidth + 6), 8,
                              buttonWidth, buttonHeight);
        const cv::Scalar color = modes[i] == mode
            ? cv::Scalar(55, 125, 75)
            : cv::Scalar(62, 67, 76);
        cv::rectangle(canvas, button, color, cv::FILLED);
        cv::rectangle(canvas, button, cv::Scalar(105, 112, 124), 1);
        cv::putText(canvas, labels[i],
                    cv::Point(button.x + 12, button.y + 22),
                    cv::FONT_HERSHEY_SIMPLEX, 0.48,
                    cv::Scalar(255, 255, 255), 1);
    }

    std::ostringstream details;
    details << modeName(mode) << " | " << frame.cols << "x" << frame.rows
            << " | " << std::fixed << std::setprecision(1) << framesPerSecond
            << " FPS | #" << frameCount;
    const int detailsFont = cv::FONT_HERSHEY_SIMPLEX;
    const double detailsScale = 0.42;
    const int detailsThickness = 1;
    const cv::Size detailsSize = cv::getTextSize(
        details.str(), detailsFont, detailsScale, detailsThickness, nullptr);
    const std::string shortcuts = "S: save  |  Esc: quit";
    const cv::Size shortcutsSize = cv::getTextSize(
        shortcuts, detailsFont, detailsScale, detailsThickness, nullptr);
    const int statsY = 59;
    const int detailsX = 12;
    const int shortcutsX = canvas.cols - shortcutsSize.width - 12;
    cv::putText(canvas, details.str(), cv::Point(detailsX, statsY),
                detailsFont, detailsScale,
                cv::Scalar(215, 219, 226), 1);
    if (detailsX + detailsSize.width + 16 <= shortcutsX) {
        cv::putText(canvas, shortcuts, cv::Point(shortcutsX, statsY),
                    detailsFont, detailsScale,
                    cv::Scalar(165, 171, 181), 1);
    }

    if (mode == ViewMode::Depth) {
        const int sliderLeft = 112;
        const int sliderRight = canvas.cols - 24;
        const int sliderWidth = sliderRight - sliderLeft;
        const int minimumY = kToolbarHeight + preview.rows + 19;
        const int maximumY = minimumY + 25;
        const cv::Scalar trackColor(83, 89, 99);
        const cv::Scalar activeColor(76, 158, 111);
        cv::putText(canvas,
                    "Min " + std::to_string(controls.minimumDepth) + " mm",
                    cv::Point(12, minimumY + 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.4,
                    cv::Scalar(220, 224, 230), 1);
        cv::putText(canvas,
                    "Max " + std::to_string(controls.maximumDepth) + " mm",
                    cv::Point(12, maximumY + 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.4,
                    cv::Scalar(220, 224, 230), 1);
        cv::line(canvas, cv::Point(sliderLeft, minimumY),
                 cv::Point(sliderRight, minimumY), trackColor, 4);
        cv::line(canvas, cv::Point(sliderLeft, maximumY),
                 cv::Point(sliderRight, maximumY), trackColor, 4);
        const int minimumX = sliderLeft +
            (controls.minimumDepth - 1) * sliderWidth / 9999;
        const int maximumX = sliderLeft +
            (controls.maximumDepth - 1) * sliderWidth / 9999;
        cv::line(canvas, cv::Point(sliderLeft, minimumY),
                 cv::Point(minimumX, minimumY), activeColor, 4);
        cv::line(canvas, cv::Point(sliderLeft, maximumY),
                 cv::Point(maximumX, maximumY), activeColor, 4);
        cv::circle(canvas, cv::Point(minimumX, minimumY), 7,
                   cv::Scalar(236, 241, 247), cv::FILLED);
        cv::circle(canvas, cv::Point(maximumX, maximumY), 7,
                   cv::Scalar(236, 241, 247), cv::FILLED);
    }
    return canvas;
}

void handleModeButtonClick(int event, int x, int y, int,
                           void* userData) {
    ViewerControls& controls = *static_cast<ViewerControls*>(userData);
    const int buttonWidth = 82;
    const int buttonGap = 6;
    const int buttonIndex = (x - 10) / (buttonWidth + buttonGap);
    const int buttonX = x - buttonIndex * (buttonWidth + buttonGap);
    if (event == cv::EVENT_LBUTTONDOWN && y >= 8 && y < 40 &&
        buttonIndex >= 0 && buttonIndex < 3 &&
        buttonX >= 10 && buttonX < 10 + buttonWidth) {
        controls.clickedMode = buttonIndex;
        return;
    }

    if (event == cv::EVENT_LBUTTONDOWN) {
        controls.draggingMinimum = false;
        controls.draggingMaximum = false;
        const int minimumY = kToolbarHeight + controls.previewHeight + 19;
        const int maximumY = minimumY + 25;
        if (controls.depthSlidersVisible && x >= 112 &&
            y >= minimumY - 12 &&
            y <= maximumY + 12) {
            if (std::abs(y - minimumY) <= 12) {
                controls.draggingMinimum = true;
            } else if (std::abs(y - maximumY) <= 12) {
                controls.draggingMaximum = true;
            }
        }
    } else if (event == cv::EVENT_LBUTTONUP) {
        controls.draggingMinimum = false;
        controls.draggingMaximum = false;
    }

    if ((event == cv::EVENT_MOUSEMOVE || event == cv::EVENT_LBUTTONDOWN) &&
        (controls.draggingMinimum || controls.draggingMaximum)) {
        const int sliderLeft = 112;
        const int sliderRight = std::max(sliderLeft + 1,
                                         controls.canvasWidth - 24);
        const int sliderWidth = sliderRight - sliderLeft;
        const int value = 1 + std::max(0, std::min(sliderWidth,
            x - sliderLeft)) * 9999 / sliderWidth;
        if (controls.draggingMinimum) {
            controls.minimumDepth = std::min(value,
                                              controls.maximumDepth - 1);
        } else {
            controls.maximumDepth = std::max(value,
                                              controls.minimumDepth + 1);
        }
    }
}

bool windowIsOpen(const std::string& windowName) {
    return cv::getWindowProperty(windowName, cv::WND_PROP_VISIBLE) > 0;
}

} // namespace

int main() {
    openni::Status status = openni::OpenNI::initialize();
    if (status != openni::STATUS_OK) {
        std::cerr << "Could not initialize OpenNI: "
                  << openni::OpenNI::getExtendedError() << std::endl;
        return 1;
    }

    openni::Device device;
    status = device.open(openni::ANY_DEVICE);
    if (status != openni::STATUS_OK) {
        std::cerr << "Could not open the PrimeSense camera: "
                  << openni::OpenNI::getExtendedError() << std::endl;
        openni::OpenNI::shutdown();
        return 1;
    }

    ViewMode mode = ViewMode::Depth;
    openni::VideoStream stream;
    if (!startStream(device, mode, stream)) {
        device.close();
        openni::OpenNI::shutdown();
        return 1;
    }
    reportDeviceInformation(device);
    reportTemperature(device);
    reportStreamProperties(stream, mode);

    const std::string windowName = "PrimeSense Viewer";
    cv::namedWindow(windowName, cv::WINDOW_AUTOSIZE);
    ViewerControls controls;
    cv::setMouseCallback(windowName, handleModeButtonClick, &controls);

    std::cout << "Live PrimeSense viewer. Click Depth, IR, or Color to switch "
                 "modes; drag depth sliders to adjust range; s=save frame, "
                 "q/Esc=quit."
              << std::endl;

    bool running = true;
    std::uint64_t frameCount = 0;
    int framesInInterval = 0;
    double framesPerSecond = 0.0;
    bool reportedFrameMetadata = false;
    std::chrono::steady_clock::time_point fpsIntervalStart =
        std::chrono::steady_clock::now();
    while (running) {
        openni::VideoStream* streams[] = {&stream};
        int readyStreamIndex = 0;
        status = openni::OpenNI::waitForAnyStream(
            streams, 1, &readyStreamIndex, 1000);
        if (status == openni::STATUS_TIME_OUT) {
            const int key = cv::waitKey(1) & 0xFF;
            if (key == 27 || key == 'q' || key == 'Q') {
                break;
            }
            if (!windowIsOpen(windowName)) {
                running = false;
                break;
            }
            continue;
        }
        if (status != openni::STATUS_OK) {
            std::cerr << "Waiting for a camera frame failed: "
                      << openni::OpenNI::getExtendedError() << std::endl;
            break;
        }

        openni::VideoFrameRef frame;
        status = stream.readFrame(&frame);
        if (status != openni::STATUS_OK || !frame.isValid()) {
            std::cerr << "Could not read a camera frame: "
                      << openni::OpenNI::getExtendedError() << std::endl;
            break;
        }

        if (!reportedFrameMetadata) {
            std::cout << modeName(mode) << " frame metadata: index="
                      << frame.getFrameIndex() << ", timestamp="
                      << frame.getTimestamp() << " us" << std::endl;
            reportedFrameMetadata = true;
        }

        ++frameCount;
        ++framesInInterval;
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now();
        const double elapsedSeconds =
            std::chrono::duration<double>(now - fpsIntervalStart).count();
        if (elapsedSeconds >= 1.0) {
            framesPerSecond = framesInInterval / elapsedSeconds;
            framesInInterval = 0;
            fpsIntervalStart = now;
        }

        cv::Mat display =
            makeDisplayFrame(frame, mode, controls.minimumDepth,
                             controls.maximumDepth);
        if (display.empty()) {
            break;
        }
        cv::Mat canvas = makeViewerCanvas(display, mode, controls,
                                          framesPerSecond, frameCount);
        cv::imshow(windowName, canvas);

        const int key = cv::waitKey(1) & 0xFF;
        if (key == 27 || key == 'q' || key == 'Q') {
            running = false;
        } else if (key == 's' || key == 'S') {
            const std::string filename = "primesense_" +
                                         std::string(modeName(mode)) +
                                         ".png";
            if (!cv::imwrite(filename, display)) {
                std::cerr << "Could not save frame to " << filename
                          << std::endl;
            } else {
                std::cout << "Saved " << filename << std::endl;
            }
        } else {
            if (!windowIsOpen(windowName)) {
                running = false;
                break;
            }

            ViewMode requestedMode = mode;
            if (key == '1') {
                requestedMode = ViewMode::Depth;
            } else if (key == '2') {
                requestedMode = ViewMode::Infrared;
            } else if (key == '3') {
                requestedMode = ViewMode::Color;
            }
            if (controls.clickedMode >= 0) {
                requestedMode = controls.clickedMode == 0 ? ViewMode::Depth
                    : controls.clickedMode == 1 ? ViewMode::Infrared
                                                 : ViewMode::Color;
                controls.clickedMode = -1;
            }

            if (requestedMode != mode) {
                stream.stop();
                stream.destroy();
                if (startStream(device, requestedMode, stream)) {
                    mode = requestedMode;
                    reportStreamProperties(stream, mode);
                    reportedFrameMetadata = false;
                    framesInInterval = 0;
                    framesPerSecond = 0.0;
                    fpsIntervalStart = std::chrono::steady_clock::now();
                } else if (!startStream(device, mode, stream)) {
                    running = false;
                }
            }
        }
        if (running && !windowIsOpen(windowName)) {
            running = false;
        }
    }

    stream.stop();
    stream.destroy();
    device.close();
    openni::OpenNI::shutdown();
    cv::destroyAllWindows();
    return running ? 1 : 0;
}
