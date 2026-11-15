#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    // gdiplus.h uses unqualified min/max. NOMINMAX removes the macros, so
    // provide the std versions globally or the header will not compile.
    #ifdef min
        #undef min
    #endif
    #ifdef max
        #undef max
    #endif
    using std::min;
    using std::max;
    #include <shlobj.h>   // DROPFILES
    #include <gdiplus.h>
    #pragma comment(lib, "gdiplus.lib")
#endif

using namespace geode::prelude;

namespace LoopShot {

    struct BusyGuard {
        static inline bool s_busy = false;
        bool owns = false;
        BusyGuard() {
            if (!s_busy) { s_busy = true; owns = true; }
        }
        ~BusyGuard() { if (owns) s_busy = false; }
    };

    static void notify(std::string const& message, bool ok = true) {
        if (Mod::get()->getSettingValue<bool>("show-notifications")) {
            Notification::create(
                message, ok ? NotificationIcon::Success : NotificationIcon::Error
            )->show();
        }
    }

    static std::filesystem::path getOutputFolder() {
        auto folder = Mod::get()->getSettingValue<std::filesystem::path>("output-folder");
        if (folder.empty())
            folder = Mod::get()->getSaveDir() / "Screenshots";
        return folder;
    }

    static std::tm localTime() {
        auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        return tm;
    }

    static std::string makeFileStem() {
        auto prefix = Mod::get()->getSettingValue<std::string>("filename-prefix");
        if (prefix.empty()) prefix = "Loop";
        for (auto& c : prefix) {
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                c == '"' || c == '<' || c == '>' || c == '|')
                c = '_';
        }
        auto tm = localTime();
        char stamp[32]{};
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &tm);
        return prefix + "_" + stamp;
    }

#ifdef _WIN32
    class GdiPlusSession {
    public:
        GdiPlusSession() {
            Gdiplus::GdiplusStartupInput input;
            m_ok = Gdiplus::GdiplusStartup(&m_token, &input, nullptr) == Gdiplus::Ok;
        }
        ~GdiPlusSession() {
            if (m_ok) Gdiplus::GdiplusShutdown(m_token);
        }
        bool ok() const { return m_ok; }
    private:
        ULONG_PTR m_token = 0;
        bool m_ok = false;
    };

    static bool pngEncoder(CLSID& out) {
        UINT count = 0, bytes = 0;
        Gdiplus::GetImageEncodersSize(&count, &bytes);
        if (!bytes) return false;

        auto buffer = std::make_unique<BYTE[]>(bytes);
        auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.get());
        if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok) return false;

        for (UINT i = 0; i < count; ++i) {
            if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
                out = codecs[i].Clsid;
                return true;
            }
        }
        return false;
    }

    // Global memory holding a CF_HDROP file list (what Explorer/Discord paste as a file).
    static HGLOBAL makeDropHandle(std::filesystem::path const& file) {
        auto wide = file.wstring();
        size_t chars = wide.size() + 2; // path + its null + list terminator
        size_t bytes = sizeof(DROPFILES) + chars * sizeof(wchar_t);
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
        if (!mem) return nullptr;
        auto* drop = static_cast<DROPFILES*>(GlobalLock(mem));
        if (!drop) { GlobalFree(mem); return nullptr; }
        drop->pFiles = sizeof(DROPFILES);
        drop->fWide = TRUE;
        auto* dst = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(drop) + sizeof(DROPFILES));
        std::memcpy(dst, wide.c_str(), wide.size() * sizeof(wchar_t));
        GlobalUnlock(mem);
        return mem;
    }

    // Global memory holding the raw PNG file bytes (registered "PNG" format).
    static HGLOBAL makePngHandle(std::filesystem::path const& file) {
        std::ifstream in(file, std::ios::binary | std::ios::ate);
        if (!in) return nullptr;
        auto size = static_cast<size_t>(in.tellg());
        in.seekg(0);
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!mem) return nullptr;
        void* ptr = GlobalLock(mem);
        if (!ptr) { GlobalFree(mem); return nullptr; }
        in.read(static_cast<char*>(ptr), static_cast<std::streamsize>(size));
        GlobalUnlock(mem);
        return mem;
    }

    // pngFile may be empty; if it exists the clipboard also gets the file + PNG data.
    static bool copyBitmapToClipboard(Gdiplus::Bitmap& bitmap, std::filesystem::path const& pngFile) {
        HBITMAP handle = nullptr;
        if (bitmap.GetHBITMAP(Gdiplus::Color(255, 0, 0, 0), &handle) != Gdiplus::Ok || !handle)
            return false;

        HGLOBAL drop = pngFile.empty() ? nullptr : makeDropHandle(pngFile);
        HGLOBAL png = pngFile.empty() ? nullptr : makePngHandle(pngFile);
        UINT pngFormat = RegisterClipboardFormatW(L"PNG");

        // OpenClipboard(nullptr) makes SetClipboardData fail after EmptyClipboard,
        // so the clipboard must be opened with a real window handle.
        HWND owner = GetActiveWindow();
        if (!owner) owner = GetForegroundWindow();

        bool opened = false;
        for (int i = 0; i < 10 && !opened; ++i) {
            opened = OpenClipboard(owner) != 0;
            if (!opened) Sleep(10);
        }
        if (!opened) {
            DeleteObject(handle);
            if (drop) GlobalFree(drop);
            if (png) GlobalFree(png);
            return false;
        }

        EmptyClipboard();
        if (!SetClipboardData(CF_BITMAP, handle)) {
            CloseClipboard();
            DeleteObject(handle);
            if (drop) GlobalFree(drop);
            if (png) GlobalFree(png);
            return false;
        }

        // The system owns each handle after SetClipboardData succeeds.
        if (drop && !SetClipboardData(CF_HDROP, drop)) GlobalFree(drop);
        if (png && pngFormat && !SetClipboardData(pngFormat, png)) GlobalFree(png);
        else if (png && !pngFormat) GlobalFree(png);

        CloseClipboard();
        return true;
    }
#endif

    static void captureGame() {
#ifndef _WIN32
        log::warn("Loop Screenshot Studio currently implements capture on Windows only.");
        return;
#else
        BusyGuard guard;
        if (!guard.owns) return;

        bool saveFile = Mod::get()->getSettingValue<bool>("save-to-file");
        bool copyClipboard = Mod::get()->getSettingValue<bool>("copy-to-clipboard");
        if (!saveFile && !copyClipboard) {
            notify("Enable Save or Clipboard in Loop settings", false);
            return;
        }

        auto director = CCDirector::sharedDirector();
        auto scene = director ? director->getRunningScene() : nullptr;
        if (!scene) {
            log::warn("no active game scene.");
            notify("Screenshot failed: no active scene", false);
            return;
        }

        // ---- 1. Render the running scene into an offscreen texture ----------
        // create() takes size in points and multiplies by the content scale
        // factor internally, so this gives a full-resolution render.
        auto winSize = director->getWinSize();
        int pointsW = std::max(1, static_cast<int>(std::ceil(winSize.width)));
        int pointsH = std::max(1, static_cast<int>(std::ceil(winSize.height)));

        // 0x88F0 = GL_DEPTH24_STENCIL8. Stencil is needed so clipping nodes
        // (scroll lists, masks) render correctly.
        auto renderTexture = CCRenderTexture::create(
            pointsW, pointsH, kCCTexture2DPixelFormat_RGBA8888, 0x88F0
        );
        if (!renderTexture)
            renderTexture = CCRenderTexture::create(
                pointsW, pointsH, kCCTexture2DPixelFormat_RGBA8888
            );
        if (!renderTexture) {
            log::error("could not create CCRenderTexture ({}x{})", pointsW, pointsH);
            notify("Screenshot failed: render target unavailable", false);
            return;
        }

        // Clear with alpha = 1 so the finished image is opaque.
        renderTexture->beginWithClear(0.f, 0.f, 0.f, 1.f, 1.f, 0);
        scene->visit();
        renderTexture->end();

        CCImage* image = renderTexture->newCCImage(true);
        if (!image || !image->getData()) {
            log::error("newCCImage returned no pixel data");
            if (image) image->release();
            notify("Screenshot failed while rendering", false);
            return;
        }

        int sourceW = image->getWidth();
        int sourceH = image->getHeight();
        if (sourceW <= 0 || sourceH <= 0) {
            image->release();
            notify("Screenshot failed while rendering", false);
            return;
        }

        // ---- 2. Copy RGBA -> BGRA (what GDI+ expects), forcing opaque -------
        std::vector<uint8_t> bgra(static_cast<size_t>(sourceW) * sourceH * 4);
        {
            const uint8_t* src = image->getData();
            uint8_t* dst = bgra.data();
            size_t pixels = static_cast<size_t>(sourceW) * sourceH;
            for (size_t i = 0; i < pixels; ++i) {
                dst[i * 4 + 0] = src[i * 4 + 2];
                dst[i * 4 + 1] = src[i * 4 + 1];
                dst[i * 4 + 2] = src[i * 4 + 0];
                dst[i * 4 + 3] = 255;
            }
        }
        image->release();

        // ---- 3. Work out the output size ------------------------------------
        UINT targetW = static_cast<UINT>(sourceW);
        UINT targetH = static_cast<UINT>(sourceH);
        auto resolution = Mod::get()->getSettingValue<std::string>("resolution");

        if (resolution == "720p") targetW = 1280;
        else if (resolution == "1080p") targetW = 1920;
        else if (resolution == "1440p") targetW = 2560;
        else if (resolution == "4K") targetW = 3840;
        else if (resolution == "8K") targetW = 7680;
        else if (resolution == "Custom") {
            targetW = static_cast<UINT>(std::clamp<int64_t>(
                Mod::get()->getSettingValue<int64_t>("custom-width"), 320, 7680));
            targetH = static_cast<UINT>(std::clamp<int64_t>(
                Mod::get()->getSettingValue<int64_t>("custom-height"), 240, 4320));
        }

        if (resolution != "Native" && resolution != "Custom") {
            // Preserve the game's aspect ratio instead of stretching it.
            double aspect = static_cast<double>(sourceH) / static_cast<double>(sourceW);
            targetH = static_cast<UINT>(std::max(1.0, std::round(targetW * aspect)));
        }

        // ---- 4. Scale with GDI+ ---------------------------------------------
        GdiPlusSession gdip;
        if (!gdip.ok()) {
            log::error("GdiplusStartup failed");
            notify("Screenshot failed: image service unavailable", false);
            return;
        }

        // Scope so Bitmap/Graphics objects die before GdiplusShutdown.
        bool fileSaved = false;
        bool copied = false;
        std::filesystem::path finalPath;
        {
            Gdiplus::Bitmap source(
                sourceW, sourceH, sourceW * 4, PixelFormat32bppARGB, bgra.data()
            );
            if (source.GetLastStatus() != Gdiplus::Ok) {
                notify("Screenshot failed: could not read capture", false);
                return;
            }

            Gdiplus::Bitmap output(
                static_cast<INT>(targetW), static_cast<INT>(targetH), PixelFormat32bppARGB
            );
            if (output.GetLastStatus() != Gdiplus::Ok) {
                notify("Screenshot failed: requested resolution is too large", false);
                return;
            }

            {
                Gdiplus::Graphics graphics(&output);
                graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
                graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
                graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

                auto filter = Mod::get()->getSettingValue<std::string>("scaling-filter");
                if (filter == "Nearest")
                    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
                else if (filter == "Bilinear")
                    graphics.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
                else
                    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

                // Tile-flip wrap mode stops the image edges from fading out.
                Gdiplus::ImageAttributes attrs;
                attrs.SetWrapMode(Gdiplus::WrapModeTileFlipXY);

                graphics.DrawImage(
                    &source,
                    Gdiplus::Rect(0, 0, static_cast<INT>(targetW), static_cast<INT>(targetH)),
                    0, 0, sourceW, sourceH, Gdiplus::UnitPixel, &attrs
                );
            }

            // ---- 5. Save / copy ---------------------------------------------
            // The PNG is also written when only the clipboard is enabled, so a
            // file can be pasted (into Discord, Explorer...), not just an image.
            if (saveFile || copyClipboard) {
                auto folder = saveFile ? getOutputFolder() : Mod::get()->getSaveDir();
                if (saveFile && Mod::get()->getSettingValue<bool>("date-subfolders")) {
                    auto tm = localTime();
                    char date[16]{};
                    std::strftime(date, sizeof(date), "%Y-%m-%d", &tm);
                    folder /= date;
                }

                std::error_code ec;
                std::filesystem::create_directories(folder, ec);
                if (ec) {
                    log::error("could not create {}: {}", folder.string(), ec.message());
                } else {
                    finalPath = folder / (saveFile ? makeFileStem() + ".png"
                                                   : std::string("clipboard_latest.png"));
                    CLSID encoder{};
                    if (pngEncoder(encoder)) {
                        auto status = output.Save(finalPath.wstring().c_str(), &encoder, nullptr);
                        fileSaved = status == Gdiplus::Ok;
                        if (!fileSaved)
                            log::error("GDI+ Save failed with status {}", static_cast<int>(status));
                    } else {
                        log::error("PNG encoder not found");
                    }
                }
            }

            if (copyClipboard)
                copied = copyBitmapToClipboard(output, fileSaved ? finalPath : std::filesystem::path{});
        }

        // ---- 6. Report ------------------------------------------------------
        if (saveFile && !fileSaved) {
            notify(copied ? "Clipboard copied; file save failed" : "Screenshot failed to save", false);
        } else if (copyClipboard && !copied) {
            notify(fileSaved ? "Saved; clipboard copy failed" : "Clipboard copy failed", false);
        } else {
            notify(saveFile && copyClipboard ? "Saved + copied to clipboard" :
                   saveFile ? "Screenshot saved" : "Screenshot copied");
        }

        if (fileSaved && saveFile)
            log::info("Screenshot saved to {}", finalPath.string());
#endif
    }
}

$on_game(Loaded) {
    listenForKeybindSettingPresses(
        "capture-key",
        [](Keybind const&, bool down, bool repeat, double) {
            if (down && !repeat) {
                LoopShot::captureGame();
            }
        }
    );
}
