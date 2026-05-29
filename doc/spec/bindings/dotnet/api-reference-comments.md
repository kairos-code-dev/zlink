# .NET API Reference Comments

This document defines how XML documentation comments are used for the .NET
binding public API reference.

XML comments in `bindings/dotnet/src/Zlink/Contracts/` are contract text. They
must describe the public behavior a caller needs to use the API correctly, and
they must stay aligned with `core/include/zlink.h` and the binding contract
documents.

## Scope

Add XML comments when a public member exposes one of these contract decisions:

- Ownership, disposal, or transfer of `Message` and `Received` payloads.
- Borrowed views versus copied buffers.
- Blocking, timeout, cancellation, callback, or non-blocking result behavior.
- Meaning of boolean return values and result enums.
- Staged operation-builder flow where the next valid call is part of the
  contract.
- Exceptions that callers can avoid by following the contract.

Do not document private implementation mechanics in public XML comments.
Native handles, polling strategy, wire encoding, and progress-pump details
belong in runtime comments or `doc/internals/`.

## Comment Shape

- Write XML comments in English.
- Keep summaries short and caller-focused.
- Use `<remarks>` only for contract details that do not fit in one summary.
- Prefer precise ownership words: owns, borrows, copies, transfers, disposes.
- Do not repeat the method name in prose unless it clarifies overload behavior.
- Do not describe current implementation shortcuts as public guarantees.

## Separation From Guides

API reference comments are not tutorials. They should explain the exact contract
of one type or member. Usage patterns, examples, and motivation belong in
`doc/guide/`.

If a public member needs long background explanation, keep the XML comment short
and link from the guide or binding README to the appropriate guide page.

## Review Checklist

When a public contract changes, review the XML comments with the code:

- Can a caller tell who owns every returned `Message`?
- Is it clear whether a buffer view is borrowed or copied?
- Are false, timeout, cancellation, and callback paths described?
- Does the comment avoid runtime-internal details?
- Does the text match the generated API reference target?
