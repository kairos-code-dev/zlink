<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [다음: Draft -- ZLink Framework Python Channel Messaging Samples](./channel-messaging-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Framework Adapter 정책](../../../../doc/spec/README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel](./fastapi-channel-messaging.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md) | [SPOT](./fastapi-spot.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md) | [STREAM](./fastapi-stream.ko.md) | [STREAM open items](./stream-open-items.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [Monitoring](./fastapi-monitoring.ko.md) | [Registry](./fastapi-registry.ko.md)

# Draft -- ZLink Framework For Python

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Python`과 `FastAPI`에서 `ZLink Framework`를 어떤
> 모양으로 노출할지 정리하기 위한 문서다.

## 1. 목적

이 문서는 `Python` 바인딩 위에 올라가는 `ZLink Framework`의 `Python` 방향을
정리한다. 대표 프레임워크는 `FastAPI`로 둔다.

현재 목표는 `FastAPI`의 dependency, startup/shutdown hook, async handler 모델과
zlink runtime을 자연스럽게 묶는 것이다.

## 1.1 공통 정책 적용

이 디렉토리의 모든 문서는
[Framework Adapter 정책](../../../../doc/spec/README.ko.md)과 그 하위 문서를 그대로 따른다.
즉 `Python` 상세 문서는 공통 의미를 다시 정의하지 않고, 그 의미를 `Python`과
`FastAPI` 표면으로만 구체화한다.

특히 아래 항목은 이 디렉토리 전체에 공통으로 적용된다.

- 네이밍 규칙은
  [doc/spec/bindings/README.md](/home/hep7/project/kairos/zlink/doc/spec/bindings/README.md)의
  `Naming Policy`를 그대로 따른다. `Python` 문서에서는 모든 public API를
  `snake_case`로 적는다.
- 수동 연결은 `channel + capability` 또는 `spot node + capability` 단위로
  설명한다. 같은 capability 안에서는 `Discovery`와 manual 연결을 섞지 않는다.
- send/publish의 async submit과 backpressure 의미는
  [framework 공통 비동기 정책](../../../../doc/spec/async-execution-policy.ko.md)을
  따른다.
- `SPOT`을 지원하는 문서는 named spot factory 등록, `spot_name` 기준 생성,
  `spot_rid -> spot_name` 조회, lifecycle timer, 외부 spot publish 표면을
  공통 정책과 맞춰 설명해야 한다.
- monitoring을 지원하는 문서는 socket/discovery/registry/spot runtime event를
  typed event와 등록 표면으로 설명해야 한다.

## 2. 문서 구조와 역할 분담

### 2.1 기준 문서

| 문서 | 역할 |
|------|------|
| [handler-interfaces.ko.md](./handler-interfaces.ko.md) | `Python` 공용 protocol, context, options, decorator 기준 문서 |

### 2.2 주제 문서

| 문서 | 다루는 범위 |
|------|------------|
| [fastapi-channel-messaging.ko.md](./fastapi-channel-messaging.ko.md) | channel 등록, outbound client dependency, handler 모델, dispatch |
| [fastapi-spot.ko.md](./fastapi-spot.ko.md) | `SPOT` lifecycle, publish/subscribe, channel attach |
| [fastapi-stream.ko.md](./fastapi-stream.ko.md) | stream packet/raw session, registration, lifecycle |
| [stream-open-items.ko.md](./stream-open-items.ko.md) | stream 미결 항목 |
| [fastapi-monitoring.ko.md](./fastapi-monitoring.ko.md) | runtime monitoring 등록, typed event, 운영 샘플 |
| [stage-wrapper-on-spot.ko.md](./stage-wrapper-on-spot.ko.md) | stage 같은 상위 모델을 `SPOT` 위에 감쌀 때의 조건 |
| [fastapi-registry.ko.md](./fastapi-registry.ko.md) | registry startup, in-process query, remote query |

### 2.3 샘플 문서

| 문서 | 다루는 범위 |
|------|------------|
| [channel-messaging-samples.ko.md](./channel-messaging-samples.ko.md) | app 등록, route handler, outbound client 샘플 |
| [spot-samples.ko.md](./spot-samples.ko.md) | `SPOT` request/subscribe/publish 샘플 |
| [stream-samples.ko.md](./stream-samples.ko.md) | stream packet/raw session 샘플 |

## 3. 핵심 방향

- `FastAPI` dependency 와 startup/shutdown hook에 맞춘다.
- channel messaging 기본 호출은 `channel name` 기준이다.
- channel capability는 startup 시점에 등록한다.
- 일반 request/send handler dispatch는 local `ROUTER(server)` ingress 기준이다.
- outbound `DEALER(client)`는 reply correlation 경로로 본다.
- packet key 기본값은 payload 타입 이름을 쓴다.
- `rid` 지정은 `SPOT` spot-to-spot 경로에만 남긴다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../doc/README.ko.md) | [다음: Draft -- ZLink Framework Python Channel Messaging Samples](./channel-messaging-samples.ko.md)
<!-- framework-adapter-nav:bottom:end -->
