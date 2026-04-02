# zlink .NET Binding API Reference

This reference is generated from the C# source in `bindings/dotnet/src/Zlink/`.

## Prerequisites

```bash
dotnet tool install -g docfx
```

## Generate

```bash
cd bindings/dotnet
docfx docfx.json
```

Generated HTML entrypoint:

```text
bindings/dotnet/_site/index.html
```

## Scope

- Public types in the `Zlink` namespace
- Socket types, message types, domain objects
- Service wrappers in `Zlink.Service`
- Internal/native types (`Zlink.Native`) are excluded
