# Node Framework runtime·spec gap 전체 재감사 — 2026-08-03

이 log는 현재 working tree의 Node Framework production contract, runtime, package, E2E inventory와
process blocker를 다시 대조한 결과다. 기존 dirty change를 되돌리지 않았으며, 과거 log의 PASS나
feature-map의 `implemented` 표시는 현재 process evidence로 재사용하지 않았다.

## 현재 결론

초기 production declaration·runtime·package의 비-E2E 비교에서는 새 public contract mismatch가 없었지만,
completion 전이와 runtime weight validation을 실제 production call path까지 다시 대조하면서
`ND-IMP-005`와 `ND-IMP-006`을 추가로 확인했다. 두 항목은 owner layer 수정과 unit regression까지
완료했다. `ND-IMP-001`~`006`의 process·native artifact evidence와 Framework spec gate의 전체 판정은
아직 `NOT CLEAN`이다.

현재 열려 있는 Framework runtime·spec gap은 다음과 같다.

| ID | 현재 상태 | 남은 조건 |
|---|---|---|
| `ND-IMP-001` | 비-E2E runtime 충족 | `configureInboundDispatch`를 사용하는 실제 role-server process와 admission/preflight 순서 evidence |
| `ND-IMP-002` | 비-E2E runtime 충족 | client-visible error kind/reason과 role-server terminal mapping의 process evidence |
| `ND-IMP-003` | 비-E2E runtime 충족 | 실제 channel·spot process에서 unknown content type의 `ProtocolError`, handler 0회와 callback exactly-once evidence |
| `ND-IMP-004` | package graph·clean consumer 충족 | current native artifact를 포함한 role server와 SubmitAdmission process 실행 |
| `ND-IMP-005` | 비-E2E runtime·unit 수정 완료 | terminal/recovery completion의 StoreVersion route replacement process evidence |
| `ND-IMP-006` | 비-E2E runtime·unit 수정 완료 | remote weight publication과 selector process evidence |
| `ND-E2E-IMP-001` | 미충족 | common 374개 exact ID와 Node 207개 scenario file의 coverage 불일치 해소 |
| `ND-E2E-IMP-002` | 미충족 | Config 1-14 aggregate가 child 상태·client result·role-server evidence를 해석하도록 수정 |
| `ND-E2E-IMP-003` | 미충족 | 모든 selector와 feature-map 상태를 current common ID에 다시 대응하고 partial/source-only를 PASS에서 제외 |
| `ND-E2E-IMP-004` | 정적 scan 충족, process 후속 | client가 role-server public endpoint만 사용하고 양쪽 evidence를 같은 실행에서 생성하는지 확인 |
| `ND-E2E-IMP-005` | 미충족 | TA-A1 route fence 오류와 SA-E2E-14 native artifact 오류를 owner layer에서 해결하고 fresh process를 통과 |
| `ND-TEST-003` | E2E·aggregate·coverage·CI 미충족 | full test, coverage, verify:ci와 aggregate process 결과를 current candidate에서 종료 |

다음 항목은 현재 비-E2E 범위에서 닫혔으므로 active runtime gap으로 다시 열지 않았다.

- `ND-TEST-001`: checked-in public snapshot과 exact member comparison이 38/38로 통과했다.
- `ND-TEST-002`: current ledger path 기준 documentation regression이 17/17로 통과했다.
- `ND-REG-001`~`004`, `ND-REG-009`~`010`: contract, error, unknown content type, package graph,
  StoreVersion route replacement와 weight validation의 비-E2E 회귀가 통과했다.

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
| `e2e-scenario-header-gate.test.js` | exit 1 | common 374, Node 207, missing 171, extra 4 |
| `TA-A1` | exit 1 | native session bind의 ActorRef route fence 불일치 |
| `SA-E2E-14` | exit 2 | candidate native artifact incomplete |
| `CH-E2E-01` | exit 2 | Config 12 role server `BLOCKED` |
| `IS-E2E-36` | exit 2 | Config 14 role server `BLOCKED` |

실행 log는 다음 위치에 남아 있다.

- `e2e/ToActorMessaging/log/20260803-073603-76551/`
- `e2e/SubmitAdmission/log/20260803-073603-76563/`

## Common ID 전체 대조

현재 Node Client scenario file가 제공하는 exact ID는 207개다. common E2E 문서의 374개 ID와 비교한
결과는 다음과 같다.

```text
common IDs:     374
Node exact IDs: 207
missing:        171
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
Config 8  (5):  TD-D4, TD-D5, TD-D6, TD-E2A, TD-F5A
Config 10 (12): ST-E1A, ST-E1B, ST-E1C, ST-F3A, ST-G1, ST-G2, ST-G3,
                ST-G4, ST-G5, ST-G6, ST-H4A, ST-H4B
Config 11 (9):  OBS-A5, OBS-C6, OBS-C7, OBS-C8, OBS-C9A, OBS-C9B, OBS-C10,
                OBS-C11, OBS-C12
Config 12 (16): CH-E2E-01, CH-E2E-02, CH-E2E-03, CH-E2E-04A, CH-E2E-04B,
                CH-E2E-04C, CH-E2E-05, CH-E2E-06, CH-E2E-07A, CH-E2E-07B,
                CH-E2E-07C, CH-E2E-08, CH-E2E-09, CH-E2E-10, CH-E2E-11, CH-E2E-12
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

Config 12의 16개 ID와 Config 14의 36개 ID는 현재 runner가 존재해도 각각 `BLOCKED`를 반환한다.
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
Config 12: CH-E2E-01..CH-E2E-12 전체 BLOCKED
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

1. `ND-E2E-IMP-001`을 기준으로 171개 누락 ID, 4개 extra와 feature-map alias를 정리한다.
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
| `npm run verify:m6a-runtime` | 26/26 PASS |
| `npm run verify:m6b-runtime` | 43/43 PASS |
| `npm run verify:m6c-runtime` | 79/79 PASS |
| E2E·sample·browser·integration·native child process를 제외한 unit inventory | 59 files, 1003/1003 PASS |
| Nest module contract | 62/62 PASS |
| backend contract | 37/37 PASS |

이 결과는 production source와 unit/contract evidence를 닫은 것이다. 공통 374개 scenario inventory,
Config 12·14 process, native artifact, full coverage·CI와 11.10 sample checklist는 이 실행에 포함하지
않았으므로 완료로 승격하지 않는다.
