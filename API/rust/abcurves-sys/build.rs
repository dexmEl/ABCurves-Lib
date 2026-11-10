use std::{env, path::PathBuf};
fn main() {
    println!("cargo:rerun-if-env-changed=ABCURVES_LIB_DIR");
    println!("cargo:rerun-if-changed=src/lib.rs");
    if env::var_os("CARGO_FEATURE_DYNAMIC").is_some()
        && env::var_os("CARGO_FEATURE_LINKED").is_none()
        && env::var("CARGO_CFG_TARGET_OS").unwrap() == "windows" {
        generate_dynamic();
        return;
    }
    let root = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap()).join("../../..");
    let directory = env::var_os("ABCURVES_LIB_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            root.join(if cfg!(target_os = "windows") {
                "build/api/Release"
            } else {
                "build/api"
            })
        });
    println!("cargo:rustc-link-search=native={}", directory.display());
    println!("cargo:rustc-link-lib=dylib=abcurves");
}

// Generate forwarding functions from the same declarations as the linked ABI.
// This keeps argument types in one place while avoiding an import library.
fn generate_dynamic() {
    let source = std::fs::read_to_string("src/lib.rs").unwrap();
    let mut output = String::from(r#"
mod dynamic_loader {
    use std::{cell::Cell, ffi::{c_char, c_void}, os::windows::ffi::OsStrExt, sync::OnceLock};
    #[link(name = "kernel32")]
    extern "system" {
        fn LoadLibraryW(name: *const u16) -> *mut c_void;
        fn GetProcAddress(module: *mut c_void, name: *const c_char) -> *mut c_void;
    }
    static MODULE: OnceLock<usize> = OnceLock::new();
    thread_local! { static MISSING: Cell<bool> = const { Cell::new(false) }; }
    pub fn missing() -> bool { MISSING.with(Cell::get) }
    pub fn set_missing(value: bool) { MISSING.with(|v| v.set(value)); }
    pub fn symbol(name: &'static [u8]) -> usize {
        let module = *MODULE.get_or_init(|| {
            let path = std::env::var_os("ABCURVES_LIBRARY").unwrap_or_else(|| "abcurves.dll".into());
            let wide: Vec<u16> = path.encode_wide().chain(Some(0)).collect();
            unsafe { LoadLibraryW(wide.as_ptr()) as usize }
        });
        if module == 0 { return 0; }
        unsafe { GetProcAddress(module as *mut c_void, name.as_ptr().cast()) as usize }
    }
}
"#);
    let mut count = 0;
    for block in source.split("extern \"C\" {").skip(1) {
        let block = block.split('}').next().unwrap();
        for declaration in block.split(';').map(str::trim).filter(|s| s.starts_with("pub fn ")) {
            count += 1;
            let signature = declaration.strip_prefix("pub fn ").unwrap();
            let open = signature.find('(').unwrap();
            let close = signature.find(')').unwrap();
            let name = signature[..open].trim();
            let arguments = &signature[open + 1..close];
            let result = signature[close + 1..].trim().strip_prefix("->").unwrap().trim();
            let args: Vec<_> = arguments.split(',').map(str::trim).filter(|s| !s.is_empty()).collect();
            let names: Vec<_> = args.iter().map(|arg| arg.split(':').next().unwrap().trim()).collect();
            let types: Vec<_> = args.iter().map(|arg| arg.split_once(':').unwrap().1.trim()).collect();
            let fallback = if result == "Status" { "7" } else { "b\"ABCurves DLL or required symbol not found\\0\".as_ptr().cast()" };
            if name == "abc_last_error" {
                output.push_str("pub unsafe fn abc_last_error() -> *const c_char { if dynamic_loader::missing() { return b\"ABCurves DLL or required symbol not found; set ABCURVES_LIBRARY to its full path\\0\".as_ptr().cast(); }\n");
            } else {
                output.push_str(&format!("pub unsafe fn {name}({}) -> {result} {{\n", args.join(", ")));
                output.push_str("dynamic_loader::set_missing(false);\n");
            }
            output.push_str(&format!("static FUNCTION: std::sync::OnceLock<usize> = std::sync::OnceLock::new();\nlet address = *FUNCTION.get_or_init(|| dynamic_loader::symbol(b\"{name}\\0\"));\nif address == 0 {{ dynamic_loader::set_missing(true); return {fallback}; }}\n"));
            output.push_str(&format!("let function: unsafe extern \"C\" fn({}) -> {result} = std::mem::transmute(address);\nfunction({})\n}}\n", types.join(", "), names.join(", ")));
        }
    }
    assert_eq!(count, 55, "dynamic forwarding generator did not read all ABI declarations");
    std::fs::write(PathBuf::from(env::var_os("OUT_DIR").unwrap()).join("dynamic.rs"), output).unwrap();
}
