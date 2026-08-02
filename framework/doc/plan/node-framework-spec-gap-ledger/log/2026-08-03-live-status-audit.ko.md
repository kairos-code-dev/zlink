# Node.js Framework live status audit — 2026-08-03

이 기록은 2026-08-03 현재 working tree에서 Node ledger의 진행 상태를 다시 확인한 결과다.
공유 working tree에는 다른 workstream의 변경도 함께 있으므로, 아래 manifest는 Node candidate의
commit이나 완료 판정이 아니다. 이전 log와 source의 과거 상태는 현재 evidence로 재사용하지 않았다.

## 현재 판정

- Framework spec gate는 `NOT CLEAN`이다. public contract·build·package의 비-E2E 범위는 통과했지만,
  common E2E exact inventory, process E2E와 native artifact 조건이 남아 있다.
- common E2E inventory는 374개이고 Node scenario file이 제공하는 exact ID는 207개다. 171개가
  누락되고 4개(`MON-A4`, `MON-D1`, `SM-D16`, `SM-Q9`)가 common inventory 밖에 있다.
- `run_e2e_all.sh`의 기본 목록은 이제 Config 1-14를 포함한다. 그러나 Config 12와 Config 14
  runner는 각각 `BLOCKED`로 exit 2를 반환하므로 aggregate process 완료는 아니다.
- Node public contract와 package consumer는 이번 audit에서 통과했다. 실제 role-server process가
  같은 native artifact를 사용했다는 증거는 아직 없다.
- Sample은 `NS-IMP-001`~`003`의 source/static wire 정렬까지 진행됐다. 일곱 sample의 process
  완료는 확인되지 않았고, 현재 bounded sample aggregate는 Bingo replacement peer의
  `ConnectionReady` 대기 timeout으로 실패했다.

## 기준과 dirty manifest

```text
HEAD: ee1dbccbb5ba72a9defc4158206b4fd23fa36c62
branch: agent/framework-contract-runtime-update
status manifest: 631df235814da8b688634d30614a852b68c67339a9f81b802204a832f5c446f4
status entries: total=727 tracked=702 untracked=25
Node entries=279, Node E2E entries=267, Node sample entries=10
Node/npm: v22.22.0 / 10.9.4
binding archive: .artifacts/wsl/npm/zlink-systems-zlink-11.1.0.tgz
binding archive sha256: 1350527e68a2a490b1e0ca888fd2ba65d90938dd2d29385c1509fd997ef4a945
```

새로 생성된 process log와 기존 변경은 삭제하거나 되돌리지 않았다. 이 manifest는 ledger 문서
수정 전 audit 시점의 값이며, 이 log와 ledger를 추가하면 working tree hash가 달라진다.

## Fresh command evidence

| 명령 | 결과 | 의미 |
|---|---|---|
| `npm run build` | exit 0 | production package와 browser build 통과 |
| `npm run typecheck` | exit 0 | 현재 TypeScript source typecheck 통과 |
| `node --test --test-force-exit test/contract/contract-surface.test.js` | 32/32, exit 0 | current public contract snapshot·exact member test 통과 |
| `node --test --test-force-exit test/contract/backend-public-api-only.test.js` | 6/6, exit 0 | E2E client package 정적 경계 통과 |
| `node --test --test-force-exit test/contract/documentation-regression.test.js` | 17/17, exit 0 | current Node ledger path와 문서 링크 regression 통과 |
| `npm run test:browser` | Chromium 1/1, exit 0 | browser transport smoke 통과. 전체 process 계약 증거는 아님 |
| `npm ls @zlink-systems/zlink --all` | exit 0 | root와 workspace가 11.1.0을 clean하게 resolve |
| `./scripts/verify_packaged_contract.sh` | exit 0 | `NODE_PACKAGED_CONTRACT_PASS packages=7 browser=esm server=commonjs` |
| `node --test --test-force-exit test/contract/e2e-scenario-header-gate.test.js` | exit 1 | common 374, implemented 207, missing 171, extra 4 |
| `./e2e/ToActorMessaging/run_e2e.sh TA-A1` | exit 1 | readiness 이후 browser client native session bind가 ActorRef route fence 불일치로 실패 |
| `./e2e/SubmitAdmission/run_e2e.sh SA-E2E-14` | exit 2 | package version은 11.1.0이지만 candidate native artifact가 incomplete |
| `./e2e/ChannelEgressRouting/run_e2e.sh CH-E2E-01` | exit 2 | Config 12 Node role server `BLOCKED` |
| `./e2e/InstanceSpot/run_e2e.sh IS-E2E-36` | exit 2 | Config 14 Node role server `BLOCKED` |
| sample static·bounded regression command | 50 pass, 1 fail, exit 1 | Bingo replacement peer `ConnectionReady` 대기 timeout |

TA-A1의 process log는 `framework/languages/node/e2e/ToActorMessaging/log/20260803-071317-29703/`,
SA-E2E-14의 log는 `framework/languages/node/e2e/SubmitAdmission/log/20260803-071317-29720/`에
남아 있다.

## 현재 source/static 정렬

- Node E2E Client subtree에서 `@zlink-systems/framework`와 `@zlink-systems/zlink` 직접 import는
  각각 0개 file로 확인했다. 이는 정적 import 경계의 개선이며, process role boundary의 완료를
  증명하지는 않는다.
- TicTacToe는 `LeaveGameMsg`, SupportChat은 `SetTypingMsg`를 사용한다.
- DeliveryDispatch는 공통 계약의 `BindCourierSessionReq/Res`를 사용하고, client-facing notify와
  decision에 `sessionRoute`를 넣지 않는다. `attempt`는 공통 계약대로 offer/result 경계에 남고,
  상태 timestamp는 `occurredAtUnixMs: number`를 사용한다.
- GameQuest는 `GameplayEventPayload` object와 `ClosePlayerQuestMsg`를 사용한다. 이전
  `number[]` payload와 호출부 `TextEncoder`/`TextDecoder` 경로는 현재 source에서 확인되지 않았다.

이 정렬은 source와 static regression evidence다. sample process, client-visible result, role-server
state owner와 cleanup을 확인하기 전에는 `NS-IMP-*` 전체를 `충족`으로 표시하지 않는다.

## 다음 closure 조건

1. common 374개 exact ID와 Node feature-map·selector를 다시 대조하고, extra ID와 상태 enum을
   정리한다.
2. Config 12·14의 role server를 구현하거나 명시적인 contract 선행 상태로 유지하고, aggregate가
   child exit code만으로 PASS를 만들지 않는지 검증한다.
3. TA-A1의 ActorRef route fence 오류를 owner layer에서 재현·수정하고, client result와 role-server
   evidence를 같은 process에서 확인한다.
4. SubmitAdmission이 현재 Core runtime과 일치하는 native artifact를 package에 포함하는지 확인한
   뒤 SA-E2E-14를 다시 실행한다.
5. Bingo timeout을 포함한 일곱 sample runner와 `NS-REG-001`~`017`, full regression, coverage,
   CI와 R2/R3 independent review를 fresh candidate에서 다시 수행한다.
