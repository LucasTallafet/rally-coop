#pragma once

namespace rally::io {

// Commit del código con el que se compiló (12 caracteres), con "-dirty" si había cambios
// sin commit, o "desconocido" sin git. Se calcula en cada build (cmake/GitInfo.cmake), no
// solo al configurar.
const char* build_commit();

} // namespace rally::io
