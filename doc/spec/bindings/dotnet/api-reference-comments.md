# .NET API Reference Comments

This document defines how XML documentation comments are used for the .NET
binding public API reference.

XML comments in `bindings/dotnet/src/Zlink/Contracts/` are contract text. Every
publicly visible type and member in that folder must have XML documentation.
The comments must describe the public behavior a caller needs to use the API
correctly, and they must stay aligned with `core/include/zlink.h` and the
binding contract documents.

## Scope

All public contract members require XML comments. Give extra attention to
members that expose one of these contract decisions:

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

The main `Zlink.csproj` must not suppress `CS1591`. A clean rebuild with XML
documentation warnings enabled is the completeness gate for the contract
assembly. Codec projects may keep their own policy because they are separate
packages.

## Comment Shape

- Write XML comments in English.
- Keep summaries short and caller-focused.
- For simple enum values and DTO fields, a concise one-line summary is enough.
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

- Does `dotnet build bindings/dotnet/src/Zlink/Zlink.csproj --no-restore -t:Rebuild`
  report zero XML documentation warnings?
- Can a caller tell who owns every returned `Message`?
- Is it clear whether a buffer view is borrowed or copied?
- Are false, timeout, cancellation, and callback paths described?
- Does the comment avoid runtime-internal details?
- Does the text match the generated API reference target?
