# Node Framework runtime·spec gap 전체 재감사 — 2026-08-03

이 log는 현재 working tree의 Node Framework production contract, runtime, package, E2E inventory와
process blocker를 다시 대조한 결과다. 기존 dirty change를 되돌리지 않았으며, 과거 log의 PASS나
feature-map의 `implemented` 표시는 현재 process evidence로 재사용하지 않았다.

## 현재 결론

초기 production declaration·runtime·package의 비-E2E 비교에서는 새 public contract mismatch가 없었지만,
completion 전이와 runtime weight validation을 실제 production call path까지 다시 대조하면서
`ND-IMP-005`와 `ND-IMP-006`을 추가로 확인했다. 후속 재현에서는 same-gate awaited request,
STREAM error code, User Spot Ready route cache lifecycle도 runtime gap으로 확인해 `ND-IMP-009`~`011`로
수정했다. `ND-IMP-001`~`011`의 runtime/unit 조건은 current candidate에서 확인했지만 process·native
artifact evidence와 Framework spec gate의 전체 판정은 아직 `NOT CLEAN`이다. targeted process PASS는
common aggregate gate를 대체하지 않는다.

현재 열려 있는 Framework runtime·spec gap은 다음과 같다.

| ID | 현재 상태 | 남은 조건 |
|---|---|---|
| `ND-IMP-001` | 비-E2E runtime 충족 | `configureInboundDispatch`를 사용하는 실제 role-server process와 admission/preflight 순서 evidence |
| `ND-IMP-002` | 비-E2E runtime 충족 | client-visible error kind/reason과 role-server terminal mapping의 process evidence |
| `ND-IMP-003` | 비-E2E runtime 충족 | 실제 channel·spot process에서 unknown content type의 `ProtocolError`, handler 0회와 callback exactly-once evidence |
| `ND-IMP-004` | package graph·clean consumer 충족 | current native artifact를 포함한 role server와 SubmitAdmission process 실행 |
| `ND-IMP-005` | 비-E2E runtime·unit 수정 완료 | terminal/recovery completion의 StoreVersion route replacement process evidence |
| `ND-IMP-006` | 비-E2E runtime·unit 수정 완료 | remote weight publication과 selector process evidence |
| `ND-IMP-009` | 같은 Spot awaited request를 transport 제출 전에 거부하고 self-send FIFO를 보존; unit·TD-D6 process 충족 | 전체 common E2E inventory와 aggregate |
| `ND-IMP-010` | STREAM Framework error kind를 public string `code`로 인코딩; stream session unit 충족 | cross-process client-visible stream error evidence |
| `ND-IMP-011` | User Spot Ready route fence를 commit 뒤 기억하고 authority delete 뒤 제거; M6C·TD-D6 충족 | restart/recovery와 Config 13·14 process |
| `ND-E2E-IMP-001` | 미충족 | common 374개 exact ID와 Node exact 224개 scenario file의 coverage 불일치 해소; common coverage는 220개 |
| `ND-E2E-IMP-002` | 미충족 | Config 1-14 aggregate가 child 상태·client result·role-server evidence를 해석하도록 수정 |
| `ND-E2E-IMP-003` | 미충족 | 모든 selector와 feature-map 상태를 current common ID에 다시 대응하고 partial/source-only를 PASS에서 제외 |
| `ND-E2E-IMP-004` | 정적 scan 충족, process 후속 | client가 role-server public endpoint만 사용하고 양쪽 evidence를 같은 실행에서 생성하는지 확인 |
| `ND-E2E-IMP-005` | 미충족 | TA-A1 route fence 오류와 SA-E2E-14 native artifact 오류를 owner layer에서 해결하고 fresh process를 통과 |
| `ND-TEST-003` | E2E·aggregate·coverage·CI 미충족 | full test, coverage, verify:ci와 aggregate process 결과를 current candidate에서 종료 |

다음 항목은 현재 비-E2E 범위에서 닫혔으므로 active runtime gap으로 다시 열지 않았다.

- `ND-TEST-001`: checked-in public snapshot과 exact member comparison이 38/38로 통과했다.
- `ND-TEST-002`: current ledger path 기준 documentation regression이 17/17로 통과했다.
- `ND-REG-001`~`004`, `ND-REG-009`~`014`: contract, error, unknown content type, package graph,
  StoreVersion route replacement, weight validation, execution claim, STREAM error와 Ready route cache의
  비-E2E 회귀가 통과했다.
- `ND-IMP-007`: 등록되지 않은 ClientServer channel의 request가 public `NotFound`로 매핑되는
  production runtime과 CH05 process가 통과했다.
- `ND-IMP-008`: 알려진 ClientServer target의 transport가 끊겨도 descriptor를 `NotConnected`로
  유지하고 ready selector가 `Unavailable`을 반환하는 production runtime, unit과 CH07C process가
  통과했다.

`NS-IMP-*`, `NS-TEST-*`, `NS-REG-*`는 위 Framework gate가 닫힌 뒤 진행하는 sample layer다. 현재
sample의 static wire 정렬은 별도 진행됐지만, 이 log의 Framework runtime gap 수에는 합산하지 않았다.

## 현재 기준 manifest와 fresh evidence

```text
HEAD: b718b4d5cd7ed28f3e399339220b83431bdae902
branch: agent/framework-contract-runtime-update
status manifest: a63b101d2d86cde6bf554e986ed1903fc6253c8c45d2f09d524a2dedebc54caf
status entries: total=1 tracked-like=1 untracked=0 node_entries=0
Node/npm: v22.22.0 / 10.9.4
binding package version: 11.1.0
```

이 manifest는 이 log와 ledger를 추가하기 전의 현재 working tree를 가리킨다. 두 문서를 추가하면
working tree manifest가 다시 바뀐다.

| 명령 | 결과 | 판정 |
|---|---|---|
| `npm run build` | exit 0 | production와 browser build 통과 |
| `npm run typecheck` | exit 0 | 현재 TypeScript source 통과 |
| `contract-surface.test.js` | 38/38, exit 0 | declaration·runtime·package public contract 통과 |
| `backend-public-api-only.test.js` | 6/6, exit 0 | Framework·binding public boundary와 E2E client 정적 import boundary 통과 |
| `npm ls @zlink-systems/zlink --all` | exit 0 | 전체 workspace가 11.1.0으로 clean resolve |
| `verify_packaged_contract.sh` | exit 0 | `NODE_PACKAGED_CONTRACT_PASS packages=7 browser=esm server=commonjs` |
| `e2e-scenario-header-gate.test.js` | exit 1 | common 374, Node exact 224, common coverage 220, missing 154; source-only extra 4 |
| `TA-A1` | exit 1 | native session bind의 ActorRef route fence 불일치 |
| `SA-E2E-14` | exit 2 | candidate native artifact incomplete |
| `CH-E2E-01`~`CH-E2E-12` | targeted process PASS | Config 12의 16개 selector를 개별 실행했고 role endpoint/evidence를 확인 |
| `IS-E2E-36` | exit 2 | Config 14 role server `BLOCKED` |

실행 log는 다음 위치에 남아 있다.

- `e2e/ToActorMessaging/log/20260803-073603-76551/`
- `e2e/SubmitAdmission/log/20260803-073603-76563/`

## Common ID 전체 대조

현재 Node Client scenario file가 제공하는 exact ID는 224개다. 이 중 220개가 common E2E 문서의
374개 ID와 일치하고 4개는 Node source에만 존재한다. 비교 결과는 다음과 같다.

```text
common IDs:             374
Node exact IDs:         224
common coverage:        220
missing common IDs:     154
extra:          MON-A4, MON-D1, SM-D16, SM-Q9
```

누락 ID는 Config별로 다음과 같다. 이 목록은 source file의 존재가 아니라 common 문서의 `#### ID`
heading과 현재 Node Client scenario header를 exact 문자열로 비교한 결과다.

```text
Config 1  (1):  RM-A7
Config 2  (12): SM-A9, SM-A10, SM-A11, SM-A12, SM-A13, SM-B0, SM-B0A,
                SM-B10, SM-B11, SM-C6, SM-G5A, SM-G5B
Config 3  (17): PS-D1, PS-D2, PS-D3, PS-D4, PS-D5, PS-D6, PS-D7A, PS-D7B,
                PS-E1, PS-E2A, PS-E2B, PS-E2C, PS-F1, PS-F2, PS-F3, PS-F4, PS-F5
Config 4  (1):  RC-B6
Config 5  (19): RL-E1, RL-E2, RL-E3, RL-E4, RL-E5, RL-F1, RL-F2, RL-F3,
                RL-F4, RL-F5, RL-F6, RL-F7, RL-F8, RL-F9, RL-F10, RL-F11,
                RL-F12, RL-F13, RL-F14
Config 6  (18): SF-B3, SF-C3, SF-C4, SF-C5, SF-F1, SF-F2, SF-F3, SF-F4,
                SF-F5, SF-F6, SF-F7, SF-F8, SF-F9, SF-F10, SF-F11, SF-G1,
                SF-G2, SF-G3
Config 7  (5):  MON-A4A, MON-A4B, MON-A6, MON-D1A, MON-D1B
Config 8  (4):  TD-D4, TD-D5, TD-E2A, TD-F5A
Config 10 (12): ST-E1A, ST-E1B, ST-E1C, ST-F3A, ST-G1, ST-G2, ST-G3,
                ST-G4, ST-G5, ST-G6, ST-H4A, ST-H4B
Config 11 (9):  OBS-A5, OBS-C6, OBS-C7, OBS-C8, OBS-C9A, OBS-C9B, OBS-C10,
                OBS-C11, OBS-C12
Config 13 (20): SA-E2E-01, SA-E2E-02, SA-E2E-03, SA-E2E-04, SA-E2E-05,
                SA-E2E-06, SA-E2E-07, SA-E2E-08, SA-E2E-09, SA-E2E-10,
                SA-E2E-11, SA-E2E-12, SA-E2E-13, SA-E2E-14, SA-E2E-15,
                SA-E2E-16, SA-E2E-17, SA-E2E-18, SA-E2E-19, SA-E2E-20
Config 14 (36): IS-E2E-01, IS-E2E-02, IS-E2E-03, IS-E2E-04, IS-E2E-05,
                IS-E2E-06, IS-E2E-07, IS-E2E-08, IS-E2E-09, IS-E2E-10,
                IS-E2E-11, IS-E2E-12, IS-E2E-13, IS-E2E-14, IS-E2E-15,
                IS-E2E-16, IS-E2E-17, IS-E2E-18, IS-E2E-19, IS-E2E-20,
                IS-E2E-21, IS-E2E-22, IS-E2E-23, IS-E2E-24, IS-E2E-25,
                IS-E2E-26, IS-E2E-27, IS-E2E-28, IS-E2E-29, IS-E2E-30,
                IS-E2E-31, IS-E2E-32, IS-E2E-33, IS-E2E-34, IS-E2E-35,
                IS-E2E-36
```

Config 12의 16개 ID는 exact scenario file과 targeted process가 모두 PASS했다. Config 14의 36개 ID는
현재 runner가 존재해도 `BLOCKED`를 반환한다.
Config 13의 `SA-E2E-14`는 feature-map에 `구현`으로 표시되어 있지만 exact Client scenario file이
없고, current process도 native artifact 단계에서 실패한다. 따라서 feature-map 상태만으로 완료할 수
없다.

## Exact file가 있어도 완료로 볼 수 없는 feature-map 상태

다음 항목은 exact missing 수에 모두 포함되지는 않는다. feature-map이 `전환 필요`, `부분 구현`,
`미구현`, `재검증 필요` 또는 `RED`로 명시한 항목이며, 모두 `ND-E2E-IMP-003`의 selector·status·
process evidence gap에 포함한다.

```text
Config 2:  SM-B6, SM-C5, SM-C6, SM-D10, SM-D14
Config 3:  PS-A2, PS-D1..PS-D7, PS-E2
Config 7:  MON-A1..MON-A5, MON-B1, MON-B2, MON-C1, MON-D1
Config 8:  TD-A1..TD-G1 전체
Config 1:  RM-C9
Config 9:  TA-A1 current process failure (ActorRef route fence)
Config 10: ST-B1, ST-B3, ST-E1A, ST-F3A, ST-G1..ST-G6,
           ST-H1, ST-H3, ST-H4, ST-H4A, ST-H4B, ST-H5, ST-I1, ST-I2, ST-I3
Config 11: OBS-C1..OBS-C11
Config 12: CH-E2E-01..CH-E2E-12 targeted process PASS
Config 13: SA-E2E-01..SA-E2E-20 전체 exact scenario 미충족;
           SA-E2E-14는 native artifact incomplete
Config 14: IS-E2E-01..IS-E2E-36 전체 BLOCKED

## Config 12 구현 전 설계 검토(R0 candidate preparation)

Config 12는 기존 RouteMesh·ClientServer public surface를 재사용하는 process fixture로 구현한다. 역할별
process가 각각 `zlinkFramework()` builder로 topology와 role을 등록하고, E2E client는 역할 process의
HTTP application endpoint만 호출한다. Handler는 `ZLinkRouteClient`와 Spot outbound를 통해 downstream을
호출하며, evidence는 handler가 기록한 application marker로만 판정한다.

검토한 대안은 두 가지다.

1. 하나의 host 안에 모든 role과 모든 handler를 함께 등록한다. 구현량은 적지만 process 경계, discovery,
   restart와 role 누락을 검증할 수 없으므로 Config 12의 핵심 계약을 숨긴다.
2. Session, Play, API, Workflow, Audit와 caller를 독립 host로 나눈다. runner가 각 host를 재시작·종료할
   수 있고 role별 public status와 evidence를 직접 비교할 수 있다. 포트·Store namespace와 cleanup이
   늘어나지만 공통 spec의 배포 경계를 그대로 보존하므로 이 대안을 선택한다.

DDD 경계는 `ChannelEgress` fixture(역할과 packet 계약), role host(Framework registration과 application
   endpoint), scenario client(검증 절차), runner(process·port·Store 제어)로 분리한다. Runner가 내부
   egress index나 Core descriptor를 읽지 않도록 하며, role host는 public runtime/status만 반환한다.
   POSD 위험 신호는 scenario별 조건을 Framework production API에 추가하는 특수 분기, handler 인자를
   그대로 넘기는 pass-through endpoint, fixed sleep으로 readiness를 추정하는 temporal coupling이다.
   각 조건은 fixture 내부 marker와 bounded polling으로 흡수한다.
```

`SM-D16`, `SM-Q9`, `MON-A4`, `MON-D1`은 current Node source에만 있는 extra ID다. common contract의
대체 ID로 계산하지 않고, 공통 문서에 추가할 계약인지 internal·diagnostic 범위인지 별도로 결정해야
한다.

## 다음 조치

1. `ND-E2E-IMP-001`을 기준으로 154개 누락 ID, 4개 extra와 feature-map alias를 정리한다.
2. Config 12·14는 공통 contract가 요구하는 role server를 구현하거나 `contract 선행`으로 분리하고,
   aggregate가 `BLOCKED`·partial·source-only를 PASS로 계산하지 않도록 한다.
3. `ND-E2E-IMP-005`의 TA-A1 route fence와 SA native artifact를 owner layer에서 수정한 뒤 fresh
   package로 client result와 role-server evidence를 함께 확인한다.
4. `ND-TEST-003`에 따라 full regression, coverage, CI와 aggregate process를 같은 candidate에서
   실행한다.

## 11.10 checklist에 반영한 static evidence

다음 항목은 실제 sample process 완료가 아니라 current source·static regression 범위만 확인했다.

- DeliveryDispatch public Actor/topology gate: 2/2 통과
- GameQuest typed topology gate: 1/1 통과
- Node sample automatic handler registration: 1/1 통과
- ZoneWorld logical handler·role gate: 9/9 통과

따라서 checklist의 DeliveryDispatch, GameQuest와 automatic handler registration만 `[x]`로 표시했고,
일곱 sample process smoke와 client·role-server evidence 항목은 미완료로 유지했다.

## 2026-08-03 runtime gap 후속 구현과 unit 재검증

초기 재감사 뒤 production call path를 다시 실행하면서 두 가지 비-E2E runtime 차이를 추가로
확인했다. 둘 다 sample이나 E2E 호출부에 우회 코드를 넣지 않고 Framework 소유 계층에서 수정했다.

| 항목 | 수정 내용 | 회귀 test |
|---|---|---|
| Instance terminal StoreVersion 전이 | terminal completion이 같은 authority owner와 object generation을 유지하면서 StoreVersion만 바꿀 때, 이전 direct spot route cache를 exact fence로 제거한 뒤 새 route를 publish하도록 수정했다. Recovery completion도 반환된 route의 spot·node·lease·StoreVersion fence를 새 `spotRoute`에 반영한다. | `m6b-runtime.contract.ts`: Promise activation completion과 authority recovery의 StoreVersion 전이 assertion |
| RouteMesh weight validation | Nest RouteMesh Server builder가 `0..10000`을 즉시 검증하고, 실행 중 placement/channel weight 변경이 잘못된 값에 대해 public `ZLinkConfigurationException`을 반환하도록 수정했다. invalid 값이 backend까지 전달되지 않는다. | `nestjs-module.test.js` 62/62, `backend-contract.test.js` 37/37 |

StoreVersion 전이는 수정 전 fresh CH03 process에서 `ServiceStaleGenerationError`로 재현됐고,
수정 후 `./run_e2e.sh CH03`이 통과했다. 현재 runtime/unit 범위에서 다시 실행한 결과는 다음과 같다.

| Gate | 결과 |
|---|---:|
| `npm run build` | PASS |
| `npm run typecheck` | PASS |
| `npm run lint` | PASS |
| `npm run verify:m5-foundation` | 5/5 PASS |
| `npm run verify:m6a-runtime` | 27/27 PASS |
| `npm run verify:m6b-runtime` | 43/43 PASS |
| `npm run verify:m6c-runtime` | 79/79 PASS |
| E2E·sample·browser·integration·native child process를 제외한 unit inventory | 59 files, 1003/1003 PASS |
| Nest module contract | 62/62 PASS |
| backend contract | 37/37 PASS |

이 결과는 production source와 unit/contract evidence를 닫은 것이다. 공통 374개 scenario inventory,
Config 12·14 process, native artifact, full coverage·CI와 11.10 sample checklist는 이 실행에 포함하지
않았으므로 완료로 승격하지 않는다.

## 2026-08-03 ClientServer error·disconnect runtime 후속

runtime·unit phase 뒤 Config 12의 실제 request 경계를 다시 실행하면서 두 가지 production call-path
차이를 확인하고 owner layer에서 수정했다.

| 항목 | 수정 내용 | fresh evidence |
|---|---|---|
| `ND-IMP-007` missing ClientServer channel mapping | 등록되지 않은 channel을 내부 `RequestTargetNotFound`에서 public `NotFound`로 매핑하고 실제 configuration conflict와 구분했다. | `channel-client.test.js` 93/93, Config 12 `CH05` process PASS |
| `ND-IMP-008` known target without ready transport | unexpected disconnect에서 discovery descriptor를 삭제하지 않고 내부 `disconnected` 상태로 보존한다. reconnect admission은 같은 descriptor revision에서 허용하고, known target에 ready selector가 없으면 `RouteNotConnected`/public `Unavailable`을 반환한다. 이미 끊긴 transport의 unexpected cleanup에서는 explicit `disconnect()`를 다시 호출하지 않고 disposal만 수행한다. | `m6a-runtime.contract.ts` 27/27, 59-file unit inventory 1003/1003, Config 12 `CH07C` process PASS |

CH07C runner는 server process를 `SIGSTOP`한 뒤 established ClientServer TCP connection을 닫고,
owner lease가 stale target을 먼저 제거하지 않도록 fixture TTL을 30초로 설정한다. ClientServer
liveness deadline에서 target state `NotConnected`가 관찰된 뒤 caller request가 public
`Unavailable`으로 종료되는지 확인했다. 초기 candidate에서 같은 상황의 native cleanup segfault가
두 차례 재현됐지만, unexpected disconnect 경로에서 explicit transport disconnect를 제거한 뒤
재실행은 PASS했다.

Config 12의 targeted process 결과는 다음과 같다.

```text
CH01 CH02 CH03 CH04A CH04B CH04C CH05 CH06 CH07A CH07B CH07C CH08 CH09 CH10 CH11 CH12: PASS
```

CH09는 runner가 session role을 시작하지 않아 port 1 fallback URL을 조회하던 fixture 오류도 함께
수정했다. Config 12의 16개 scenario는 현재 개별 runner에서 통과하지만, common 374개 exact inventory,
Config 14, aggregate·coverage·CI와 전체 Framework gate의 완료 판정은 별도로 유지한다.

동일 runner의 `bash run_e2e.sh ALL`도 실행했다. CH01~CH07B까지는 통과했지만 CH07C에서 caller
native process가 `Segmentation fault (core dumped)`로 종료되어 aggregate는 실패했다. 따라서 위의
16/16 결과는 독립 selector의 targeted evidence로만 사용하고, aggregate PASS나 native artifact
완료로 승격하지 않는다.

추가로 ChannelName의 내부 `trySend` 경로와 public 비동기 `send` 경로를 common spec §3.2와
대조했다. `trySend`는 readiness를 기다리지 않고 현재 선택 결과만 즉시 반환하는 fast-fail 경로이며,
공통 계약의 bounded readiness wait는 public 비동기 `send`에 적용된다. 따라서 `trySend`의
disconnected 상태 즉시 반환은 별도 runtime gap으로 분류하지 않았다.

## 2026-08-03 순서 재정렬 후 live runtime/unit 결과

사용자 지시에 따라 현재 작업 순서를 `runtime gap 구현·unit -> sample spec -> 전체 E2E`로 고정했다.
runtime card마다 DDD state owner와 invariant, POSD red flag와 두 가지 대안은
[`runtime POSD·DDD review`](2026-08-03-runtime-posd-ddd-review.ko.md)에 기록했다.

### Runtime card 결과

| card | 결과 | 직접 evidence |
|---|---|---|
| `ND-IMP-009` | runtime·unit·TD-D6 process PASS | entry serial dispatch 25/25; `InvalidOperation` pre-submit과 self-send FIFO assertion |
| `ND-IMP-010` | runtime·unit PASS | stream session runtime 51/51; Framework kind를 `NotFound` string code로 인코딩 |
| `ND-IMP-011` | runtime·unit·TD-D6 process PASS | M6C 79/79; Ready route `remember -> publish`, close `delete -> forget` assertion |

### Runtime gate의 경계 보정

전체 `npm test`는 browser와 contract를 포함한 뒤 `e2e-scenario-header-gate.test.js`에서 다음 exact
inventory를 보고 exit 1로 종료했다. 이는 production runtime regression 실패가 아니라 다음 sample/E2E
단계의 미완료 조건이다.

```text
common IDs:        374
Node exact IDs:    224
common coverage:   220
missing:           154
extra:             MON-A4, MON-D1, SM-D16, SM-Q9
```

그 전에 runtime gate가 발견한 정적 boundary 문제도 owner 경계에서 정리했다. Config 12 Client의
local `fetch` wrapper를 제거하고 공통 `e2e/http-client.ts`의 public `@zlink-systems/http-client`
경로를 사용하도록 바꿨으며, ChannelEgress runner의 local readiness/settle budget을 공통 policy로
맞췄다. `e2e-http-client-adoption.test.js` 1/1과 `e2e-local-wait-policy-gate.test.js` 3/3이 통과했다.

### Message trace와 file log evidence

TD-D6 process는 `e2e/AutomaticTurnDispatch/run_e2e.sh TD-D6`로 실행했고 다음 directory에 결과를
남겼다.

`framework/languages/node/e2e/AutomaticTurnDispatch/log/20260803-102712-710905/`

- `session-a-flow.log`: EnsureSpot와 ProbeReq가 `received -> dispatched`로 처리되고, self-cycle
  request가 error 없이 transport에 제출되지 않으며, self-send가 FIFO로 이어지는 message trace
- `play-a-flow.log`: EnsureSpot와 evidence polling의 `received -> replied` trace
- `play-a.evidence.log`, `session-a.evidence.log`: `self-cycle-rejected`, `self-send-started`,
  `self-send-completed`, `probe-completed`의 client-visible/server evidence

따라서 runtime/unit phase의 current 판정은 `ND-IMP-001`~`011` 구현·focused regression PASS다.
sample spec 정렬과 154개 exact E2E 누락, Config 13·14 native/role runner, aggregate·coverage·CI는
다음 단계로 남겨 두며 완료표시하지 않는다.
