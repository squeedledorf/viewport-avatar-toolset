// Viewport Avatar Toolset - registering .vat (and .anim) files with the desktop.
// Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
//
// Spec: docs/spec/03 IO-51, docs/spec/06 section 5. Per user only; nothing needs admin rights.
#pragma once

#include <string>

namespace vats {

// packaging_dir holds viewport-avatar-toolset.desktop and .xml (Linux). Returns false with a reason.
bool register_file_types(const std::string& packaging_dir, std::string& message);
bool unregister_file_types(std::string& message);

}  // namespace vats
