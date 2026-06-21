#include "ModernSceneLoop.hpp"

int main(int, char**)
{
    ManualEngine::App::ModernSceneLaunchOptions options;
    options.windowTitle = "ManualEngine";
    options.debugSceneModeLabel = "Modern default scene";
    options.editorMode = false;
    return ManualEngine::App::runModernSceneApp(options);
}
