// Windows: DirectShow device enumeration. The order matches OpenCV's CAP_DSHOW indices, which is the backend
// HeadTracker tries first. This file must not include raylib.h (it clashes with windows.h).
#include "tracking/camera_list.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#endif

namespace tess {

#ifdef _WIN32
namespace {
std::string Utf8(const wchar_t* w) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string s(size_t(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}
} // namespace

std::vector<CameraInfo> ListCameras() {
    std::vector<CameraInfo> out;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninit = SUCCEEDED(init);   // RPC_E_CHANGED_MODE: COM is already up in another mode; still usable
    ICreateDevEnum* devEnum = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&devEnum)))) {
        IEnumMoniker* en = nullptr;
        if (devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &en, 0) == S_OK) {
            IMoniker* mon = nullptr;
            int index = 0;
            while (en->Next(1, &mon, nullptr) == S_OK) {
                std::string name = "Camera " + std::to_string(index);
                IPropertyBag* bag = nullptr;
                if (SUCCEEDED(mon->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&bag)))) {
                    VARIANT v;
                    VariantInit(&v);
                    if (SUCCEEDED(bag->Read(L"FriendlyName", &v, nullptr)) && v.vt == VT_BSTR) {
                        const std::string s = Utf8(v.bstrVal);
                        if (!s.empty()) name = s;
                    }
                    VariantClear(&v);
                    bag->Release();
                }
                out.push_back({index++, name});
                mon->Release();
            }
            en->Release();
        }
        devEnum->Release();
    }
    if (uninit) CoUninitialize();
    if (out.empty()) out.push_back({0, "Camera 0"});
    return out;
}
#else
std::vector<CameraInfo> ListCameras() {
    std::vector<CameraInfo> out;
    for (int i = 0; i < 4; ++i) out.push_back({i, "Camera " + std::to_string(i)});
    return out;
}
#endif

} // namespace tess
