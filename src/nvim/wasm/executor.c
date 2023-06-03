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
    smsg(_("Wasm exec file: \"%s\""), wasm_file);
    nwasm_exec_file(wasm_file);
  }
}

static void nwasm_print_event(void **argv)
{
  char *str = argv[0];
  const size_t len = (size_t)(intptr_t)argv[1] - 1;  // exclude final NUL

  for (size_t i = 0; i < len;) {
    if (got_int) {
      break;
    }
    const size_t start = i;
    while (i < len) {
      switch (str[i]) {
      case NUL:
        str[i] = NL;
        i++;
        continue;
      case NL:
        // TODO(baysmith): use proper multiline msg? Probably should implement
        // print() in nwasm in terms of nvim_message(), when it is available.
        str[i] = NUL;
        i++;
        break;
      default:
        i++;
        continue;
      }
      break;
    }
    msg(str + start);
  }
  if (len && str[len - 1] == NUL) {  // Last was newline
    msg("");
  }
  xfree(str);
}

typedef struct
{
    uint32_t buf;
    uint32_t buf_len;
} wasm_strings;

m3ApiRawFunction(nwasm_print_strings)
{
  m3ApiGetArgMem(wasm_strings*, strs);
  m3ApiGetArg(uint32_t, strs_len);

  garray_T msg_ga;
  ga_init(&msg_ga, 1, 80);

  ssize_t res = 0;
  for (uint32_t i = 0; i < strs_len; ++i) {
    void* str = m3ApiOffsetToPtr(m3ApiReadMem32(&strs[i].buf));
    size_t len = m3ApiReadMem32(&strs[i].buf_len);
    if (len == 0) continue;
    m3ApiCheckMem(str, len);
    ga_concat_len(&msg_ga, str, len);
    if (i > 0) {
      ga_append(&msg_ga, ' ');
    }
  }
  ga_append(&msg_ga, NUL);
  nwasm_print_event((void *[]){ msg_ga.ga_data,
    (void *)(intptr_t)msg_ga.ga_len });

  m3ApiSuccess();
}

m3ApiRawFunction(nwasm_print)
{
  m3ApiGetArgMem(char*, str);
  m3ApiGetArg(uint32_t, str_len);
  char* s = xmallocz(str_len + 1);
  memcpy(s, str, str_len);
  s[str_len] = '\0';
  msg(s);
  xfree(s);
  m3ApiSuccess();
}


m3ApiRawFunction(bas_msg)
{
  m3ApiGetArgMem(char*, str);
  m3ApiGetArg(int32_t, str_len);
  if (str_len > 0) {
    char* c_str = malloc(str_len + 1);
    memcpy(c_str, str, str_len);
    c_str[str_len] = '\0';
    printf("%s\n", c_str);
    free(c_str);
    m3ApiSuccess();
  }
  fprintf(stderr, "invalid str_len\n");
  exit(1);
}

m3ApiRawFunction(nwasm_semsg);
m3ApiRawFunction(nwasm_semsg)
{
  m3ApiGetArgMem(char*, str);
  m3ApiGetArg(int32_t, str_len);
  char* msg = xmallocz(str_len + 1);
  memcpy(msg, str, str_len);
  semsg(_("E?: %s"), msg);
  xfree(msg);
  return m3Err_none;
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

    smsg(_("Wasm parsing module"));
    M3Result result = m3Err_none;
    result = m3_ParseModule(global_wstate.env, &module->module, module->bytes, module->bytes_size);
    if (result) {
      semsg(_("E?: Error parsing wasm module %s: %s"), path, result);
      os_errmsg(result);
      return false;
    }
    result = m3_LoadModule(global_wstate.runtime, module->module);
    if (result) {
      semsg(_("E?: Error loading wasm module %s: %s"), path, result);
      os_errmsg(result);
      return false;
    }

    m3_LinkRawFunction(module->module, "nvim_api", "print", "v(*i)", nwasm_print);
    m3_LinkRawFunction(module->module, "nvim_api", "print_strings", "v(*i)", nwasm_print_strings);
    m3_LinkRawFunction(module->module, "bas", "msg", "v(*i)", bas_msg);
    m3_LinkRawFunction(module->module, "nvim_api", "nvim_semsg", "v(*i)", nwasm_semsg);
    smsg(_("Wasm linked nvim_semsg function"));
    smsg("");
    /* nwasm_add_api_functions(&global_wstate, module->module); */

    IM3Function start;
    result = m3_FindFunction(&start, global_wstate.runtime, "_start");
    if (result) {
      semsg(_("E?: Error loading finding _start function %s: %s"), path, result);
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
  }
  printf("nwasm_exec_file done\n");
  return true;
}


