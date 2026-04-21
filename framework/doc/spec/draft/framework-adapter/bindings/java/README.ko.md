[스펙 목차](../../../README.ko.md)

[Framework Adapter 정책](../../policy/README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel](./spring-boot-channel-messaging.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md) | [SPOT](./spring-boot-spot.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md) | [STREAM](./spring-boot-stream.ko.md) | [STREAM open items](./stream-open-items.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [Monitoring](./spring-boot-monitoring.ko.md) | [Registry](./spring-boot-registry.ko.md)

# Draft -- ZLink Framework For Java

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Java`와 `Spring Boot`에서 `ZLink Framework`를 어떤
> 모양으로 노출할지 정리하기 위한 문서다.

## 1. 목적

이 문서는 `Java` 바인딩 위에 올라가는 `ZLink Framework`의 `Java` 방향을
정리한다. 대표 프레임워크는 `Spring Boot`로 둔다.

현재 목표는 새 웹 프레임워크를 만드는 일이 아니다.
기존 `Spring Boot`가 제공하는 bean lifecycle, configuration, scheduler,
application event 모델 위에 zlink runtime을 자연스럽게 얹는 것이다.

## 1.1 공통 정책 적용

이 디렉토리의 모든 문서는
[Framework Adapter 정책](../../policy/README.ko.md)과 그 하위 문서를 그대로 따른다.
즉 `Java` 상세 문서는 공통 의미를 다시 정의하지 않고, 그 의미를 `Java`와
`Spring Boot` 표면으로만 구체화한다.

특히 아래 항목은 이 디렉토리 전체에 공통으로 적용된다.

- 네이밍 규칙은
  [doc/spec/bindings/README.md](/home/hep7/project/kairos/zlink/doc/spec/bindings/README.md)의
  `Naming Policy`를 그대로 따른다. `Java` 문서에서는 메서드는 `camelCase`,
  클래스와 annotation은 `PascalCase`를 쓴다.
- 수동 연결은 `channel + capability` 또는 `spot node + capability` 단위로
  설명한다. 같은 capability 안에서는 `Discovery`와 manual 연결을 섞지 않는다.
- send/publish는 기본 blocking submit으로 설명하고, optional non-blocking
  변형이 필요해도 `sendNonBlocking` 같은 별도 동사 이름을 만들지 않는다.
- `SPOT`을 지원하는 문서는 named spot factory 등록, `spotName` 기준 생성,
  `spotRid -> spotName` 조회, lifecycle timer, 외부 spot publish 표면을
  공통 정책과 맞춰 설명해야 한다.
- monitoring을 지원하는 문서는 socket/discovery/registry/spot runtime event를
  typed event와 등록 표면으로 설명해야 한다.

## 2. 문서 구조와 역할 분담

문서는 `.NET` 묶음과 같은 세 층으로 나눈다.

### 2.1 기준 문서

| 문서 | 역할 |
|------|------|
| [handler-interfaces.ko.md](./handler-interfaces.ko.md) | `Java` 공용 인터페이스, annotation, options, context를 한 곳에 모은 기준 문서 |

### 2.2 주제 문서

| 문서 | 다루는 범위 |
|------|------------|
| [spring-boot-channel-messaging.ko.md](./spring-boot-channel-messaging.ko.md) | channel 등록, outbound client, handler 모델, dispatch, filter |
| [spring-boot-spot.ko.md](./spring-boot-spot.ko.md) | `SPOT` bean lifecycle, publish/subscribe, channel attach |
| [spring-boot-stream.ko.md](./spring-boot-stream.ko.md) | stream packet/raw session, registration, lifecycle 기준 |
| [stream-open-items.ko.md](./stream-open-items.ko.md) | stream에서 아직 닫지 않은 serializer/write/lifecycle 항목 |
| [spring-boot-monitoring.ko.md](./spring-boot-monitoring.ko.md) | runtime monitoring 등록, typed event, 운영 샘플 |
| [stage-wrapper-on-spot.ko.md](./stage-wrapper-on-spot.ko.md) | stage 같은 상위 모델을 `SPOT` 위에 감쌀 때 필요한 조건 |
| [spring-boot-registry.ko.md](./spring-boot-registry.ko.md) | embedded registry, remote query, topology 조회 |

### 2.3 샘플 문서

| 문서 | 다루는 범위 |
|------|------------|
| [channel-messaging-samples.ko.md](./channel-messaging-samples.ko.md) | channel 등록, handler, HTTP controller, outbound client 샘플 |
| [spot-samples.ko.md](./spot-samples.ko.md) | room/stage/zone 기준 `SPOT` 등록과 publish/request 샘플 |
| [stream-samples.ko.md](./stream-samples.ko.md) | stream 등록, packet/raw session 샘플 |

## 3. 핵심 방향

- `Spring Boot` bean lifecycle에 맞춘다.
- channel messaging은 `channel name` 호출을 기본으로 둔다.
- channel capability는 startup 시점에 등록한다.
- 같은 capability에서 자동 연결과 수동 연결을 섞지 않는다.
- 일반 request/send handler dispatch는 local `ROUTER(server)` ingress 기준으로 설명한다.
- outbound `DEALER(client)`는 주로 reply correlation 경로로 본다.
- `rid` 직접 지정은 `SPOT` spot-to-spot 경로에만 남긴다.

## 4. Java에서 기대하는 표면

- `@EnableZLinkFramework`
- `ZLinkFrameworkOptionsCustomizer`
- `ZLinkClient`
- `ZLinkEventPublisher`
- `@ZLinkRequestMapping`, `@ZLinkSendMapping`, `@ZLinkEventMapping`
- `ZLinkRequestContext`, `ZLinkSendContext`, `ZLinkEventContext`

기본 packet key는 payload 타입의 `SimpleName`을 쓰고, 충돌이나 외부 계약 때문에
다른 이름이 필요할 때만 annotation 또는 options에서 override하는 쪽을 기준으로
본다.
