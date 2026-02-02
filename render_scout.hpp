#pragma once
#include <wrl.h>
#include <memory>
#include <cassert>
#include <windows.h>
#include <type_traits>

#include <d3d9.h>
#include <d3d10_1.h>
#include <d3d10.h>
#include <d3d11.h>
#include <d3d12.h>
#include <Dxgi1_3.h>

#define RENDER_SCOUT_VERSION "0.0.1"

namespace render_scout 
{
    enum class Status {
        Success,
        D3DError,
        UnknownError,
        UnknownRenderer,
        DummyWindowInitFailed,
        RendererModuleNotFound,
    };

    template <int Index, typename Fn>
    struct VMethod
    {
        using type = Fn;
        static constexpr int index = Index;
    };

    class VMT 
    {
    private:
        std::unique_ptr<void*[]> table;
        size_t methods_count = 0;

    public:
        ~VMT() = default;
        VMT() = default;
        VMT(VMT&&) = default;
        VMT& operator=(VMT&&) = default;

        VMT(const VMT&) = delete;
        VMT& operator=(const VMT&) = delete;

        VMT(void* vmt_address, size_t methods_count);

        size_t get_methods_count() const noexcept;

        template<size_t Index, typename Method>
        Method get_method() const 
        {
            static_assert(std::is_pointer_v<Method>);

            if (this->methods_count == 0 || Index >= this->methods_count) {
                return nullptr;
            }

            return reinterpret_cast<Method>(table[Index]);
        }
        
        template <typename VMethod>
        typename VMethod::type get_method() const {
            return this->get_method<VMethod::index, typename VMethod::type>();
        }
    };
    
    Status get_d3d9_vmt(_Out_opt_ VMT* d3d9_vmt, _Out_opt_ VMT* device_vmt);
    Status get_d3d10_vmt(_Out_opt_ VMT* swapchain_vmt, _Out_opt_ VMT* device_vmt);
    Status get_d3d11_vmt(_Out_opt_ VMT* swapchain_vmt, _Out_opt_ VMT* device_vmt, _Out_opt_ VMT* context_vmt);
    Status get_d3d12_vmt(_Out_opt_ VMT* swapchain_vmt, _Out_opt_ VMT* device_vmt, _Out_opt_ VMT* command_queue_vmt);

    namespace methods 
    {
        namespace d3d9 
        {
            using reset_t = HRESULT(__stdcall*)(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* pPresentationParameters);
            using present_t = HRESULT(__stdcall*)(IDirect3DDevice9* device, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion);
            using endscene_t = HRESULT(__stdcall*)(IDirect3DDevice9* device);

            using reset_vm = VMethod<16, reset_t>;
            using present_vm = VMethod<17, present_t>;
            using endscene_vm = VMethod<42, endscene_t>;
        }

        namespace d3d10 
        {
            using sc_present_t = HRESULT(__stdcall*)(IDXGISwapChain swapchain, UINT SyncInterval, UINT Flags);

            using sc_present_vm = VMethod<8, sc_present_t>;
        }

        namespace d3d11
        {
            using sc_present_t = HRESULT(__stdcall*)(IDXGISwapChain swapchain, UINT SyncInterval, UINT Flags);

            using sc_present_vm = VMethod<8, sc_present_t>;
        }

        namespace d3d12
        {
            using sc_present_t = HRESULT(__stdcall*)(IDXGISwapChain swapchain, UINT SyncInterval, UINT Flags);
            using sc_present1_t = HRESULT(__stdcall*)(IDXGISwapChain1 swapchain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters);

            using sc_present_vm = VMethod<8, sc_present_t>;
            using sc_present1_vm = VMethod<21, sc_present1_t>;
        }
    }

#ifdef RENDER_SCOUT_IMPLEMENTATION
    
    VMT::VMT(void* vmt_address, size_t methods_count) 
    {  
        if (methods_count == 0) {
            return;
        }

        table = std::make_unique<void*[]>(methods_count);
        memcpy(table.get(), vmt_address, sizeof(void*) * methods_count);

        this->methods_count = methods_count;
    }   

    size_t VMT::get_methods_count() const noexcept {
        return this->methods_count;
    }

    namespace detail 
    {
        class DummyWindow
        {
        private:
            ATOM klass = NULL;
            HWND window = NULL;

        public:
            DummyWindow(const DummyWindow&) = delete;
            DummyWindow& operator=(const DummyWindow&) = delete;
            DummyWindow(DummyWindow&&) noexcept = delete;
            DummyWindow& operator=(DummyWindow&&) noexcept = delete;

            DummyWindow() : DummyWindow(L"tmp_window_class") {}
            DummyWindow(const wchar_t* window_class_name)
            {
                WNDCLASSEXW wndclass{};
                wndclass.cbSize = sizeof(wndclass);
                wndclass.hInstance = GetModuleHandleW(NULL);
                wndclass.lpfnWndProc = DefWindowProcW;
                wndclass.lpszClassName = window_class_name;

                klass = RegisterClassExW(&wndclass);
                assert(klass && "RegisterClass has failed");

                window = CreateWindowW(
                    MAKEINTATOM(klass),
                    NULL,
                    WS_OVERLAPPED | WS_DISABLED,
                    0,
                    0,
                    CW_USEDEFAULT, CW_USEDEFAULT,
                    HWND_MESSAGE,
                    NULL,
                    wndclass.hInstance,
                    NULL
                );
                assert(window && "CreateWindow has failed");
            }

            ~DummyWindow()
            {
                if (window) {
                    DestroyWindow(window);
                    window = nullptr;
                }

                if (klass) {
                    UnregisterClassW(MAKEINTATOM(klass), GetModuleHandleW(NULL));
                    klass = NULL;
                }
            }

            HWND get_handle() const noexcept {
                return window;
            }

            bool is_initialized() const noexcept {
                return klass != NULL && window != nullptr;
            }

        };
    }

    Status get_d3d9_vmt(VMT* d3d9_vmt, VMT* device_vmt)
    {
        using namespace Microsoft::WRL;

        constexpr size_t d3d9_methods_count = 17;
        constexpr size_t device_methods_count = 119;

        auto d3d9_mod = GetModuleHandleW(L"d3d9.dll");
        if (!d3d9_mod) {
            return Status::RendererModuleNotFound;
        }
        
        using Direct3DCreate9_t = decltype(Direct3DCreate9)*;
        auto create_fn = reinterpret_cast<Direct3DCreate9_t>(GetProcAddress(d3d9_mod, "Direct3DCreate9"));
        if (!create_fn) {
            return Status::UnknownError;
        }

        detail::DummyWindow window;
        if (!window.is_initialized()) {
            return Status::DummyWindowInitFailed;
        }

        ComPtr<IDirect3D9> d3d = create_fn(D3D_SDK_VERSION);
        if (!d3d) {
            return Status::D3DError;
        }

        D3DPRESENT_PARAMETERS params{};
        params.Windowed = TRUE;
        params.SwapEffect = D3DSWAPEFFECT_DISCARD;
        params.hDeviceWindow = window.get_handle();

        ComPtr<IDirect3DDevice9> device;
        auto hr = d3d->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_NULLREF,
            window.get_handle(),
            D3DCREATE_DISABLE_DRIVER_MANAGEMENT | D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &params,
            &device
        );
        if (FAILED(hr)) {
            return Status::D3DError;
        }

        if (d3d9_vmt) {
            *d3d9_vmt = std::move(VMT(*reinterpret_cast<void**>(d3d.Get()), d3d9_methods_count));
        }
        if (device_vmt) {
            *device_vmt = std::move(VMT(*reinterpret_cast<void**>(device.Get()), device_methods_count));
        }

        return Status::Success;
    }

    Status get_d3d10_vmt(VMT* swapchain_vmt, VMT* device_vmt)
    {
        using namespace Microsoft::WRL;
        
        constexpr size_t device_methods_count = 98;
        constexpr size_t swapchain_methods_count = 18;

        auto d3d10_mod = GetModuleHandleW(L"d3d10.dll");
        if (!d3d10_mod) {
            return Status::RendererModuleNotFound;
        }

        using D3D10CreateDeviceAndSwapChain_t = decltype(D3D10CreateDeviceAndSwapChain)*;
        auto create_fn = reinterpret_cast<D3D10CreateDeviceAndSwapChain_t>(GetProcAddress(d3d10_mod, "D3D10CreateDeviceAndSwapChain"));
        if (!create_fn) {
            return Status::UnknownError;
        }

        detail::DummyWindow window;
        if (!window.is_initialized()) {
            return Status::DummyWindowInitFailed;
        }

        DXGI_SWAP_CHAIN_DESC desc{};
        desc.Windowed = TRUE;
        desc.BufferCount = 1;
        desc.SampleDesc.Count = 1;
        desc.OutputWindow = window.get_handle();
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

        ComPtr<ID3D10Device> device;
        ComPtr<IDXGISwapChain> swapchain;
        auto hr = create_fn(
            nullptr,
            D3D10_DRIVER_TYPE_NULL,
            NULL,
            NULL,
            D3D10_SDK_VERSION,
            &desc,
            &swapchain,
            &device
        );
        if (FAILED(hr)) {
            return Status::D3DError;
        }
        
        if (device_vmt) {
            *device_vmt = std::move(VMT(*reinterpret_cast<void**>(device.Get()), device_methods_count));
        }
        if (swapchain_vmt) {
            *swapchain_vmt = std::move(VMT(*reinterpret_cast<void**>(swapchain.Get()), swapchain_methods_count));
        }

        return Status::Success;
    }

    Status get_d3d11_vmt(VMT* swapchain_vmt, VMT* device_vmt, VMT* context_vmt)
    {
        using namespace Microsoft::WRL;

        constexpr size_t device_methods_count = 43;
        constexpr size_t context_methods_count = 115;
        constexpr size_t swapchain_methods_count = 18;

        auto d3d11_mod = GetModuleHandleW(L"d3d11.dll");
        if (!d3d11_mod) {
            return Status::RendererModuleNotFound;
        }

        using D3D11CreateDeviceAndSwapChain_t = decltype(D3D11CreateDeviceAndSwapChain)*;
        auto create_fn = reinterpret_cast<D3D11CreateDeviceAndSwapChain_t>(GetProcAddress(d3d11_mod, "D3D11CreateDeviceAndSwapChain"));
        if (!create_fn) {
            return Status::UnknownError;
        }

        detail::DummyWindow window;
        if (!window.is_initialized()) {
            return Status::DummyWindowInitFailed;
        }

        DXGI_SWAP_CHAIN_DESC desc{};
        desc.Windowed = TRUE;
        desc.BufferCount = 1;
        desc.SampleDesc.Count = 1;
        desc.OutputWindow = window.get_handle();
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

        ComPtr<ID3D11Device> device;
        ComPtr<IDXGISwapChain> swapchain;
        ComPtr<ID3D11DeviceContext> context;
        auto hr = create_fn(
            nullptr,
            D3D_DRIVER_TYPE_NULL,
            nullptr,
            NULL,
            nullptr,
            NULL,
            D3D11_SDK_VERSION,
            &desc,
            &swapchain,
            &device,
            NULL,
            &context
        );
        if (FAILED(hr)) {
            return Status::D3DError;
        }

        if (device_vmt) {
            *device_vmt = std::move(VMT(*reinterpret_cast<void**>(device.Get()), device_methods_count));
        }
        if (context_vmt) {
            *context_vmt = std::move(VMT(*reinterpret_cast<void**>(context.Get()), context_methods_count));
        }
        if (swapchain_vmt) {
            *swapchain_vmt = std::move(VMT(*reinterpret_cast<void**>(swapchain.Get()), swapchain_methods_count));
        }

        return Status::Success;
    }

    Status get_d3d12_vmt(VMT* swapchain_vmt, VMT* device_vmt, VMT* command_queue_vmt)
    {
        using namespace Microsoft::WRL;

        constexpr size_t device_methods_count = 44;
        constexpr size_t swapchain_methods_count = 29;
        constexpr size_t command_queue_methods_count = 19;

        auto d3d12_mod = GetModuleHandleW(L"d3d12.dll");
        if (!d3d12_mod) {
            return Status::RendererModuleNotFound;
        }

        auto dxgi_mod = GetModuleHandleW(L"dxgi.dll");
        if (!dxgi_mod) {
            return Status::RendererModuleNotFound;
        }

        using D3D12CreateDevice_t = decltype(D3D12CreateDevice)*;
        auto create_fn = reinterpret_cast<D3D12CreateDevice_t>(GetProcAddress(d3d12_mod, "D3D12CreateDevice"));
        if (!create_fn) {
            return Status::UnknownError;
        }

        using CreateDXGIFactory2_t = decltype(CreateDXGIFactory2)*;
        auto create_factory_fn = reinterpret_cast<CreateDXGIFactory2_t>(GetProcAddress(dxgi_mod, "CreateDXGIFactory2"));
        if (!create_factory_fn) {
            return Status::UnknownError;
        }

        detail::DummyWindow window;
        if (!window.is_initialized()) {
            return Status::DummyWindowInitFailed;
        }

        HRESULT hr;
        ComPtr<ID3D12Device> device;
        hr = create_fn(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
        if (FAILED(hr)) {
            return Status::D3DError;
        }

        ComPtr<IDXGIFactory2> factory;
        hr = create_factory_fn(NULL, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            return Status::D3DError;
        }

        D3D12_COMMAND_QUEUE_DESC cmq_desc{};

        ComPtr<ID3D12CommandQueue> command_queue;
        hr = device->CreateCommandQueue(&cmq_desc, IID_PPV_ARGS(&command_queue));
        if (FAILED(hr)) {
            return Status::D3DError;
        }

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.BufferCount = 2;
        desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        ComPtr<IDXGISwapChain1> swapchain;
        hr = factory->CreateSwapChainForHwnd(command_queue.Get(), window.get_handle(), &desc, nullptr, nullptr, &swapchain);
        if (FAILED(hr)) {
            return Status::D3DError;
        }
        
        if (device_vmt) {
            *device_vmt = std::move(VMT(*reinterpret_cast<void**>(device.Get()), device_methods_count));
        }
        if (swapchain_vmt) {
            *swapchain_vmt = std::move(VMT(*reinterpret_cast<void**>(swapchain.Get()), swapchain_methods_count));
        }
        if (command_queue_vmt) {
            *command_queue_vmt = std::move(VMT(*reinterpret_cast<void**>(command_queue.Get()), command_queue_methods_count));
        }

        return Status::Success;
    }

#endif
}
