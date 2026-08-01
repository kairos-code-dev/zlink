# zlink Guide

Welcome to the zlink documentation. zlink is a modern messaging library
based on libzmq with Boost.Asio I/O, TLS transport, and a streamlined
socket API.

## Getting Started

- [Overview](guide/01-overview.md) — Architecture and key differences from libzmq
- [Core API](guide/02-core-api.md) — Context, socket, and message fundamentals

## Socket Patterns

- [PAIR](guide/03-1-pair.md) — 1:1 bidirectional
- [PUB/SUB](guide/03-2-pubsub.md) — Topic-based publish/subscribe
- [DEALER](guide/03-3-dealer.md) — Asynchronous request
- [ROUTER](guide/03-4-router.md) — Routing
- [STREAM](guide/03-5-stream.md) — Raw TCP communication

## Bindings

- [Language bindings](bindings/spec/README.md) — .NET · C++ · Java · Node.js · Python · Go · Rust

## Code Examples

All code examples are shown with language tabs. Select your language once
and all examples on the page (and across pages) will switch automatically.
