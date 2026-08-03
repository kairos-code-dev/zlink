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
    // The crate payload is the only implicit runtime source. In particular,
    // do not discover or prefer a repository `core/build` directory here:
    // that would let a clean consumer silently execute a different Core
    // candidate than the one packaged with this crate.
    println!("cargo:rustc-link-search=native={}", native_dir.display());
    println!("cargo:rustc-link-lib=dylib=zlink");

    if target_os != "windows" {
        println!("cargo:rustc-link-arg=-Wl,-rpath,{}", native_dir.display());
    }
}
