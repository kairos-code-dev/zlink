# Node Bindings Core Alignment 실행 가이드

> 상태: 완료
> 기준 문서: `bindings/node/plan/bindings/node-bindings-core-api-alignment-plan.ko.md`
> 대상 범위: `bindings/node/`, `doc/bindings/`, `bindings/node/plan/bindings/`
> 목적: Node bindings를 최신 `core` public surface와 Node 스타일 API 철학에 맞춰 끝까지 정렬하는 실행 순서와 완료 판정 기준 고정
> 최종 종료 판정: `미적용 사항이 없습니다.`

## 1. 문서 목적

이 문서는 메인 플랜 문서의 내용을 실제 코드 변경 순서와 완료 판정 기준으로
고정하는 실행 문서다.

설계 authority는 아래 메인 플랜 문서 하나로 고정한다.

- [`node-bindings-core-api-alignment-plan.ko.md`](./node-bindings-core-api-alignment-plan.ko.md)
  - 목적 / 상태 / 설계 원칙:
    [`1. 목표`](./node-bindings-core-api-alignment-plan.ko.md#1-목표),
    [`2. 현재 상태 요약`](./node-bindings-core-api-alignment-plan.ko.md#2-현재-상태-요약),
    [`3. 설계 원칙`](./node-bindings-core-api-alignment-plan.ko.md#3-설계-원칙)
  - 고정 결정 / public API:
    [`3.1 범위 고정 결정`](./node-bindings-core-api-alignment-plan.ko.md#31-범위-고정-결정),
    [`4. 공개 API 재정렬 방향`](./node-bindings-core-api-alignment-plan.ko.md#4-공개-api-재정렬-방향),
    [`4.4 Node 스타일 API 결정`](./node-bindings-core-api-alignment-plan.ko.md#44-node-스타일-api-결정),
    [`4.5 canonical message API 초안`](./node-bindings-core-api-alignment-plan.ko.md#45-canonical-message-api-초안),
    [`4.6 성능 계약`](./node-bindings-core-api-alignment-plan.ko.md#46-성능-계약)
  - 단계별 구현 / 검증:
    [`5. 단계별 실행 계획`](./node-bindings-core-api-alignment-plan.ko.md#5-단계별-실행-계획),
    [`6. 파일 단위 작업 범위`](./node-bindings-core-api-alignment-plan.ko.md#6-파일-단위-작업-범위),
    [`7. 검증 전략`](./node-bindings-core-api-alignment-plan.ko.md#7-검증-전략),
    [`8. 완료 기준`](./node-bindings-core-api-alignment-plan.ko.md#8-완료-기준)

실행 중 설계 판단이 필요해 보이면 먼저 메인 플랜을 갱신하고, 그 다음 이 guide를
맞춘 뒤 코드를 수정한다.

## 2. 실행 authority

단일 설계 authority:

- [`node-bindings-core-api-alignment-plan.ko.md`](./node-bindings-core-api-alignment-plan.ko.md)

이 가이드는 아래 내용을 메인 플랜에서 그대로 따른다.

- `Socket` 은 raw 계층에서 `send`, `sendParts`, `recv`, `recvInto` 만 canonical
  API로 가진다
- payload와 변환 책임은 `Message` 가 담당한다
- `Message.from` 으로 payload source에서 owned `Message`를 만든다
- `Buffer` fast path, preallocated receive buffer 경로는 유지한다
- `Received` 는 raw recv 결과와 lifecycle aggregate를 동시에 담당한다
- 공개 상수값과 enum coverage도 최신 헤더 기준으로 맞춘다
- 검증 자산은 sample script, contract tests 두 층으로 나눈다
- `perf` 쪽 자산은 이번 실행 범위에서 제외한다

자동 실행 관계:

- 수동 실행 기준 문서는 이 guide와 메인 플랜이다.
- 자동 실행이 필요하면 [`run_node_bindings_alignment_execution.sh`](./run_node_bindings_alignment_execution.sh)
  를 사용한다.
- 이 스크립트는 내부적으로 공통 supervisor인
  [`core/tools/run_codex_execution_guide_loop.sh`](../../../../core/tools/run_codex_execution_guide_loop.sh)
  를 호출한다.
- 공통 supervisor는 guide / master plan / logs / gate label만 주입받는 제너릭
  루프이고, bindings 전용 정책은 이 guide와 메인 플랜이 결정한다.
- 실행 wrapper 자체는 별도 `lock`을 두지 않는다.
  같은 작업을 병렬 실행해야 하면 `--logs-dir` 또는 `--gate-label`을 분리해서
  상태 파일 충돌을 피한다.

## 3. 중단 금지 규칙

아래 경우가 아니면 멈추지 않는다.

- 메인 플랜만으로는 해결할 수 없는 Node public API 계약 충돌
- 사용자 작업과 직접 충돌하는 워크트리 변경 발견
- `bindings/node/`, `doc/bindings/`, `bindings/node/plan/bindings/`만으로 해결할 수
  없는 blocker

위 경우가 아니면:

1. 첫 미완료 slice를 잡는다.
2. 코드 수정과 sample/contract 정리를 같이 한다.
3. 관련 검증을 끝낸다.
4. 이 guide 상태를 갱신한다.
5. 다음 미완료 slice로 바로 넘어간다.

이 가이드는 commit / push를 기본 규칙으로 강제하지 않는다.
commit / push는 사용자 지시가 있을 때만 수행한다.

## 4. 기본 실행 명령

현재 즉시 가능한 smoke:

```bash
cd bindings/node && npm test

./bindings/node/plan/bindings/run_node_bindings_alignment_execution.sh --max-iterations 0
```

현재 baseline 진단 명령:

```bash
cd bindings/node && node --test tests/*.test.js
```

주의:

- 현재 baseline은 기존 Node 바인딩의 legacy surface를 기준으로 작성되어 있을 수 있다.
- 따라서 baseline test가 모두 통과하더라도 최신 core alignment가 끝났다고 볼 수 없다.
- 반대로 alignment 중에는 TS declaration, examples, legacy API 정리 때문에 기존 test가
  일시적으로 깨질 수 있으므로, 각 slice는 해당 slice의 contract 기준으로 다시 검증한다.

최종 상태 검증 흐름:

```bash
cd bindings/node && npm test

cd bindings/node && node --test tests/*.test.js

cd bindings/node && node examples/pair-recv.js
cd bindings/node && node examples/pair-handler.js
cd bindings/node && node examples/pubsub-recv.js
cd bindings/node && node examples/pubsub-handler.js
cd bindings/node && node examples/dealer-router-recv.js
cd bindings/node && node examples/stream-handler.js
cd bindings/node && node examples/spot-recv.js
cd bindings/node && node examples/discovery-service-view.js
```

주의:

- `examples/*` 는 해당 slice에서 실제로 추가한 뒤에만 존재한다.
- Slice 5 완료 전에는 examples 명령이 없는 것이 정상일 수 있다.

실행 중 gate가 오래 걸리면 아래 명령으로 같은 셸에서 추적한다.

```bash
./core/tools/run_execution_gate_loop.sh --label node_bindings_alignment_gate --count 1
```

스크립트 smoke 확인:

```bash
./bindings/node/plan/bindings/run_node_bindings_alignment_execution.sh --max-iterations 0
```

위 명령은 공통 supervisor까지 실제로 호출하지만 Codex iteration은 돌리지 않는
최소 점검 경로다. wrapper가 supervisor의 `max-iterations=0` 종료를 smoke 성공으로
해석하므로 종료 코드는 `0`이어야 한다.

## 5. 남은 작업 체크리스트

상태 값은 아래 네 개만 쓴다.

- `미착수`
- `진행중`
- `검증중`
- `완료`

### 5.1 Slice 1. native contract / exported surface 재정렬

메인 플랜 참조:

- `Phase 0`
- `6. 파일 단위 작업 범위`의 native addon 파일

상태: `완료`

진행 메모:

- `native/src/addon.cc`, `native/src/addon_api.h`,
  `native/src/addon_core.cc`, `native/src/addon_discovery.cc`,
  `native/src/addon_spot.cc`를 최신 `core/include/zlink.h` 공개 surface 기준으로
  다시 정렬했다.
- socket/service monitor open/recv/snapshot/close, registry status/topology/query,
  discovery member peer, spot node snapshot wrapper가 Node native 경계에 추가됐다.
- JS/TS 상수 객체를 최신 헤더 값 체계로 교체했고, legacy 값은 native translate layer에서만
  허용하는 호환 경계로 축소했다.
- 검증:
  - `cd bindings/node && node-gyp rebuild`
  - `cd bindings/node && npm test`
  - `cd bindings/node && node --test tests/*.test.js`
  - `cd bindings/node && ./plan/bindings/run_node_bindings_alignment_execution.sh --max-iterations 0`

대상:

- `native/src/addon.cc`
- `native/src/addon_api.h`
- `native/src/addon_core.cc`
- `native/src/addon_discovery.cc`
- `native/src/addon_spot.cc`
- `binding.gyp`
- `native/binding.gyp`

작업:

- 공식 헤더 밖 심볼 의존 제거
- monitor / discovery / registry / spot 시그니처 재정렬
- 공개 enum / option / monitor constant export가 헤더 값과 일치하도록 native / JS 경계 정리
- 누락된 ctx / service enum coverage와 service monitor / topology query export 범위 점검
- JS export shape가 새 canonical API를 받을 수 있게 native entrypoint 정리

완료 기준:

- 비공식 심볼 lookup이 남지 않는다
- 공개 상수값이 최신 헤더와 일치한다
- ctx / service enum 누락과 service monitor / query 계층 처리 방침이 고정된다
- native addon load smoke가 통과한다

### 5.2 Slice 2. `Message` / `Received` / `Socket` canonical API

메인 플랜 참조:

- `Phase 1`
- `4.4` ~ `4.6`

상태: `완료`

진행 메모:

- `src/index.js`, `src/index.d.ts`에 `Message`, `Received`를 도입했다.
- `Socket.send`, `Socket.sendParts`, `Socket.recv`, `Socket.recvInto`가 canonical
  path가 됐고, `recv(size, flags)`는 legacy compatibility overload로만 남겼다.
- multipart / routing-id / preallocated buffer contract test를 새 API 기준으로
  재작성했다.
- 검증:
  - `cd bindings/node && npm test`
  - `cd bindings/node && node --test tests/*.test.js`

대상:

- `src/index.js`
- `src/index.d.ts`
- `tests/api.test.js`
- `tests/multipart.test.js`

작업:

- `Message`, `Received` 도입
- `Socket.recv(size)` 의존 제거 또는 legacy 축소
- `send`, `sendParts`, `recv`, `recvInto` 중심 재구성
- copy path / wrap path / preallocated buffer path를 문서와 타입으로 고정

완료 기준:

- size 추측 기반 recv가 canonical path에서 제거된다
- TS 선언과 runtime shape가 일치한다
- copy / wrap / recvInto contract test가 통과한다

### 5.3 Slice 3. option / monitor / stream / poller 계층

메인 플랜 참조:

- `Phase 2`

상태: `완료`

진행 메모:

- `MonitorSocket.snapshot()`, `ServiceMonitor`, `MonitorSnapshotDetail`,
  `MonitorSourceKind`, `MonitorState`를 추가해 monitor result shape를 고정했다.
- `Socket.setOption/getOption`, `setRoutingId/getRoutingId`,
  `subscribe/unsubscribe` helper를 추가해 old `setSockOpt/getSockOpt` 의존을
  축소하기 시작했다.
- `setSockOpt/getSockOpt`, split `streamAttachRaw/Len32be`는 runtime compatibility에만
  남기고 TS canonical surface에서는 제외하기 시작했다.
- `streamAttach*` unsupported 경계를 docs / tests / examples까지 일치시켰고,
  `streamDetach()`를 안전한 no-op cleanup 경계로 고정했다.
- native가 이미 금지한 `Registry.setSockOpt`를 JS runtime explicit rejection으로
  맞추고 TS canonical surface에서는 제거했다.
- 검증:
  - `cd bindings/node && npm test`
  - `cd bindings/node && node --test tests/*.test.js`
  - `cd bindings/node && node examples/stream-handler.js`

대상:

- `src/index.js`
- `src/index.d.ts`
- `tests/api.test.js`
- `tests/version.test.js`

작업:

- old `setSockOpt/getSockOpt` 축소
- dedicated option helper 도입
- `streamAttach*` lifecycle 정리
- monitor / poller result shape 정리

완료 기준:

- attach/detach ownership이 명확해진다
- option family가 dedicated helper로 정리된다

### 5.4 Slice 4. service 계층 재정렬

메인 플랜 참조:

- `Phase 3`

상태: `완료`

진행 메모:

- `Discovery(ctx, serviceType, serviceName)` 고정 생성자와 `Registry.bind(pub, router)`
  중심 lifecycle을 코드와 타입에 반영했다.
- `Discovery`는 비어 있지 않은 `serviceName`을 강제하고,
  `Registry.bind(pub, router)`가 native `zlink_registry_bind()` lifecycle에 직접
  매핑되는 canonical entrypoint가 되도록 정리했다.
- `Discovery.setValue/value`, `setMetadata/metadata`,
  `Registry.serviceSummarySnapshot`, `RegistryQueryClient`,
  registry topology/status snapshot, discovery member peers를 추가했다.
- unified `Spot`, `Receiver` 제거 방침을 JS/TS/runtime에 반영했고,
  `Receiver`, `ReceiverSocketRole`, `Discovery.getReceivers()`는 public export와
  TS declaration에서 제거했다.
- `SpotNode.openMonitor()`는 default mask에 sub-side role bit를 포함시켜
  actual open/close contract test로 고정했다.
- `Discovery.getReceivers()` runtime alias를 제거했고,
  `SpotNode.setDiscovery()`는 `attachDiscovery(discovery)`를 쓰도록 명시적으로
  거부하는 compatibility boundary로 축소했다.
- 검증:
  - `cd bindings/node && npm test`
  - `cd bindings/node && node --test tests/*.test.js`

대상:

- `src/index.js`
- `src/index.d.ts`
- `tests/dealer_router.test.js`
- `tests/pubsub.test.js`
- `tests/xpub_xsub.test.js`
- `tests/enums.test.js`

작업:

- `Discovery(ctx, serviceType, serviceName)` 기준 정리
- `Registry.bind(pub, router)` 중심 lifecycle 재구성
- discovery value / metadata surface 정리
- registry status / service summary / topology snapshot-query 정리
- unified `Spot` 정리
- service monitor / topology query 노출 여부와 shape 확정
- `Receiver` 처리 방침 확정

완료 기준:

- service 계층이 최신 core model로 설명된다
- service monitor / topology query 처리 방침이 문서와 타입에 반영된다
- `Receiver` 존치 여부와 migration path가 고정된다

### 5.5 Slice 5. examples / docs / contract tests 정리

메인 플랜 참조:

- `Phase 4`
- `7. 검증 전략`

상태: `완료`

진행 메모:

- `bindings/node/tests/*.test.js`를 canonical API 기준 계약 테스트로 전면 재작성했다.
- `sleep`, retry loop, polling helper 의존을 테스트 자산에서 제거했다.
- 가이드에 나열된 `bindings/node/examples/*.js` smoke 스크립트를 모두 추가했고
  로컬 실행 경로를 확인했다.
- `doc/bindings/node.md`, `doc/bindings/node.ko.md`를 새 canonical API 설명으로
  갱신했다.
- `bindings/node/README.md`를 추가해 canonical API와 verification 경로를
  정리했다.
- examples / docs를 latest aligned surface에 맞춰 다시 갱신했고,
  가이드의 smoke 경로를 실제 실행으로 확인했다.
- 검증:
  - `cd bindings/node && node examples/pair-recv.js`
  - `cd bindings/node && node examples/pair-handler.js`
  - `cd bindings/node && node examples/pubsub-recv.js`
  - `cd bindings/node && node examples/pubsub-handler.js`
  - `cd bindings/node && node examples/dealer-router-recv.js`
  - `cd bindings/node && node examples/stream-handler.js`
  - `cd bindings/node && node examples/spot-recv.js`
  - `cd bindings/node && node examples/discovery-service-view.js`
  - `cd bindings/node && ./plan/bindings/run_node_bindings_alignment_execution.sh --max-iterations 0`

대상:

- `bindings/node/examples/**`
- `bindings/node/tests/**`
- `bindings/node/README*`
- `doc/bindings/**`

작업:

- sample script 추가
- retry / sleep / polling 기반 helper 제거
- migration note, README, 타입 선언 설명 갱신

완료 기준:

- sample과 tests가 새 canonical API만 사용한다
- perf 자산 수정 없이 성능 계약이 문서화된다

## 6. 종료 조건

아래를 모두 만족하면 종료한다.

- 모든 slice 상태가 `완료`
- 메인 플랜과 가이드 사이에 불일치 없음
- examples / contract tests / smoke 경로가 최신 Node surface를 사용
- `perf` 범위 제외가 유지됨

최종 답변 문구는 아래로 고정한다.

`미적용 사항이 없습니다.`
