<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Framework Adapter 정책](../../../../doc/spec/README.ko.md) | [공통 API](../../../../doc/spec/framework-api.ko.md) | [상호작용 모델](../../../../doc/spec/interaction-model.ko.md) | [메시지 모델](../../../../doc/spec/message-model.ko.md) | [channel topology](../../../../doc/spec/channel-topology.ko.md)

# Draft -- ZLink Framework For Rust

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Rust`와 `Axum`에서 `ZLink Framework`를 어떤 모양으로
> 노출할지 정리하기 위한 진입 문서다.

## 1. 목적

이 문서는 `Rust` 바인딩 위에 올라가는 `ZLink Framework`의 `Rust` 방향을
정리한다. 현재 대표 프레임워크는 `Axum`으로 둔다.

이 기준을 고른 이유는 아래와 같다.

- extractor와 state 개념이 handler context와 잘 맞는다.
- `tower` middleware와 runtime lifecycle을 설명하기 쉽다.
- async 중심 client와 background task를 자연스럽게 넣을 수 있다.

## 1.1 공통 정책 적용

이 디렉토리의 모든 문서는
[Framework Adapter 정책](../../../../doc/spec/README.ko.md)과 그 하위 문서를 그대로 따른다.
즉 `Rust` 상세 문서는 공통 의미를 다시 정의하지 않고, 그 의미를 `Rust` 표면으로만
구체화한다.

특히 아래 항목은 이 디렉토리 전체에 공통으로 적용된다.

- 네이밍 규칙은
  [doc/spec/bindings/README.md](/home/hep7/project/kairos/zlink/doc/spec/bindings/README.md)의
  `Naming Policy`를 그대로 따른다. `Rust` 문서에서는 메서드는 `snake_case`,
  타입은 `PascalCase`를 쓰고, overloading이 없을 때만 `send_to` 수준의 최소
  접미사를 허용한다.
- 수동 연결은 `channel + capability` 또는 `spot node + capability` 단위로
  설명한다. 같은 capability 안에서는 `Discovery`와 manual 연결을 섞지 않는다.
- send/publish는 기본 async submit으로 설명한다. backpressure는 public
  non-blocking 옵션이 아니라 framework 내부의 nonblocking send, pending queue,
  ready notification으로 처리한다.
- `SPOT`을 지원하는 문서는 named spot factory 등록, `spot_name` 기준 생성,
  `spot_rid -> spot_name` 조회, lifecycle timer, 외부 spot publish 표면을
  공통 정책과 맞춰 설명해야 한다.

## 2. 이 문서 묶음이 다뤄야 할 것

`Rust` 상세 초안은 최소한 아래 내용을 다뤄야 한다.

- app/host registration
- outbound client state 주입
- request/send/event handler registration
- channel 등록, Discovery, 수동 연결
- `SPOT` 통합
- `STREAM` 통합
- monitoring
- Registry query

## 3. 공통 초안에서 먼저 받아야 하는 것

이 언어 문서는 아래 공통 의미를 다시 정의하지 않는다.

- 상호작용 모델 이름과 의미
- `packet-name`, `correlation-id`, `content-type`
- channel grouping과 연결 방식
- 일반 channel messaging에서 dispatch는 local `ROUTER(server)` 기준이라는 점

## 4. Rust에서 기대하는 표면

현재 방향에서는 아래 같은 표면이 자연스럽다.

- `ZLinkFramework::builder()`
- `ZLinkClient`
- typed context extractor 또는 explicit context 인자
- trait 기반 handler registration 또는 macro helper

`Rust`도 `packet key` 기본값은 타입 이름을 쓰는 쪽이 자연스럽지만, macro와 trait
경계 때문에 explicit override 규칙을 문서에 분명히 적어야 한다.

## 5. 필요한 후속 문서

`.NET` 수준으로 가려면 최소한 아래 문서가 추가되어야 한다.

- 인터페이스 기준 문서
- channel messaging 주제 문서
- channel messaging 샘플 문서
- `SPOT` 주제/샘플 문서
- `STREAM` 주제/샘플 문서
- Monitoring 주제 문서
- Registry 주제 문서

## 6. 현재 상태

현재는 진입 문서만 있다.
즉 `Rust` 상세 초안은 **대표 프레임워크와 설계 방향만 잡은 단계**다.
