# ZLink Framework Draft

[Framework 스펙](../README.ko.md) | [Framework Adapter 초안](./framework-adapter/README.ko.md)

이 디렉토리는 `ZLink Framework`의 구현 전 초안을 모아 둔다.

draft 문서는 현재 공개 계약이 아니다.
구현이 끝나고 실제 API, 테스트, 배포 구조가 정리되면 정식 spec 문서로 옮긴다.

이 디렉토리의 문서는 단순 메모가 아니라, **언어별 상세 초안을 만들 때 먼저
따라야 하는 상위 초안**이다. 따라서 언어별 문서는 이 디렉토리의 공통 초안과
충돌하면 안 된다.

## 문서 목록

| 문서 | 설명 |
|------|------|
| [framework-adapter/README.ko.md](./framework-adapter/README.ko.md) | `ZLink Framework` 초안 묶음의 진입점 |
| [streaming-client.ko.md](./streaming-client.ko.md) | STREAM 서버에 접속하는 범용 client connector 초안 |

## 관리 원칙

- 요구가 생기면 먼저 draft 문서에 기록한다.
- 특히 새 기능 요구는 가능하면 use case 문서부터 추가한다.
- 정식 spec은 구현과 공개 surface가 어느 정도 고정된 뒤에 따로 정리한다.

## 공통 초안의 역할

언어별 상세 초안(`dotnet/`, 이후 추가될 `java/`, `node/` 등)은 아래 역할을
공통 초안에서 먼저 받아야 한다.

- `overview`
  - framework가 무엇을 해결하는지
- `interaction-model`
  - request-response, command, publish-subscribe, stream 같은 공용 상호작용 의미
- `message-model`
  - header/body, packet key, codec, correlation 같은 공통 메시지 의미
- `channel-topology`
  - channel grouping, Discovery, 수동 연결, inbound/outbound 경로 같은 내부 매핑
- `framework-api`
  - 프레임워크 사용자가 보게 될 handler/client/registration 표면의 큰 방향
- `usecase-validation`
  - 현재 초안이 어떤 요구를 이미 설명하는지, 무엇이 아직 비어 있는지

즉 언어별 문서는 "새 상호작용 모델을 정의하는 문서"가 아니라, 공통 초안을
해당 언어와 프레임워크 관용구에 맞게 **구체화하는 문서**여야 한다.

## 언어별 대표 프레임워크

현재 초안은 아래 프레임워크를 언어별 대표 기준으로 본다.

| 언어 | 대표 프레임워크 또는 host | 메모 |
|------|--------------------------|------|
| `.NET` | `ASP.NET Core` | 현재 가장 자세한 초안이 있다. |
| `Java` | `Spring Boot` | annotation, DI, lifecycle 통합이 자연스럽다. |
| `Node.js` | `NestJS` | decorator, module, provider 기반 모델이 잘 맞는다. |
| `Python` | `FastAPI` | async handler, DI, startup/shutdown hook 기준으로 본다. |
| `Go` | `net/http`, 필요하면 `Gin` | 강한 adapter보다 helper + middleware 통합에 가깝다. |
| `Rust` | `Axum` | extractor, state, tower middleware와의 결합을 기준으로 본다. |
| `C++` | standalone host/runtime | 기존 프레임워크 위 adapter보다 zlink host를 직접 제공하는 쪽이 맞다. |

이 표는 "반드시 그 프레임워크만 지원한다"는 뜻이 아니다.
문서를 처음 만들 때 기준으로 삼을 대표 표면을 정해 둔 것이다.

## 언어별 상세 초안 작성 규칙

새 언어 디렉토리를 만들 때는 아래 순서를 따른다.

1. 공통 초안 문서에서 먼저 개념을 닫는다.
2. 그 다음 언어별 디렉토리를 만들고, 공통 개념을 그 언어 표면으로 내린다.
3. 언어 문서에서 공통 초안과 다른 의미를 도입해야 하면, 먼저 공통 초안을
   고친 뒤 언어 문서를 고친다.

언어별 상세 초안은 최소한 아래 질문에 답할 수 있어야 한다.

- 이 언어에서 framework를 어떤 이름의 registration API로 시작하는가
- outbound client를 어떤 방식으로 주입하거나 획득하는가
- request/send/event/stream/spot/registry를 어떤 문서로 나누어 설명하는가
- packet key, channel name, timeout, codec 같은 공통 개념이 이 언어에서는 어떤
  타입과 시그니처로 드러나는가
- 공통 use case를 이 언어 문서만 읽고도 따라갈 수 있는가

언어별 문서가 위 질문에 답하지 못하면, 아직 `.NET` 묶음과 같은 수준의 상세
초안이라고 보기 어렵다.
