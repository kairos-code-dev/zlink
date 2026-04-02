# zlink Node Binding API Reference

This reference is generated from the TypeScript source in `bindings/node/src/`.

## Prerequisites

```bash
npm install --save-dev typedoc
```

## Generate

```bash
cd bindings/node
npx typedoc
```

Generated HTML entrypoint:

```text
bindings/node/typedoc/html/index.html
```

## Scope

- Public exports from `src/index.ts`
- Socket types and domain objects
- Message and result types (`Message`, `Received`, `SendResult`)
- Constants and enums (`SocketType`, `SocketOption`)
- Internal symbols marked `@internal` are excluded
