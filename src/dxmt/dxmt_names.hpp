#pragma once

#include <ostream>
#include "dxgi.h"
#include "d3d11.h"
#include "Metal/MTLPixelFormat.hpp"

namespace MTL {
     std::ostream& operator << (std::ostream& os, PixelFormat e);
}

std::ostream& operator << (std::ostream& os, DXGI_FORMAT e);
std::ostream& operator << (std::ostream& os, D3D_FEATURE_LEVEL e);
std::ostream& operator << (std::ostream& os, D3D11_RESOURCE_DIMENSION e);