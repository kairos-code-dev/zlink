# ZLink Framework C++ 공개 계약

이 디렉토리는 C++ framework가 제공해야 하는 **정식 public contract**를 소유한다. public header와
contract test는 이 계약을 따라야 한다.

> **C++는 다른 언어와 달리 기능별 스펙을 유지한다.** 다른 언어는 framework를 **사용**하지만 C++는
> **framework 자체를 구현**하기 때문이다. 파일 번호는 [공통 스펙](../../README.ko.md)의 주제
> 그룹과 맞춘다.

## 0x 기반

| 문서 | 범위 |
|------|------|
| [01 application framework](01-application-framework.ko.md) | host 부트스트랩, 등록, lifecycle |
| [02 framework interfaces](02-framework-interfaces.ko.md) | 전체 public surface의 기준 |
| [03 handler interfaces](03-handler-interfaces.ko.md) | handler 정렬 규칙 |

## 1x Channel

| 문서 | 범위 |
|------|------|
| [11 channel messaging](11-channel-messaging.ko.md) | channel 등록, outbound, dispatch |

## 2x SPOT · Actor

| 문서 | 범위 |
|------|------|
| [20 spot](20-spot.ko.md) | SPOT lifecycle, publish/subscribe, timer |
| [22 actor gateway session relay](22-actor-gateway-session-relay.ko.md) | session에서 actor로의 relay |
| [25 stage wrapper on spot](25-stage-wrapper-on-spot.ko.md) | SPOT 위의 상위 모델 패턴 |

## 3x STREAM

| 문서 | 범위 |
|------|------|
| [30 stream](30-stream.ko.md) | STREAM 서버 session 표면 |

## 4x Location

| 문서 | 범위 |
|------|------|
| [40 registry](40-registry.ko.md) | 옛 Registry-backed lookup 계약의 현재 상태 |

## 5x 관측

| 문서 | 범위 |
|------|------|
| [50 monitoring](50-monitoring.ko.md) | runtime event와 snapshot |

## 6x HTTP

| 문서 | 범위 |
|------|------|
| [60 http hosting](60-http-hosting.ko.md) | HTTP 호스팅 계약 |
| [61 embedded http server](61-embedded-http-server.ko.md) | 내장 HTTP 서버 |

**기능의 의미와 동작 규칙은 [공통 스펙](../../README.ko.md)이 소유한다.** 이 디렉토리는 그 의미가
C++에서 갖는 **정확한 public 표면**을 고정한다.

## 취소 인자

C++ public interface에는 **`.NET` 모양을 옮긴 custom cancellation token을 기본 callback 인자로
두지 않는다.** 중단 가능한 장기 작업에 명시적 중단 전달이 필요하면 **C++ 표준 수명과 중단
관례**를 사용한다. timeout, host shutdown, RAII cleanup과 coroutine 수명은 각 기능 계약을 따른다.
