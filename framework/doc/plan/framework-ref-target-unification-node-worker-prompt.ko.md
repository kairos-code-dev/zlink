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
```
