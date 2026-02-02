# Render Scout [![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)


> This is a single-file C++ library designed to easily obtain virtual methods of graphics objects.

## Status
- Supports **DirectX 9–12**

## Example Usage

```cpp
#define RENDER_SCOUT_IMPLEMENTATION
#include "render_scout.hpp"

int main() 
{
    namespace rs = render_scout;

    rs::VMT d3d9, device;
    auto status = rs::get_d3d9_vmt(&d3d9, &device);
    if (status == rs::Status::Success)
    {
        // get method using predefined method signature
        auto reset = device.get_method<rs::methods::d3d9::reset_vm>();
        
        // using custom method signatures
        using my_present_vm = rs::VMethod<17, rs::methods::d3d9::present_t>;
        auto present = device.get_method<my_present_vm>();

        auto first_method = device.get_method<0, void*>();
    }

    return 0;
}
