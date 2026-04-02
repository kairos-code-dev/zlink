# zlink Rust Binding API Reference

This reference is generated from the Rust source in `bindings/rust/src/`.

## Generate

```bash
cd bindings/rust
cargo doc --no-deps
```

Generated HTML entrypoint:

```text
bindings/rust/target/doc/zlink/index.html
```

## Scope

- Public API of the `zlink` crate
- Socket types in `zlink::socket`
- Monitor types in `zlink::monitor`
- Service types in `zlink::service`
- Domain objects (`Message`, error types, enums)
- FFI internals (`zlink::ffi`) are private and excluded
