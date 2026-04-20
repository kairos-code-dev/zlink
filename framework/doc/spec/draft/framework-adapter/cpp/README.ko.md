[스펙 목차](../../../README.ko.md)

[Framework Adapter 초안](../README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel](./cpp-channel-messaging.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md) | [SPOT](./cpp-spot.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md) | [STREAM](./cpp-stream.ko.md) | [STREAM open items](./stream-open-items.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [Registry](./cpp-registry.ko.md)

# Draft -- ZLink Framework For C++

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++`에서 `ZLink Framework`를 어떤 모양으로 노출할지
> 정리하기 위한 문서다.

## 1. 목적

이 문서는 `C++` 바인딩 위에 올라가는 `ZLink Framework`의 `C++` 방향을 정리한다.
`C++`는 기존 대표 웹 프레임워크 위 adapter보다, zlink framework가 host/runtime
역할 일부를 직접 제공하는 standalone 형태로 설명하는 편이 맞다.

## 2. 문서 구조와 역할 분담

### 2.1 기준 문서

| 문서 | 역할 |
|------|------|
| [handler-interfaces.ko.md](./handler-interfaces.ko.md) | `C++` host/runtime이 노출할 공용 타입, registry, options 기준 문서 |

### 2.2 주제 문서

| 문서 | 다루는 범위 |
|------|------------|
| [cpp-channel-messaging.ko.md](./cpp-channel-messaging.ko.md) | app host, channel 등록, dispatch loop, outbound client |
| [cpp-spot.ko.md](./cpp-spot.ko.md) | `SPOT` runtime, publish/subscribe, spot-to-spot |
| [cpp-stream.ko.md](./cpp-stream.ko.md) | stream packet/raw handler와 poll loop 통합 |
| [stream-open-items.ko.md](./stream-open-items.ko.md) | stream 미결 항목 |
| [stage-wrapper-on-spot.ko.md](./stage-wrapper-on-spot.ko.md) | stage 같은 상위 모델을 `SPOT` 위에 감쌀 때의 조건 |
| [cpp-registry.ko.md](./cpp-registry.ko.md) | embedded registry, query, topology 조회 |

### 2.3 샘플 문서

| 문서 | 다루는 범위 |
|------|------------|
| [channel-messaging-samples.ko.md](./channel-messaging-samples.ko.md) | host bootstrap, handler registry, outbound client 샘플 |
| [spot-samples.ko.md](./spot-samples.ko.md) | `SPOT` request/subscribe/publish 샘플 |
| [stream-samples.ko.md](./stream-samples.ko.md) | stream packet/raw handler 샘플 |

## 3. 핵심 방향

- zlink framework가 application host/runtime 역할 일부를 직접 제공한다.
- channel messaging 기본 호출은 `channel name` 기준이다.
- outbound channel은 startup 시점에 등록한다.
- 일반 request/send handler dispatch는 local `ROUTER(server)` ingress 기준이다.
- outbound `DEALER(client)`는 reply correlation 경로로 본다.
- packet key 기본값은 payload 타입 이름을 쓴다.
- `rid` 직접 지정은 `SPOT` spot-to-spot 경로에만 남긴다.
