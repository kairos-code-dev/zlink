[스펙 목차](../../../README.ko.md)

[Framework Adapter 초안](../README.ko.md) | [공통 API](../framework-api.ko.md) | [상호작용 모델](../interaction-model.ko.md) | [메시지 모델](../message-model.ko.md) | [channel topology](../channel-topology.ko.md)

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

## 2. 이 문서 묶음이 다뤄야 할 것

`Rust` 상세 초안은 최소한 아래 내용을 다뤄야 한다.

- app/host registration
- outbound client state 주입
- request/send/event handler registration
- channel 등록, Discovery, 수동 연결
- `SPOT` 통합
- `STREAM` 통합
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
- Registry 주제 문서

## 6. 현재 상태

현재는 진입 문서만 있다.
즉 `Rust` 상세 초안은 **대표 프레임워크와 설계 방향만 잡은 단계**다.
