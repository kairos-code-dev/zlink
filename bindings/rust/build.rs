use std::env;
use std::path::PathBuf;

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());

    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap();
    let target_arch = env::var("CARGO_CFG_TARGET_ARCH").unwrap();

    let (os_dir, arch_dir) = match (target_os.as_str(), target_arch.as_str()) {
        ("linux", "x86_64") => ("linux", "x86_64"),
        ("linux", "aarch64") => ("linux", "aarch64"),
        ("macos", "x86_64") => ("darwin", "x86_64"),
        ("macos", "aarch64") => ("darwin", "aarch64"),
        ("windows", "x86_64") => ("windows", "x86_64"),
        ("windows", "aarch64") => ("windows", "aarch64"),
        (os, arch) => panic!("unsupported platform: {os}-{arch}"),
    };

    let native_dir = manifest_dir
        .join("native")
        .join(format!("{os_dir}-{arch_dir}"));
    let repo_root = manifest_dir
        .parent()
        .and_then(|path| path.parent())
        .expect("bindings/rust should live under the repo root");
    let core_build_lib_dir = repo_root.join("core").join("build").join("lib");
    let link_dir = if core_build_lib_dir
        .join(if target_os == "windows" {
            "zlink.dll"
        } else if target_os == "macos" {
            "libzlink.dylib"
        } else {
            "libzlink.so"
        })
        .exists()
    {
        core_build_lib_dir
    } else {
        native_dir
    };

    println!("cargo:rustc-link-search=native={}", link_dir.display());
    println!("cargo:rustc-link-lib=dylib=zlink");

    if target_os != "windows" {
        println!("cargo:rustc-link-arg=-Wl,-rpath,{}", link_dir.display());
    }
}
