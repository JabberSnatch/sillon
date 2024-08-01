#include "_voxels.hh"

#define ZIPTK_IMPLEMENTATION
#define MCTK_IMPLEMENTATION
#include "mctk/mctk.hh"

namespace Voxels
{

Interface::Interface()
    : ConstantResources{ nullptr }
    , RenderResources{ nullptr }
    , Update{ nullptr }
    , Draw{ nullptr }
    , Shutdown{ nullptr }
    , state{ nullptr }
{}

Interface::~Interface()
{}

}
