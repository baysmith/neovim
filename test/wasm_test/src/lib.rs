use std::ffi::c_char;

#[no_mangle]
pub fn start() -> (i32, i32) {
    (42, 64)
}

static MSG: &str = "Hello from wasm\0";
#[no_mangle]
pub fn ret_str() -> (usize, *const c_char) {
    (MSG.len(), MSG.as_ptr() as *const c_char)
}

#[no_mangle]
pub fn ret_str_2() -> &'static str {
    "Hello again\0"
}
