# Node Worker Prompt: ActorRef / SpotRef 전송 대상 통일

기준 문서: `framework/doc/plan/framework-ref-target-unification-plan.ko.md`

## 목표

Node framework public contract에서 메시징 대상 개념을 `ActorRef` / `SpotRef`로 통일한다.
actor id 또는 spot id만 받아서 메시지를 보내는 API는 제거한다. id는 조회 입력이고, ref는 전송
입력이다.

## Naming 규칙

| 현재 이름 | 최종 이름 |
|-----------|-----------|
| `ActorRef` | `ActorRef` 유지 |
| `ZLinkSpotAddress` | `SpotRef` |
| `IZLinkSpotAddressResolver` | `ZLinkSpotRefResolver` |

Manager, client, store, runtime 같은 service/role 타입은 기존 framework naming을 따른다. 값 개념인
ref 타입에는 `ZLink`를 붙이지 않는다.

## 제거 대상

```text
framework/languages/node/packages/framework/src/contracts/Actors/ZLinkActorClient.ts
  sendToActor(actorId: string, ...)
  requestToActor(actorId: string, ...)

framework/languages/node/packages/framework/src/contracts/Spots/Contracts.ts
  sendToSpot(spotRid: RoutingId, ...)
  requestToSpot(spotRid: RoutingId, ...)
```

## 추가/변경 대상

- `ZLinkSpotAddress` interface를 `SpotRef`로 변경한다.
- `IZLinkSpotAddressResolver`를 `ZLinkSpotRefResolver`로 변경한다.
- `resolveSpotAddress`를 `resolveSpotRef`로 변경한다.
- `resolveActorSpotAddress`를 `resolveActorSpotRef`로 변경한다.
- actor messaging API는 `ActorRef`를 받는다.
- spot messaging API는 `SpotRef`를 받는다.
- `ZLinkSpotRemoteAddress` / `ZLinkSpotRemoteAddressResolver`는 일반 application surface에서 제거하거나
  advanced routing extension으로 분리한다.

## 주요 파일

```text
framework/languages/node/packages/framework/src/contracts/Actors/ZLinkActorClient.ts
framework/languages/node/packages/framework/src/contracts/Common/ActorRef.ts
framework/languages/node/packages/framework/src/contracts/Locations/Resolvers.ts
framework/languages/node/packages/framework/src/contracts/Locations/Rows.ts
framework/languages/node/packages/framework/src/contracts/Spots/Contracts.ts
framework/languages/node/packages/framework/src/contracts/Spots/SpotRoutingContracts.ts
framework/languages/node/packages/framework/src/runtime/actors/actor-client.ts
framework/languages/node/packages/framework/src/runtime/actors/index.ts
framework/languages/node/packages/framework/src/runtime/channels/index.ts
framework/languages/node/packages/framework/src/runtime/locations/index.ts
framework/languages/node/packages/framework/src/runtime/spots/index.ts
framework/languages/node/packages/framework/src/runtime/streams/index.ts
```

## 테스트

추가/수정해야 할 테스트:

```text
framework/languages/node/test/contract/contract-surface.test.js
framework/languages/node/test/contract/actor-manager.test.js
framework/languages/node/test/contract/location-runtime.test.js
framework/languages/node/test/contract/entry-spot-dispatch.test.js
framework/languages/node/test/contract/stream-runtime.test.js
```

필수 검증:

- `sendToActor(ActorRef, ...)` / `requestToActor(ActorRef, ...)`가 동작한다.
- `sendToSpot(SpotRef, ...)` / `requestToSpot(SpotRef, ...)`가 동작한다.
- id-only messaging API가 public contract에 없다.
- ref 기반 전송 중 location resolver/store가 호출되지 않는다.
- stale `SpotRef` 실패 분류가 기존 계약과 맞다.

## 문서 변경 대상

코드와 테스트를 바꾸는 같은 작업 안에서 Node 문서와 관련 공통 문서를 함께 수정한다. 문서 수정은
별도 worker로 넘기지 않는다.

```text
framework/doc/contract-inventory/framework-public-contract-inventory.json
framework/doc/framework/common
framework/doc/framework/node
```

사용자-facing 문서에는 `ActorRef` / `SpotRef` 기반 전송만 남긴다. `JoinSpot(spotRid, ...)`처럼
lifecycle id 입력이 남아야 하는 경우에는 일반 메시징 API가 아니라는 설명을 붙인다.

## 메시지 핸들러 등록 정책 동시 적용

이 worker 작업 중 Node sample이나 E2E의 handler 등록 표면을 고치면
`framework/doc/framework/common/spec/framework-api.ko.md`의 메시지 핸들러 정책도 같은 범위에서
적용한다. 특히 handler 타입과 metadata로 알 수 있는 값은 등록 호출부에 반복 인자로 넘기지 않고,
Node/NestJS에서는 decorator와 provider metadata를 활용한 automatic registration을 우선 사용한다.

sample 정리는 아래 기준으로 함께 진행한다.

- `TicTacToe.Ts`는 manual registration을 보여 주는 예시로 남긴다.
- `Bingo.Ts`, `DeliveryDispatch.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`, `SupportChat.Ts`는 manual
  registration을 제거하고 automatic registration만 사용하도록 정리한다.
- 자동 등록과 수동 등록이 같은 dispatch key를 만들면 startup validation 오류로 처리한다. 조용히
  덮어쓰거나 특정 sample만 통과시키는 우회로 처리하지 않는다.

## connection 복구 책임 경계

Node framework는 이미 core/binding에 넘긴 connection의 복구를 직접 구현하지 않는다. 연결된
connection의 끊김 감지와 reconnect는 core 또는 binding socket option 책임이다. framework는
location/topology desired set 계산과 initial connect 실패 재시도까지만 맡는다.

감사 대상:

```text
framework/languages/node/packages/framework/src/runtime/messaging/index.ts
framework/languages/node/packages/framework/src/runtime/backend/
framework/languages/node/packages/framework/src/runtime/locations/
framework/languages/node/packages/framework/src/runtime/streams/
bindings/node/src/
```

처리 기준:

1. `ZLinkAsyncSubmitter`가 established connection reconnect를 수행하지 않는지 확인한다. ready
   notification으로 pending submit을 drain하는 것은 reconnect가 아니지만, core/binding의
   `submit_retry`, poller, ready notification과 같은 정책을 중복 구현하는지 감사한다.
2. 중복이면 framework queue를 유지하지 말고 core/binding 옵션 전달과 error mapping으로 내린다. 단,
   core/binding public surface가 부족하면 Node만 우회하지 말고 binding/core 버그로 분리한다.
3. disconnected monitor event를 보고 framework가 같은 endpoint reconnect loop를 시작하는 코드가
   있으면 제거한다.
4. location runtime의 topology reconciliation이 있으면 initial connect retry와 topology handover만
   허용한다. active/connected 상태를 core 상태보다 framework가 더 권위 있게 판단하면 수정한다.
5. test fake가 reconnect 동작을 framework 책임처럼 고정하고 있으면 테스트 이름과 기대값을 바꾼다.

완료 보고에는 `ZLinkAsyncSubmitter`를 유지/제거/축소한 판단 근거와 binding/core로 분리한 항목을
포함한다.

## 완료 게이트

```bash
cd framework/languages/node
npm test

rg -n "ZLinkSpotAddress|resolveSpotAddress|resolveActorSpotAddress|sendToActor\\([^)]*actorId|requestToActor\\([^)]*actorId|sendToSpot\\([^)]*spotRid|requestToSpot\\([^)]*spotRid" \
  . \
  -S -g '!**/node_modules/**'

rg -n "SpotAddress|spot address|SpotRemoteAddress|spot remote address|sendToActor\\([^)]*actorId|requestToActor\\([^)]*actorId|sendToSpot\\([^)]*spotRid|requestToSpot\\([^)]*spotRid" \
  ../../doc/contract-inventory ../../doc/framework/common ../../doc/framework/node \
  -S -g '!../../doc/plan/**' -g '!../../doc/**/draft/**'

rg -n "reconnect|retry|backoff|setTimeout|setInterval|disconnect.*connect|connect.*disconnect" \
  packages/framework/src test \
  -S -g '!**/dist/**' -g '!**/node_modules/**'
```
