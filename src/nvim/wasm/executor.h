#ifndef NVIM_WASM_EXECUTOR_H
#define NVIM_WASM_EXECUTOR_H

#include "wasm3/source/wasm3.h"
#include <stdbool.h>

#include "nvim/api/private/defs.h"
#include "nvim/api/private/helpers.h"
#include "nvim/assert.h"
#include "nvim/eval/typval.h"
#include "nvim/ex_cmds_defs.h"
#include "nvim/func_attr.h"
#include "nvim/wasm/converter.h"
#include "nvim/macros.h"
#include "nvim/types.h"
#include "nvim/usercmd.h"

struct wasm_State {
  IM3Runtime runtime;
  IM3ImportContext ctx;
  uint64_t *sp;
  void *mem;
};

#define wasm_Function(WRAP_NAME, NAME) m3ApiRawFunction(WRAP_NAME) { wasm_State state = {.runtime = runtime, .ctx = _ctx, .sp = _sp, .mem = _mem}; return NAME(&state); }

#endif  // NVIM_WASM_EXECUTOR_H

