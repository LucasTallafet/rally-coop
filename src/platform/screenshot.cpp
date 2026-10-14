#include "platform/screenshot.h"

#include <raylib.h>
#include <rlgl.h>

namespace rally::platform {

void save_screenshot() {
    rlDrawRenderBatchActive();
    int number = 0;
    // TakeScreenshot guarda en la carpeta del exe.
    while (FileExists(TextFormat("%s/captura_%03d.png", GetApplicationDirectory(), number))) {
        ++number;
    }
    TakeScreenshot(TextFormat("captura_%03d.png", number));
}

void save_screenshot_to(const std::string& path) {
    rlDrawRenderBatchActive();
    // TakeScreenshot se queda solo con el nombre del archivo: la imagen se guarda a mano.
    Image image = LoadImageFromScreen();
    ExportImage(image, path.c_str());
    UnloadImage(image);
}

} // namespace rally::platform
