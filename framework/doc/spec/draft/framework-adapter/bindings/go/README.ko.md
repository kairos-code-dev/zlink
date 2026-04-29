[스펙 목차](../../../README.ko.md)

[Framework Adapter 정책](../../policy/README.ko.md) | [공통 API](../../policy/framework-api.ko.md) | [상호작용 모델](../../policy/interaction-model.ko.md) | [메시지 모델](../../policy/message-model.ko.md) | [channel topology](../../policy/channel-topology.ko.md)

# Draft -- ZLink Framework For Go

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Go`에서 `ZLink Framework`를 어떤 모양으로 노출할지
> 정리하기 위한 진입 문서다.

## 1. 목적

이 문서는 `Go` 바인딩 위에 올라가는 `ZLink Framework`의 `Go` 방향을 정리한다.
현재 대표 기준은 `net/http`이고, 필요하면 `Gin` 같은 상위 프레임워크도 함께
참고한다.

이 기준을 고른 이유는 아래와 같다.

- `Go`에서는 강한 프레임워크보다 표준 `net/http` 위 helper를 얹는 편이 자연스럽다.
- middleware, context, background goroutine lifecycle을 설명하기 쉽다.
- registration helper와 outbound client를 별도 패키지로 나누기 좋다.

## 1.1 공통 정책 적용

이 디렉토리의 모든 문서는
[Framework Adapter 정책](../../policy/README.ko.md)과 그 하위 문서를 그대로 따른다.
즉 `Go` 상세 문서는 공통 의미를 다시 정의하지 않고, 그 의미를 `Go` 표면으로만
구체화한다.

특히 아래 항목은 이 디렉토리 전체에 공통으로 적용된다.

- 네이밍 규칙은
  [doc/spec/bindings/README.md](/home/hep7/project/kairos/zlink/doc/spec/bindings/README.md)의
  `Naming Policy`를 그대로 따른다. `Go` 문서에서는 exported 이름을
  `PascalCase`로 적고, overloading이 없을 때만 `SendTo` 수준의 최소 접미사를
  허용한다.
- 수동 연결은 `channel + capability` 또는 `spot node + capability` 단위로
  설명한다. 같은 capability 안에서는 `Discovery`와 manual 연결을 섞지 않는다.
- send/publish는 기본 async submit으로 설명한다. backpressure는 public
  non-blocking 옵션이 아니라 framework 내부의 nonblocking send, pending queue,
  ready notification으로 처리한다.
- `SPOT`을 지원하는 문서는 named spot factory 등록, `spotName` 기준 생성,
  `spotRid -> spotName` 조회, lifecycle timer, 외부 spot publish 표면을
  공통 정책과 맞춰 설명해야 한다.

## 2. 이 문서 묶음이 다뤄야 할 것

`Go` 상세 초안은 최소한 아래 내용을 다뤄야 한다.

- host/app registration helper
- outbound client 획득 방식
- request/send/event handler registration
- channel 등록, Discovery, 수동 연결
- `SPOT` 통합
- `STREAM` 통합
- monitoring
- Registry query

## 3. 공통 초안에서 먼저 받아야 하는 것

이 언어 문서는 아래 공통 의미를 다시 정의하지 않는다.

- 상호작용 모델 이름
- `packet-name`, `correlation-id`, `content-type`
- channel grouping과 outbound channel 등록 의미
- 일반 channel messaging의 dispatch ingress

## 4. Go에서 기대하는 표면

현재 방향에서는 아래 같은 표면이 자연스럽다.

- `zlinkframework.Register(...)`
- `zlinkframework.Client`
- 명시적 registration 함수
- `Context`와 `Options` 구조체

`Go`는 method overload가 약하므로, `packetName`, `timeout` 같은 변형은 options
구조체로 모으는 편이 더 중요하다.

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
즉 `Go` 상세 초안은 **표현 방식과 구조 원칙만 정한 단계**다.
