# C++ Stream Connector

C++ STREAM client connector 제품군의 문서 진입점이다. **네이티브 빌드 게임 엔진**(Unreal,
Godot GDExtension, Axmol), 일반 C++ 애플리케이션, 서버 e2e/perf client가 대상이다.

| 문서 | 내용 |
|------|------|
| [가이드 INDEX](guide/INDEX.ko.md) | 개요, 시작하기, 옵션, 송수신, lifecycle, 엔진 어댑터, 패키징, 성능 |
| [core — async runtime](core/guide/async-runtime.ko.md) | no-exception·no-coroutine core runtime |
| [e2e-client — coroutine client](e2e-client/guide/coroutine-client.ko.md) | 서버 e2e/perf용 coroutine helper |
| [Stream Connector 공통 스펙](../../framework/spec/stream-connector/32-stream-connector.ko.md) | **정본** — 대상 환경, transport, wire 계약 |

> **웹(WASM) 빌드에는 이 connector를 쓸 수 없다.** Cocos Creator web과 Godot Web은
> [TypeScript connector](../node/README.ko.md)를 사용한다.
