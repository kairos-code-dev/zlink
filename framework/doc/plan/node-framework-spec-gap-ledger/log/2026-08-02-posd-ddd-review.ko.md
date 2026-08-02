# Node Framework gap 수정 진행 log

이 log는
[`node-framework-spec-gap-ledger.ko.md`](../node-framework-spec-gap-ledger.ko.md)의
POSD·DDD 기반 review와 구현 검증 결과를 시간 순서로 기록한다. 이 파일의 기록은
완료 판정이 아니며, 최종 상태는 ledger의 항목과 검증 명령으로 판단한다.

## 2026-08-02

### 현재 상태

- `Nest` inbound dispatch builder가 호출자가 넘긴 options 객체를 계속 사용하도록 수정했다.
  따라서 HWM fluent 호출 뒤 `build()` 결과에 `applicationHwmBytes`와
  `processMemoryLimitBytes`가 보존된다.
- Native socket HWM getter에서 `bigint`를 `number`로 변환하도록 수정했다. 이 변환이 없으면
  runtime host 시작 중 `Math.max` 경로에서 `bigint`와 `number`를 섞어 처리한다.
- Channel envelope의 알 수 없는 `contentType`은 handler에 전달하기 전에
  `RequestProtocolError` 내부 원인으로 중단하도록 수정하고 회귀 검사를 추가했다.
- Node framework와 Redis location package의 binding 버전을 `11.1.0`으로 맞췄고,
  `verify_packaged_contract.sh`와 `npm ls @zlink-systems/zlink --all`을 통과시켰다.
- SPOT route physical slot은 native callback이 반환되기 전까지 유지하도록 수정했다.
  timeout 뒤 즉시 slot을 재사용하지 않는 회귀 경로를 확인했다.
- Node RouteMesh E2E client의 Client/Contract 코드에서 framework 직접 import를 제거했다.
  다만 Shared를 포함한 전이 의존성 검사와 실제 role/server adapter 경계 검증은 남아 있다.

### Sol Medium review 결과

Sol Medium review는 현재 candidate를 `NOT CLEAN`으로 판정했다.

1. public `ZLinkFrameworkException`이 41개의 상세 kind, numeric `code`,
   `isRetriable`과 join completion retry boolean을 노출한다. 공통 계약은 numeric 13종과
   `kind`만 허용한다.
2. `ZLinkInboundDispatchOptionValues`가 framework public barrel에 노출되어 있다.
   이 값 객체는 Nest/framework composition bridge의 private type이어야 한다.
3. unknown content type 검사가 direct decoder에만 있고 실제 dispatch handler의
   callback 횟수와 `ProtocolError` 결과를 검증하지 않는다.
4. `ChannelEgressRouting`과 `InstanceSpot` runner가 placeholder 상태이며 aggregate가
   partial stdout로 통과할 수 있다.
5. E2E client boundary 검사가 Client/Contract 직접 import만 확인하고, magic number와
   local duplicate DTO가 실제 public API 회귀를 가릴 수 있다.

### POSD·DDD 판단

- Error kind 변경은 public contract와 내부 lifecycle reason을 한 enum으로 유지하지 않는다.
  Application boundary에는 13종만 두고, actor·spot·routing bounded context의 상세 reason은
  내부 adapter에서 coarse kind로 변환해야 한다.
- Route request의 readiness는 test sleep으로 숨기지 않는다. one-way send는 source queue
  admission에서 완료되므로 remote handler 완료를 의미하지 않는다. route readiness와
  request submission의 책임 경계를 runtime queue에서 확인해야 한다.
- 새 public API나 test 전용 adapter를 추가하지 않는다. 기존 public surface로 해결되지 않는
  경우에는 계약 근거와 책임 소유자를 ledger에 먼저 남긴다.

### 다음 검증

1. public error kind와 내부 상세 reason을 분리한 뒤 generated declaration에 상세 kind,
   `code`, `isRetriable`이 남지 않는지 확인한다.
2. real dispatch 경로에서 malformed/unknown content type이 handler를 실행하지 않고
   callback을 한 번만 완료하는지 확인한다.
3. route one-way 뒤 request 순서를 fixed sleep 없이 반복 검증하고, route error가 발생하면
   public `Unavailable` 계약에 맞게 처리되는지 확인한다.
4. 전체 Node contract/runtime gate, package gate, process E2E를 다시 실행한다.

### R1 candidate 재검증 — 2026-08-02

이번 round의 기록은 이 ledger와 같은 디렉토리의 `log/`에만 추가한다. 다른 E2E `logs/`나 과거
snapshot은 현재 candidate의 완료 evidence로 사용하지 않는다.

```text
candidate: working-tree manifest 2026-08-02 (commit 9efee01aa39 + current changes)
reviewer: Sol Medium
model: gpt-5.6-sol
reasoning: medium
round: R1 candidate
scope: ND-IMP-001..004, ND-E2E-IMP-001..005, ND-TEST-001..003
```

구현과 회귀 검증에서 확인한 결과는 다음과 같다.

- `npm run build`: 통과.
- Node targeted regression: 260/260 통과.
- `test/contract/channel-client.test.js`: 93/93 통과.
- HWM, public 13-kind error surface, channel ProtocolError mapping, RouteMesh request admission,
  SPOT physical slot과 actor/spot join callback fixture를 current public contract에 맞췄다.
- E2E static gate에서 common scenario 302개 중 Node selector/source에 207개가 있고, 99개가 아직
  실제 scenario file 또는 selector dispatch를 갖지 않는다. 따라서 `npm test`는
  `e2e-scenario-header-gate.test.js`의 missing 목록에서 중단되며 full regression CLEAN으로 기록하지
  않는다.
- Config 12 `ChannelEgressRouting`과 Config 14 `InstanceSpot` runner는 공통 wait/log 정책을 갖추었지만
  role server가 아직 없어서 의도적으로 exit 2 (`BLOCKED`)를 반환한다. aggregate PASS로 세지 않는다.

```text
decision: NOT CLEAN
finding: ND-E2E-IMP-001 / ND-E2E-IMP-002
severity: High
category: test/evidence
evidence: 99 common scenario IDs are absent; Config 12 and Config 14 role runners are blocked.
status: open
verification: implement exact selectors and role-server process evidence, then rerun npm test and aggregate all.
```

이 결과로 ledger의 `ND-E2E-IMP-*`, `ND-TEST-003`, `ND-REG-005`~`008`은 계속 unresolved다. `log/`
규칙 자체는 충족했지만, log를 추가했다는 사실만으로 구현 gap을 닫지 않는다.

### 작업 중단 기록 — 2026-08-02

사용자 지시에 따라 이 시점에서 작업을 중단하고 대기한다. 이후 재개할 때는 이 항목부터
확인한다.

- 현재 작업 log는 계속 이 문서가 있는 디렉토리의 `log/` 아래에 기록한다. 별도의 공통
  `logs/` 디렉토리나 다른 언어의 log를 현재 Node 검증 evidence로 사용하지 않는다.
- `entry-spot-dispatch.test.js`의 9개 회귀 검사는 current public contract에 맞춘 뒤 9/9 통과했다.
  변경 내용은 `ActorRef`의 `objectGeneration`·`meshName`, raw actor lifecycle callback,
  bound-session seal callback fixture와 optional `targetSpotGeneration` 반환 shape다.
- `fanout-location-store.test.js`는 location write enum이 public barrel이 아닌 internal SPI라는
  계약에 맞춰 3/3 통과했다.
- 그 뒤 실행한
  `ZLINK_NODE_RUNTIME_GATE_SKIP_TESTS=test/contract/e2e-scenario-header-gate.test.js npm test`
  는 build·typecheck·lint 다음 `test/browser/stream-connector-chromium.test.js`가 exit 1을
  보고했다. 사용자가 중단을 요청해 outer command는 exit 130으로 종료했으므로 full gate 통과로
  기록하지 않는다. Chromium failure의 상세 원인과 이후 contract test는 재개 시 다시 확인한다.
- 앞선 R1 evidence의 핵심 blocker인 common scenario 99개 누락과 Config 12/14 role-server
  `BLOCKED` 상태는 그대로다. `npm test`의 scenario-header gate를 건너뛰어 완료로 판정하지 않는다.

```text
decision: PAUSED
pause_reason: user requested stop and wait
last_command: ZLINK_NODE_RUNTIME_GATE_SKIP_TESTS=test/contract/e2e-scenario-header-gate.test.js npm test
last_observed: test/browser/stream-connector-chromium.test.js reported exit 1; outer command exit 130 after user stop
resume_from: browser failure details, then remaining contract gate; retain 99 missing scenarios and Config 12/14 as open
```

## 비-E2E runtime 재검토 — 2026-08-02 후속

### 위험 신호와 선택한 리팩토링

| 위험 신호 | POSD·DDD 근거 | 검토한 대안 | 선택 |
|---|---|---|---|
| hot queue에서 `shift()`/`splice(0, ...)`가 반복됨 | 자료구조 선택이 호출 경로에 노출되고, 큐 비용이 요청량에 따라 O(n)으로 커진다. Queue aggregate의 FIFO invariant를 여러 cleanup 경로가 나눠 가진다. | A. 기존 배열을 유지하고 주기적으로 앞부분을 잘라낸다. B. head/count와 tombstone을 가진 내부 bounded queue로 감싼다. | B. FIFO와 cancellation hole을 내부에서 소유하고, 충분히 누적된 경우에만 compaction해 할당과 이동을 제한했다. |
| CPU worker 옵션 중 `minThreads`/`idleTimeoutMs`가 선언만 되고 runtime에서 무시됨 | 정보 은닉이 아니라 계약 누락이며, 설정을 제공해도 의미가 없는 얕은 모듈이다. | A. 필드를 계속 보존하되 무시한다. B. elastic pool의 baseline·idle reclaim으로 실제 semantics를 구현한다. | B. Worker slot이 pool aggregate의 상태와 lifecycle을 소유하고, timeout/cancellation은 slot 종료로 수렴시켰다. |
| Nest builder prototype에 codec/duplicate-name 내부 helper가 노출됨 | public surface가 runtime composition 결정을 누출하고, shallow pass-through method가 생긴다. | A. helper를 public으로 유지하고 문서에서 private라고 설명한다. B. module-local helper로 이동하고 public member exact test를 둔다. | B. 호출자는 계약에 필요한 builder만 보고 내부 정책은 module 경계 아래에 둔다. |
| generated codec test가 존재하지 않는 wire type을 import함 | 테스트가 현재 계약이 아닌 과거 representation에 결합되어 dead surface를 보존한다. | A. 없는 generated type을 compatibility shim으로 되살린다. B. 현재 generated request/response contract로 fixture를 바꾼다. | B. codec 책임을 framework 기본 serializer path에 두고 test만 현재 type graph에 맞췄다. |

### DDD 책임·invariant 확인

- `ZLinkCpuWorkerPool`은 CPU offload bounded context의 aggregate root다. `queueCount`, `inFlight`,
  slot 상태와 idle reclaim을 한 곳에서 변경하며, caller는 queue representation이나 Worker
  lifecycle을 알 필요가 없다.
- stream/session·admission·dispatch queue는 각각 자기 owner가 cancellation, terminal cleanup,
  FIFO와 capacity를 함께 보유한다. tombstone compaction은 외부 observable state가 아니라 내부
  storage policy다.
- worker `minThreads`와 `maxThreads`는 pool capacity invariant를, `idleTimeoutMs`는 slot
  lifecycle policy를 표현한다. registration normalizer와 runtime resolver는 shared internal
  defaults를 사용해 동일한 contract를 만든다.

### 재검증 결과

```text
candidate: working tree 2026-08-02 after non-E2E runtime refactor
scope: Node production runtime, contract/unit tests, package and CI path filter; E2E/sample excluded
decision: CLEAN for the non-E2E implementation scope
evidence: build PASS; typecheck PASS; lint PASS; 59 contract files 1001/1001 PASS;
          package graph 11.1.0 clean; packaged contract PASS
remaining: E2E scenario inventory gate reports 171 missing IDs; process/sample evidence excluded
```

이 판정은 E2E·sample gap을 닫았다는 의미가 아니다. 해당 범위의 unresolved 항목과
`npm run verify:ci`의 header gate 실패는 별도 E2E 후속 조건으로 유지한다.

### Worker reference 수명 재검토

worker listener를 등록하기 전에 `unref()`하는 구현은 첫 결과 message 뒤 MessagePort가 다시
참조되어 idle pool이 host process 종료를 지연시키는 위험 신호를 보였다. 생성 직후부터 계속
`unref()`하는 대안은 반대로 active job의 Promise가 event loop 종료로 끝나지 않을 수 있었다.
따라서 `ZLinkCpuWorkerPool`이 slot aggregate의 active/idle invariant를 직접 소유하도록,
listener 등록 후 baseline slot을 `unref()`, assignment 시 `ref()`, terminal completion 뒤
`unref()`하는 방식을 선택했다. 이 방식은 caller에게 worker lifecycle을 노출하지 않으면서
활성 작업의 completion과 유휴 process 종료를 모두 보장한다.

최신 검증은 `entry-spot-serial-dispatch.test.js` 24/24, 관련 6개 contract 파일 combined
152/152, build·typecheck·lint PASS와 direct test exit 0으로 확인했다. E2E/process/sample
evidence는 사용자 범위 밖이므로 이 review 판정에 포함하지 않았다.
