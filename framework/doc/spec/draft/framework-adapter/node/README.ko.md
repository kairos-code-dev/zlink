[스펙 목차](../../../README.ko.md)

[Framework Adapter 초안](../README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel](./nestjs-channel-messaging.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md) | [SPOT](./nestjs-spot.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md) | [STREAM](./nestjs-stream.ko.md) | [STREAM open items](./stream-open-items.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [Registry](./nestjs-registry.ko.md)

# Draft -- ZLink Framework For Node.js

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Node.js`와 `NestJS`에서 `ZLink Framework`를 어떤
> 모양으로 노출할지 정리하기 위한 문서다.

## 1. 목적

이 문서는 `Node.js` 바인딩 위에 올라가는 `ZLink Framework`의 `Node.js` 방향을
정리한다. 대표 프레임워크는 `NestJS`로 둔다.

현재 목표는 raw socket 조합을 application 코드로 밀어 올리는 것이 아니라,
module/provider/decorator 구조 안에서 zlink runtime을 자연스럽게 설명하는 것이다.

## 2. 문서 구조와 역할 분담

### 2.1 기준 문서

| 문서 | 역할 |
|------|------|
| [handler-interfaces.ko.md](./handler-interfaces.ko.md) | `Node.js` 공용 interface, decorator, options, context 기준 문서 |

### 2.2 주제 문서

| 문서 | 다루는 범위 |
|------|------------|
| [nestjs-channel-messaging.ko.md](./nestjs-channel-messaging.ko.md) | module 등록, outbound client, request/send handler, dispatch, filter |
| [nestjs-spot.ko.md](./nestjs-spot.ko.md) | `SPOT` lifecycle, publish/subscribe, channel attach |
| [nestjs-stream.ko.md](./nestjs-stream.ko.md) | stream packet/raw handler, registration, dispatch |
| [stream-open-items.ko.md](./stream-open-items.ko.md) | stream 미결 항목 |
| [stage-wrapper-on-spot.ko.md](./stage-wrapper-on-spot.ko.md) | stage 같은 상위 모델을 `SPOT` 위에 감쌀 때의 조건 |
| [nestjs-registry.ko.md](./nestjs-registry.ko.md) | registry startup, query, topology 조회 |

### 2.3 샘플 문서

| 문서 | 다루는 범위 |
|------|------------|
| [channel-messaging-samples.ko.md](./channel-messaging-samples.ko.md) | module 등록, controller, outbound client 샘플 |
| [spot-samples.ko.md](./spot-samples.ko.md) | `SPOT` request/subscribe/publish 샘플 |
| [stream-samples.ko.md](./stream-samples.ko.md) | stream packet/raw handler 샘플 |

## 3. 핵심 방향

- `NestJS` module/provider lifecycle에 맞춘다.
- channel messaging 기본 호출은 `channel name` 기준이다.
- outbound channel은 startup 시점에 등록한다.
- 일반 request/send handler dispatch는 local `ROUTER(server)` ingress 기준이다.
- outbound `DEALER(client)`는 reply correlation 경로로 본다.
- packet key 기본값은 payload constructor 이름 또는 schema 이름을 쓴다.
- `rid` 지정은 `SPOT` spot-to-spot 경로에만 남긴다.
