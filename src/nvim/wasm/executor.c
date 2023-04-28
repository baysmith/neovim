#include "nvim/wasm/executor.h"

#include "klib/kvec.h"

static wasm_State global_wstate;

typedef struct {
  IM3Module module;
  uint8_t* bytes;
  size_t bytes_size;
} wasm_Module;

static kvec_t(wasm_Module) modules;

bool nwasm_exec_file(const char *path);

/// Initializes global Wasm interpreter, or exits Nvim on failure.
void nwasm_init(char* wasm_file)
{
  global_wstate.env = m3_NewEnvironment();
  if (!global_wstate.env) {
    os_errmsg(_("E???: Failed to initialize wasm interpreter environment\n"));
    os_exit(1);
  }
  global_wstate.runtime = m3_NewRuntime(global_wstate.env, 1024, NULL);
  if (!global_wstate.runtime) {
    os_errmsg(_("E???: Failed to initialize wasm interpreter runtime\n"));
    os_exit(1);
  }

  kv_init(modules);

  if (wasm_file) {
    nwasm_exec_file(wasm_file);
  }
}

bool nwasm_exec_file(const char *path)
{
  if (path) {
    wasm_Module m = { .module = NULL, .bytes = NULL, .bytes_size = 0};
    kv_push(modules, m);
    wasm_Module* module = &kv_last(modules);

    FileInfo file_info;
    if (!os_fileinfo(path, &file_info)) {
      semsg(_("E?: Can't get file info for file %s"), path);
      return false;
    }
    module->bytes_size = os_fileinfo_size(&file_info);
    module->bytes = xmalloc(module->bytes_size);
    FileDescriptor fp;
    int error;
    if ((error = file_open(&fp, path, kFileReadOnly, 0))) {
      semsg(_("E?: Can't open file %s for reading: %s"), path, os_strerror(error));
      return false;
    }
    error = file_read(&fp, module->bytes, module->bytes_size);
    if (error < 0) {
      semsg(_("E?: Error reading file %s: %s"), path, os_strerror(error));
      return false;
    }

    if ((error = file_close(&fp, false)) != 0) {
      semsg(_("E?: Error when closing file %s: %s"), path, os_strerror(error));
    }

    M3Result result = m3Err_none;
    result = m3_ParseModule(global_wstate.env, &module->module, module->bytes, module->bytes_size);
    if (result) {
      os_errmsg(result);
      return false;
    }
    nwasm_add_api_functions(&global_wstate, module->module);
    result = m3_LoadModule(global_wstate.runtime, module->module);
    if (result) {
      os_errmsg(result);
      return false;
    }

    IM3Function start;
    result = m3_FindFunction(&start, global_wstate.runtime, "start");
    if (result) {
      os_errmsg(result);
      return false;
    }

    result = m3_CallV(start);
    if (result) {
      os_errmsg(result);
      return false;
    }

    uint32_t value = 0;
    uint32_t value2 = 0;
    result = m3_GetResultsV(start, &value, &value2);
    if (result) {
      os_errmsg(result);
      return false;
    }

    printf("Result: %d, %d\n", value, value2);

    IM3Function ret_str;
    result = m3_FindFunction(&ret_str, global_wstate.runtime, "ret_str_2");
    if (result) {
      os_errmsg(result);
      return false;
    }

    result = m3_CallV(ret_str);
    if (result) {
      os_errmsg(result);
      return false;
    }

    uint32_t ref = 0;
    uint32_t len = 0;
    result = m3_GetResultsV(ret_str, &ref, &len);
    if (result) {
      os_errmsg(result);
      return false;
    }

    uint32_t mem_len;
    uint8_t* mem = m3_GetMemory(global_wstate.runtime, &mem_len, 0);

    printf("Result: %d, %s\n", len, (char*)(mem + ref));
  }
  return true;
}


