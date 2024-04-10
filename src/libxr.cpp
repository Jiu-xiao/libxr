#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"

LibXR::Callback<void, const char *, uint32_t>
    LibXR::Assert::libxr_fatal_error_callback;

LibXR::ReadFunction LibXR::STDIO::read;
LibXR::WriteFunction LibXR::STDIO::write;
