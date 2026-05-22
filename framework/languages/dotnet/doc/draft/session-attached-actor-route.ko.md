<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Draft -- ZLink Framework .NET Session Actor Dispatch](./session-actor-dispatch.ko.md)
<!-- framework-adapter-nav:end -->

[.NET 묶음](../README.ko.md) | [ActorGateway Relay](./actor-gateway-session-relay.ko.md) | [Session Actor Dispatch](../spec/session-actor-dispatch.ko.md)

# Draft -- Session-Attached Actor Route

> 이 초안은 **ActorGateway session relay 설계로 대체되었다**.
> 현재 공개 계약이나 구현 계획은
> [`actor-gateway-session-relay.ko.md`](./actor-gateway-session-relay.ko.md)와
> 공통 초안 [`doc/spec/draft/actor-gateway-session-relay.ko.md`](../../../../../doc/spec/draft/actor-gateway-session-relay.ko.md)를 따른다.

## 1. 대체 결정

이전 초안은 session 이 actor route snapshot 을 들고 hot path 에서 resolver lookup 을 피하는
방향을 검토했다. 현재 구현은 그 책임을 session 에 두지 않는다.

- session 은 actor id/type 으로 logical actor handle 을 bind 한다.
- route snapshot 을 public session bind 입력으로 받지 않는다.
- actor 위치 해석은 core ActorGateway 가 맡는다.
- actor 에서 client session 으로 보내는 표면은 `BoundSession` 이다.
- session close 는 actor current Spot 을 바꾸지 않는다.

따라서 이 초안의 public session attach overload, route snapshot update, resolver fallback
관련 계획은 더 이상 적용 대상이 아니다.

## 2. 현재 기준 문서

| 문서 | 역할 |
|------|------|
| `./actor-gateway-session-relay.ko.md` | `.NET` framework projection 과 회귀 기준 |
| `../spec/session-actor-dispatch.ko.md` | 현재 session actor dispatch public 계약 |
| `../spec/handler-interfaces.ko.md` | handler/context public surface |
| `../internals/behavior-matrix.ko.md` | failure/lifecycle 의미 |
| `../internals/regression-test-matrix.ko.md` | 검증 매트릭스 |

## 3. 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|-------------|-----------|
| `LocalSessionRelayTests.LocalSessionActorDispatch_Relays_Stream_Request_And_Replies_From_Request_Handler` | local relay 가 logical actor binding 과 ActorGateway 경로로 동작한다. |
| `RemoteSessionRelayTests.SessionActorDispatch_Relays_Stream_Request_And_Routes_Request_To_Bound_Actor_By_Sequence` | remote relay 가 application route mesh packet 없이 ActorGateway 경로로 동작한다. |
| `RemoteProxyDisconnectTests.BoundSessionDisconnect_FromRemoteActor_Closes_Client_Without_Session_Disconnect_Callback` | remote actor 의 bound session close 가 session disconnect callback 으로 우회하지 않는다. |
| `ActorDisconnectNotifyTests.ClientClose_Cleans_Session_Without_Actor_Disconnect_Callback` | client close 는 actor-session binding cleanup 만 수행한다. |
| `RegressionTests.Bingo_Uses_RegistryBacked_Defaults_Without_Sample_Metadata_Store` | Bingo sample 은 session route store 없이 ActorGateway attach 를 사용한다. |
| `RegressionTests.TicTacToe_Uses_RegistryBacked_Defaults_Without_Sample_Metadata_Store` | TicTacToe session gateway sample 도 같은 구조를 유지한다. |
