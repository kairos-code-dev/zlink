<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md)
<!-- framework-adapter-nav:end -->

# Session Gateway 초안 보관본

> 이 문서는 이전 session gateway 초안의 보관본이다. 현재 공개 계약이 아니며,
> 새 구현 기준으로 사용하지 않는다.
>
> 현재 구현 기준은
> [Session Actor Dispatch 사용성 초안](../session-actor-dispatch.ko.md)이다.

## 1. 상태

이 초안은 `Session Actor Dispatch` 모델로 대체되었다. 이전 초안은 session과
actor 사이의 raw relay 표면을 public API처럼 설명했지만, 현재 모델은 typed handler,
session actor helper, `SessionProxy`, 명시적 metadata 정책을 중심으로 동작한다.

이 문서는 이전 이름이 코드나 sample에 다시 들어오지 않도록 제거 이력만 남긴다.
새 API 계약, sample 흐름, 테스트 기준은
`session-gateway-usability.ko.md`를 기준으로 확인한다.

## 2. 제거된 이전 표면

| 이전 이름 | 현재 대체 | 상태 |
|-----------|-----------|------|
| `EnableSessionGateway(...)` | `AddStreamNode(...).AddHeaderSession<TSession>()`, `BindActorHandleAsync(...)` | 제거 |
| `IZLinkSessionGateway` | `IZLinkSessionContext`, `IZLinkActorClient`, `IZLinkSessionProxy` | 제거 |
| `AddSessionProxyHandler(...)` | Entry Spot / user Spot actor handler와 `IZLinkSessionProxy` | 제거 |
| `OpenActorRelay(...)` | `CreateAndBindActorAsync(...)`, `BindActorHandleAsync(...)`, `DispatchToActorAsync(...)` | 제거 |
| `SendToActor(...)` | `DispatchToActorAsync(...)` 또는 `IZLinkActorClient.Send(...)` | 제거 |
| `RequestActor(...)` | `DispatchToActorAsync(...)` 또는 `IZLinkActorClient.Request(...)` | 제거 |
| `IZLinkSessionProxyHandler` | typed actor handler와 `IZLinkSessionProxy` 호출 표면 | 제거 |

위 이름은 제거 이력이나 검색 검증 문맥에서만 나타나야 한다. 새 코드, 새 sample,
새 계약 설명에서는 현재 대체 표면을 사용한다.

## 3. 현재 모델 요약

현재 모델은 다음 책임 경계를 사용한다.

- session은 stream header와 payload를 받은 뒤 actor ref를 만들거나 보관하고
  `DispatchToActorAsync(...)`로 전달한다.
- actor handler는 typed request 또는 command를 받고, raw request sequence나 native
  stream handle을 직접 다루지 않는다.
- actor가 client session으로 응답이나 알림을 보내야 하면 `IZLinkSessionProxy`를
  사용한다.
- session 위치는 별도 public resolver가 아니라 actor-session binding 내부 상태로
  관리한다. registry metadata를 쓰는 구현은 sample infrastructure일 수 있지만
  framework 기본 저장소가 아니다.
- application metadata는 기본적으로 전달하지 않는다. `ConfigureMetadata(...)`에서
  허용한 key만 actor handler context의 `ZLinkMessageMetadata.Application`으로 전달한다.

## 4. 검증 기준

이 문서는 구현 계획의 검색 검증에서 old API 이름이 발견될 때 분류 기준으로만 사용한다.
old API 이름이 이 문서의 제거 표 외에 새 설명으로 나타나면 충돌로 본다.

다음 문서가 현재 기준이다.

- [Session Actor Dispatch 사용성 초안](../session-actor-dispatch.ko.md)
- [framework API 초안](../framework-api.ko.md)
- [interaction model 초안](../interaction-model.ko.md)
- [message model 초안](../message-model.ko.md)
