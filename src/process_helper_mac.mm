// macOS CEF subprocess (helper) entry point.
//
// On macOS, CEF launches its subprocesses (renderer, GPU, utility, etc.) as
// separate Helper .app bundles rather than re-executing the main application
// (as it does on Linux). Each Helper loads the CEF framework dynamically and
// then hands control to CefExecuteProcess.
//
// A null CefApp is passed here: brow6el's BrowserApp only implements
// browser-process handlers, so the subprocesses need no application object
// (this matches the upstream cefsimple helper on macOS).

#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"

int main(int argc, char *argv[]) {
  // Load the CEF framework from the Helper app bundle.
  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInHelper()) {
    return 1;
  }

  CefMainArgs main_args(argc, argv);
  return CefExecuteProcess(main_args, nullptr, nullptr);
}
