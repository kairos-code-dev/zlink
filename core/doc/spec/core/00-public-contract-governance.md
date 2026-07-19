[한국어](00-public-contract-governance.ko.md) | English

[Specification index](../README.md) · [Core index](README.md)

# Core public-contract governance

This document defines the sources, documentation responsibilities, and change procedure for the ZLink Core 10.1.0 public contract. Its audience is developers who design, implement, and review the public Core C ABI. It answers: “How are the formal specification, public headers, tests, and packages kept consistent?”

## 1. Contract sources

The formal specifications under `core/doc/spec/` define Core 10.1.0 public behavior. `core/include/zlink.h` and the domain headers it includes are the C ABI expression of the same contract. The implementation, contract tests, bindings, and installed packages must agree with both expressions.

The formal specification is not reduced for implementation convenience. A mismatch between headers and specification prevents completion, and neither side is retained as a separate compatibility layer.

## 2. Documentation responsibilities

Formal specifications define function signatures, types and constants, results and errno, ownership, timeout, thread safety, callbacks, and close semantics. Guides own purpose and application examples. Internals describe socket wiring, queues, locks, threads, and protocol codecs only after implementation is confirmed.

Formal specifications, guides, and internals describe only the current Core 10.1.0 contract and structure. Navigation from a formal document points only to formal indexes and public-contract owners.

## 3. Change procedure

A public-contract change follows this order:

1. Record the exact 10.1.0 contract in the Korean and English formal specifications.
2. Review functions, types, enums, constants, results, ownership, and thread-safety tables.
3. Align public headers, exported symbols, and implementation with the formal specification.
4. Verify the public surface with contract tests and bindings-package snapshots.
5. Update internals after the implementation and structural tests are confirmed.
6. Reverify parity among Korean, English, headers, and packages.

## 4. Korean and English parity

The Korean and English documents have the same heading order, C signatures, enum and struct fields and numeric values, defaults, results, ownership, thread-safety rules, and link targets. A public contract present in only one language is not a valid Core 10.1.0 contract.
