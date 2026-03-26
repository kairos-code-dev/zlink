# Node Bindings Core 최신 API 정렬 계획

작성일: 2026-03-26
대상: `bindings/node`
기준 소스:
- `core/include/zlink.h`
- `doc/guide/*.md`
- `bindings/node/src/index.js`
- `bindings/node/src/index.d.ts`
- `bindings/node/native/src/**`
- `bindings/node/tests/**`

## 1. 목표

`node` 바인딩을 최신 `core`의 공식 공개 표면에 다시 맞춘다. 이번 작업의 기준은
현재 네이티브 애드온이나 배포 바이너리에 우연히 남아 있을 수 있는 구 심볼이
아니라, `core/include/zlink.h`와 `doc/guide`에 문서화된 API다.

핵심 목표는 다음과 같다.

- Node native addon이 공식 헤더에 없는 구 심볼 의존을 제거한다.
- Node 공개 API를 최신 `core`의 소유권/라이프사이클/콜백 모델에 맞게 재설계한다.
- 서비스 계층을 최신 `Discovery` / `Socket Family` / `Spot` / `Registry` 모델로 정렬한다.
- 기존 `Buffer` 중심 fast path와 `recvInto`류의 재사용 경로는 버리지 않고 새 contract
  아래로 재배치한다.
- 사용자-facing 검증 체계를 `examples/` 또는 sample script + 소수의 binding contract
  tests 중심으로 재편한다.
- 저장소의 fail-fast 정책에 맞지 않는 retry/sleep 기반 테스트와 helper를 제거한다.
- 이번 정렬 작업 범위에서는 별도 `perf` 산출물, perf runner, perf helper는 다루지 않는다.

## 2. 현재 상태 요약

### 2.1 가장 큰 구조 문제

현재 Node 바인딩은 최신 `core`를 직접 반영한 표면이 아니라, 레거시 C API와
기존 addon helper shape를 전제로 구성되어 있다.

대표 예:

- `Socket` 이 `send(buf, flags)`, `recv(size, flags)`, `recvInto(buffer)` 같은
  low-level byte API를 중심으로 공개되어 있다.
- `SocketType`, `ServiceType`, `SocketOption`, `MonitorEvent` 같은 공개 상수 객체가
  최신 `core/include/zlink.h` enum / mask 값 체계와 어긋날 가능성이 높다.
- `ContextOption` 도 최신 헤더의 `ZLINK_CTX_OPT_BLOCKY` 같은 항목을 아직 반영하지
  못했을 가능성이 높다.
- `setSockOpt/getSockOpt` 가 old zmq-style 옵션 모델을 그대로 노출한다.
- `Receiver`, split service/provider 계층, split `SpotNode` + `Spot` 조합이
  최신 core의 unified service model과 어긋날 가능성이 높다.
- `Registry.setEndpoints()` + `start()` 모델은 최신 core의 bind 중심 lifecycle과
  맞지 않을 수 있다.
- `streamAttach*` 가 callback ownership / detach contract보다 mode helper 확장에
  더 치우쳐 있다.

### 2.2 Node 표면에서 이미 보이는 어긋남

현재 공개 API만 보더라도 다음 문제가 드러난다.

- `Socket.recv(size)`:
  - 호출자가 사전에 size를 알아야 하므로 최신 canonical recv 계약과 잘 맞지 않는다.
  - 메모리 재사용은 가능하지만 기본 API가 "메시지"보다 "버퍼 길이 추측"을 강제한다.
- `Socket.send(buf)` / `Socket.sendFrom(buffer, length)`:
  - copy 경로와 borrow/reuse 경로의 의미가 이름만으로 드러나지 않는다.
- `Socket.setSockOpt/getSockOpt`:
  - 공통 옵션, socket-family 전용 옵션, routing id, subscription이 한 API에 섞여 있다.
- 공개 상수 객체:
  - 현재 `SocketType.PAIR = 0`, `ServiceType.SPOT = 2` 같은 값이
    최신 헤더의 `0x1001`, `0x3002` 계열과 다르다.
  - 이름뿐 아니라 값 체계까지 최신 헤더 기준으로 다시 정렬해야 한다.
- `Context` / service discovery metadata:
  - 현재 Node 공개 표면에는 `ContextOption.BLOCKY`, `SERVICE_TYPE_SOCKET`,
    discovery value/metadata API가 보이지 않는다.
  - 최신 헤더가 제공하는 관리/메타데이터 표면이 빠져 있거나 축약되어 있을 가능성이 높다.
- `Discovery(ctx, serviceType)`:
  - 최신 core의 service-name anchored view와 맞지 않을 가능성이 높다.
- `Spot` / `SpotNode`:
  - 현재 Node는 `new Spot(node)` 와 `SpotNode.setDiscovery(discovery, service)`를
    제공하지만, 최신 core는 `zlink_spot_new(ctx)` 와
    `zlink_spot_node_attach_discovery(node, discovery)` 기준이다.
  - 생성자와 attach contract 모두 최신 헤더와 직접 맞물리지 않는다.
- monitor / topology query:
  - 최신 헤더의 `zlink_service_monitor_*`, `zlink_registry_query_*`,
    topology snapshot/query 계층이 현재 Node 공개 표면에 직접 드러나지 않는다.
  - 즉 monitor가 socket 수준에 치우쳐 있고 서비스/토폴로지 관측 축이 비어 있을 수 있다.
- `Receiver`:
  - 최신 core 공식 서비스 모델과 불일치할 가능성이 높고, `routerSocket()`,
    `routerPeers()` 같은 표면은 service abstraction보다 내부 구조를 더 많이 새게 한다.
- `Spot`:
  - topic publish/recv를 제공하지만 unified `zlink_spot_new` 기준인지, split
    pub/sub wrapper 기준인지 API shape만으로는 드러나지 않는다.

### 2.3 최신 core가 요구하는 방향

최신 `core`는 다음 방향을 요구한다.

- 옵션 계층:
  - 공통 옵션은 `zlink_set_option` / `zlink_get_option`
  - 특화 옵션은 socket-family별 dedicated API
  - routing id / subscription은 전용 API
- 메시지 계층:
  - canonical send/recv는 `zlink_send`, `zlink_send_rid`, `zlink_recv`,
    `zlink_publish`, `zlink_subscribe`
  - raw helper는 공개 표면의 중심이 아니다
- 콜백/이벤트 계층:
  - recv / subscribe / send-ready handler
  - socket monitor / service monitor / snapshot
- 서비스 계층:
  - `Registry`: bind / config / snapshot / query
  - `Discovery`: `(ctx, serviceType, serviceName)` 기반 단일 service view
  - unified `Spot`
  - `SpotNode`: topology / lifecycle / snapshot 역할

### 2.4 정책 위반 가능성

이번 정렬 작업에서는 Node 코드와 테스트도 다음 금지 규칙을 따라야 한다.

- retry loop 금지
- `setTimeout`, `sleep`, polling 기반 테스트 동기화 금지
- fail-fast 위반 helper 금지
- 제품 버그를 테스트 완화로 숨기는 수정 금지

## 3. 설계 원칙

이번 작업은 다음 원칙으로 진행한다.

- 공식 헤더 우선:
  - Node addon과 JS/TS surface는 `core/include/zlink.h` 기준으로만 정렬한다.
- Node 우선:
  - JS 사용자가 C++식 out parameter, mode enum 남발, 구조체 흉내 API를 배우지
    않아도 되게 만든다.
- POSD 우선:
  - `Socket`, `Message`, `Received`, `Discovery` 같은 깊은 모듈 경계를 만든다.
  - 얕은 wrapper 확산과 shape별 helper 남발을 줄인다.
- 라이프사이클 명확화:
  - `close()` ownership, callback detach, monitor ownership을 문서 몇 줄로 설명
    가능해야 한다.
- copy/borrow 명시:
  - hidden copy가 일어나는 API와 caller-owned buffer 재사용 API를 이름으로 구분한다.
- hot path 절제:
  - hot path public API에서 불필요한 문자열 변환, object cloning, 배열 재포장,
    implicit UTF-8 decode를 기본값으로 두지 않는다.
- 이벤트 루프 존중:
  - Node surface는 sync native 호출을 유지하더라도 장시간 blocking이 기본 경로가
    되지 않게 설계한다.
- 성능 보존:
  - 기존 `Buffer` fast path, preallocated receive buffer, multipart direct path는
    제거하지 않고 canonical API 아래에 재배치한다.
- 검증 우선:
  - 각 단계마다 Node test + sample 검증 기준을 둔다.

## 3.1 범위 고정 결정

이번 작업에서 아래 항목은 더 이상 열어두지 않고 고정한다.

- 공식 표면 기준:
  - Node 바인딩은 `core/include/zlink.h`에 선언된 공개 함수/enum/struct에만 의존한다.
- 상수값 표면:
  - JS/TS에 노출하는 enum / event / option 상수 값도 공식 헤더 값으로 정렬한다.
  - 이름만 맞고 값이 다른 alias는 유지 대상으로 보지 않는다.
- service / topology 관측:
  - 최신 헤더의 service monitor, topology snapshot/query 표면을 Node에서도 빠짐없이
    다룰지 여부를 이번 작업에서 명시적으로 결정한다.
- `Receiver`:
  - 최신 core 공식 표면과 불일치하면 유지 대상이 아니다.
  - `Receiver`는 deprecated 유지보다 삭제를 기본 방침으로 한다.
- `Spot`:
  - split pub/sub wrapper가 아니라 unified `zlink_spot_new` 기반 handle로 정렬한다.
- `Discovery(Context, ServiceType)`:
  - 유지하지 않는다.
  - `serviceName` 없는 discovery view는 canonical API로 채택하지 않는다.
- `Registry.setEndpoints()` / `start()`:
  - 새 canonical API는 `bind(pub, router)`다.
  - 기존 메서드는 유지하지 않는 쪽을 기본으로 한다.
- 검증 전략:
  - `perf` 쪽 산출물, perf harness, perf benchmark는 이번 작업 범위에서 제외한다.
  - 검증 자산은 sample script와 contract tests 두 축으로만 추가한다.

## 3.2 비목표

이번 작업의 비목표는 다음과 같다.

- `core/tests` transport / protocol / reconnect matrix를 Node에서 다시 구현하는 것
- 최신 core에 없는 호환 심볼을 계속 노출하기 위한 adapter layer 유지
- perf 전용 커맨드, benchmark 결과 수집, 성능 대시보드 추가
- `Receiver`, old option model, guessed-size recv 모델을 장기 호환 API로 승격하는 것

## 4. 공개 API 재정렬 방향

### 4.1 유지할 축

- `Context`
- `Socket`
- `Message`
- `Received`
- `Poller`
- `MonitorSocket`
- `Discovery`
- `Registry`
- `SpotNode`
- `Spot`

### 4.2 축소/제거/치환 대상

| 현재 Node 표면 | 문제 | 목표 방향 |
|---|---|---|
| `Socket.recv(size)` | 크기 추측 강제, message lifecycle 누락 | `recv()` 또는 `recvInto()` 중심 canonical recv로 재설계 |
| `sendFrom(buffer, length)` | copy/borrow 경계가 이름만으로 드러나지 않음 | `send(message)` / `sendParts(parts)` + 명시적 wrap API로 치환 |
| `setSockOpt/getSockOpt` | option family 혼합 | dedicated option API + helper로 재설계 |
| `ContextOption` / `ServiceType` 일부 누락 | 최신 헤더 enum coverage 부족 | 누락 상수와 ctx/service enum을 헤더 기준으로 보강 |
| legacy constant objects | 공식 헤더와 값 체계 불일치 가능 | 헤더 값 기준 상수 객체로 재생성 |
| `Discovery(ctx, serviceType)` | service name 없는 wide view | `Discovery(ctx, serviceType, serviceName)` 로 치환 |
| `Receiver` | 최신 service model과 불일치 | `Socket + Discovery attach` 모델 또는 삭제 |
| `Registry.setEndpoints()+start()` | bind 중심 lifecycle과 불일치 | `Registry.bind(pub, router)` 로 치환 |
| `new Spot(node)` | unified `zlink_spot_new(ctx)` 와 불일치 | `Spot.open(ctx)` 또는 `new Spot(ctx)` 기준으로 재설계 |
| `SpotNode.setDiscovery(discovery, service)` | discovery가 이미 service identity를 고정 | `attachDiscovery(discovery)` 로 치환 |
| service monitor / topology query 부재 | 최신 service 관측 surface 누락 | `ServiceMonitor`, query client, snapshot API 검토 후 정렬 |
| split helper `streamAttachRaw/Len32be` | mode helper 확산 | `streamAttach({ mode, onPackets })` 또는 얇은 alias만 유지 |

### 4.3 임시 호환 정책

레거시 API를 한 번에 삭제하지 말고 다음 순서로 간다.

1. 최신 core 기반 신규 내부 contract 구축
2. 신규 Node API 추가
3. 기존 API를 신규 API 위에서 재구현 가능한 범위만 deprecated로 유지
4. 재구현이 억지인 API는 early removal 후보로 분류

`Receiver`, old `Discovery(Context, ServiceType)`, `Registry.setEndpoints()/start()`는
강한 삭제/치환 후보로 본다.

### 4.4 Node 스타일 API 결정

이번 작업에서 raw socket 계층의 public API는 Node 스타일로 다음 원칙을 따른다.

- `Socket` 은 행위 이름을 `send`, `sendParts`, `recv`, `recvInto` 로 단순화한다.
- payload 변환 책임은 `Message` 가 맡는다.
- `Buffer` 와 `Uint8Array`는 hot path 1급 타입으로 취급한다.
- 문자열 payload는 canonical path가 아니라 명시적 helper로 분리한다.
- 상수 객체는 `core/include/zlink.h` 값을 그대로 반영하고, TS literal type도 같은 값으로 고정한다.
- multipart는 `Buffer[]` 보다 `ReadonlyArray<Message>` 또는 `ReadonlyArray<BufferLike>`
  계약을 우선 검토하되, runtime allocation이 늘면 `Buffer[]` canonical surface를 유지한다.
- receive 결과는 `Received` value object 하나로 통일한다.
- callback 등록은 `attachXxx(handler)` / `detachXxx()` 처럼 수명 경계가 이름으로 드러나야 한다.
- promise wrapper가 필요하더라도 native hot path는 sync contract를 기준으로 설계한다.

### 4.5 canonical message API 초안

문서 기준 초안은 아래다.

```ts
type BufferLike = Buffer | Uint8Array;

class Message {
  static copyOf(data: BufferLike | string, encoding?: BufferEncoding): Message;
  static wrap(buffer: BufferLike): Message;
  static empty(): Message;

  toBuffer(): Buffer;
  byteLength(): number;
}

class Received {
  parts: readonly Buffer[];
  routingId?: Buffer;
  hasMore: boolean;

  close(): void;
}

class Socket {
  send(message: Message, flags?: number): number;
  sendParts(parts: readonly Message[], flags?: number): number;
  recv(flags?: number): Received;
  recvInto(target: Buffer, flags?: number): number;
}
```

고정 의도:

- `copyOf` 와 `wrap` 으로 복사/borrow를 이름으로 구분한다.
- `recv()` 는 메시지 경계를 보존해야 하며, size 추측을 호출자에게 강제하지 않는다.
- `recvInto()` 는 preallocated `Buffer` 재사용이 필요한 hot path 전용 API로 둔다.
- `Received.close()` 는 native borrowed resource가 있는 경우에만 의미를 갖고, pure
  JS copy 결과에서는 no-op 이어도 된다. 중요한 것은 ownership 설명이 일관되는 것이다.

### 4.6 성능 계약

이번 작업의 성능 계약은 별도 perf 산출물이 아니라 API shape로 관리한다.

- 기본 send/recv에서 hidden UTF-8 encode/decode를 넣지 않는다.
- `Buffer.isBuffer()` fast path를 유지한다.
- `Uint8Array` 입력은 필요 최소한의 `Buffer` view 변환만 허용한다.
- `recvInto(buffer)` 는 새 `Buffer`를 만들지 않는 경로로 유지한다.
- multipart 수신 시 part별 불필요한 재포장 object를 만들지 않는다.
- callback 기반 stream/monitor/service handler는 매 이벤트마다 closure capture object를
  추가로 만들지 않는 방향을 우선한다.
- TypeScript 선언은 사용자에게 copy/borrow와 sync/blocking 성격을 숨기지 않는다.
- 성능 검증은 perf benchmark가 아니라 sample과 contract test에서 allocation-heavy API
  회귀가 없는지 확인하는 수준으로 제한한다.

## 5. 단계별 실행 계획

### Phase 0. native contract 정렬

- `native/src/*.cc` 에서 공식 헤더 밖 심볼 lookup 제거
- enum / struct / callback 시그니처를 최신 `core` 기준으로 정렬
- monitor / discovery / spot / registry 계층의 downcall 정리

현재 반영 상태:

- `addon_core.cc`, `addon_discovery.cc`, `addon_spot.cc`가 최신 공개 헤더 기준
  native entrypoint를 사용한다.
- socket/service monitor wrapper, registry status/topology/query wrapper,
  spot node snapshot wrapper가 Node native 경계에 추가됐다.
- JS/TS 상수 객체 값도 헤더 값 체계로 교체됐다.

완료 기준:

- 비공식 심볼 lookup 이 남지 않는다
- native load smoke 가 통과한다

### Phase 1. `Message` / `Received` / `Socket` canonical화

- `Socket.recv(size)` 의존 제거
- `Message.copyOf` / `Message.wrap` 도입
- `Received` aggregate 도입
- 기존 `send`, `sendFrom`, `recvInto`, multipart path를 새 canonical 경로 위로 연결

현재 반영 상태:

- `Message`, `Received`가 `src/index.js`, `src/index.d.ts`에 도입됐다.
- `Socket.send`, `sendParts`, `recv`, `recvInto`가 canonical path가 됐고
  `recv(size, flags)`는 legacy overload로 축소됐다.
- contract test가 copy / wrap / recvInto / multipart / routing-id 경로를 검증한다.

완료 기준:

- size 추측 기반 recv가 canonical API에서 사라진다
- copy path / wrap path / recvInto path contract test가 통과한다

### Phase 2. option / monitor / poller / stream 계층 정리

- old `setSockOpt/getSockOpt` 축소
- dedicated option helper 도입
- `streamAttach*` / monitor API의 lifecycle 정리

현재 반영 상태:

- `MonitorSocket.snapshot()`, `ServiceMonitor`, monitor snapshot constants가
  추가됐다.
- `setOption/getOption`, `setRoutingId/getRoutingId`, `subscribe/unsubscribe`
  helper가 추가돼 raw option surface 분리가 시작됐다.
- `setSockOpt/getSockOpt`, split `streamAttachRaw/Len32be`는 runtime compatibility에만
  남기고 TS canonical surface에서는 제외하기 시작했다.
- `streamAttach*` unsupported 경계를 docs / tests / examples까지 일치시켰고,
  `streamDetach()`를 안전한 no-op cleanup 경계로 고정했다.
- native가 이미 금지한 `Registry.setSockOpt`를 JS runtime explicit rejection으로
  맞추고 TS canonical surface에서는 제거했다.

완료 기준:

- option family가 dedicated helper로 정리된다
- callback attach/detach ownership이 문서와 타입에 반영된다

### Phase 3. service 계층 재정렬

- `Discovery`, `Registry`, `SpotNode`, `Spot` 를 최신 core surface에 정렬
- `Receiver` 제거 또는 명시적 deprecated 후 삭제 경로 고정

현재 반영 상태:

- `Registry.bind(pub, router)`, `RegistryQueryClient`, topology/status snapshot,
- `Discovery(ctx, serviceType, serviceName)`, discovery value/metadata,
  discovery member peer surface가 추가됐다.
- `Discovery`는 비어 있지 않은 `serviceName`을 강제하고,
  `Registry.bind(pub, router)`가 native `zlink_registry_bind()` lifecycle에 직접
  매핑되는 canonical entrypoint로 정리됐다.
- `Registry.serviceSummarySnapshot()`까지 포함한 service snapshot surface가
  추가됐다.
- `Receiver`, `ReceiverSocketRole`, `Discovery.getReceivers()`는 public export와
  TS declaration에서 제거했고, runtime alias도 정리했다.
- `SpotNode.openMonitor()`는 default mask에 sub-side role bit를 포함시켜
  actual open/close contract test로 고정됐다.
- `SpotNode.setDiscovery()`는 canonical `attachDiscovery()`로의 migration path를
  드러내는 explicit rejection 경계로 축소했다.

완료 기준:

- unified service model 설명이 가능하다
- `Receiver` 유지 비용이 남지 않는다

### Phase 4. 문서 / examples / 테스트 정리

- `README`, examples, TS declaration, migration note 정리
- contract tests를 최신 canonical API 기준으로 재작성
- sleep/retry 기반 테스트 제거

현재 반영 상태:

- `bindings/node/tests/*.test.js`는 canonical API 계약 테스트 기준으로
  재작성됐다.
- sleep/retry/polling helper는 제거됐다.
- 실행 가이드에 명시된 `bindings/node/examples/*.js` smoke 스크립트가 추가됐다.
- `doc/bindings/node*.md`는 새 `Message` / `Received` / service surface 설명으로
  갱신됐다.
- `bindings/node/README.md`가 canonical API와 검증 경로 기준으로 추가됐다.

완료 기준:

- examples와 tests가 새 API 철학을 그대로 사용한다
- perf 자산 수정 없이도 Node binding 방향을 설명할 수 있다

## 6. 파일 단위 작업 범위

우선 수정 대상:

- `bindings/node/native/src/addon.cc`
- `bindings/node/native/src/addon_api.h`
- `bindings/node/native/src/addon_core.cc`
- `bindings/node/native/src/addon_discovery.cc`
- `bindings/node/native/src/addon_spot.cc`
- `bindings/node/binding.gyp`
- `bindings/node/native/binding.gyp`
- `bindings/node/src/index.js`
- `bindings/node/src/index.d.ts`
- `bindings/node/tests/*.test.js`
- `bindings/node/package.json`

필요 시 추가 대상:

- `doc/bindings/**`
- `bindings/node/README*`
- `bindings/node/examples/**`
- `bindings/node/plan/bindings/**`

이번 작업에서 제외:

- `core/perf/**`
- `core/bench/**`
- 별도 Node perf runner / benchmark 디렉토리 추가

## 7. 검증 전략

Node 바인딩 검증은 `core` correctness 재증명이 아니라, "Node surface가 최신 C API를
안전하고 예측 가능하게 노출하는가"만 검증해야 한다.

남길 검증:

- native addon load / symbol smoke
- `Context` / `Socket` / `Message` / `Received` lifecycle
- multipart send/recv mapping
- copy path / wrap path / recvInto path
- enum / option / event constant value mapping
- option / routing-id / subscription mapping
- monitor / service-monitor wrapper
- topology query / snapshot wrapper
- unified `Spot` / `Discovery` / `Registry` contract
- TS declaration과 runtime shape 일치

삭제 대상:

- transport matrix 복제
- reconnect / HWM / protocol corner case 대량 포팅
- perf benchmark성 검증

sample 후보:

- `examples/pair-recv.js`
- `examples/pair-handler.js`
- `examples/pubsub-recv.js`
- `examples/pubsub-handler.js`
- `examples/dealer-router-recv.js`
- `examples/stream-handler.js`
- `examples/spot-recv.js`
- `examples/discovery-service-view.js`

## 8. 완료 기준

다음을 모두 만족하면 완료다.

- Node addon과 JS/TS surface가 공식 헤더 기준으로 정렬된다
- `Socket`, `Message`, `Received` canonical API가 문서와 코드에서 일치한다
- `Receiver` 와 old option / discovery / registry 축의 처리 방침이 고정된다
- sample과 contract tests가 새 API 철학을 사용한다
- `perf` 쪽 변경 없이도 성능 계약이 API shape로 설명된다
- 실행 가이드의 남은 작업이 모두 `완료`다

최종 종료 판정 문구는 아래로 고정한다.

`미적용 사항이 없습니다.`
