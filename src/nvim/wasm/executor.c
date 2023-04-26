#include "nvim/wasm/executor.h"

/// Initializes global Wasm interpreter, or exits Nvim on failure.
void nwasm_init(char* wasm_file)
{
  M3Result result = m3Err_none;
  IM3Environment env = m3_NewEnvironment();
  if (!env) {
    os_errmsg(_("E???: Failed to initialize wasm interpreter environment\n"));
    os_exit(1);
  }
  IM3Runtime runtime = m3_NewRuntime(env, 1024, NULL);
  if (!runtime) {
    os_errmsg(_("E???: Failed to initialize wasm interpreter runtime\n"));
    os_exit(1);
  }

  if (wasm_file) {
    // read wasm_file
    IM3Module module;
    uint8_t* wasm = NULL;
    size_t wasm_size = 0;
    result = m3_ParseModule(env, &module, wasm, wasm_size);
    if (result) {
      os_errmsg(result);
      os_exit(1);
    }
    result = m3_LoadModule(runtime, module);
    if (result) {
      os_errmsg(result);
      os_exit(1);
    }
  }
}



