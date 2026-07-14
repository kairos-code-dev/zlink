# Node.js framework public contract gap 구현 로그

이 문서는 Node.js 작업의 시간순 실행 기록을 보관한다. 현재 작업 상태와 완료 여부는
[`framework-public-contract-gap-implementation.ko.md`](../../framework-public-contract-gap-implementation.ko.md)의
진행표에서 확인한다.

| 실행 시각 | gate | 기준 commit | 명령 또는 검토 | exit code | 결과 | 증거 |
|-----------|------|-------------|----------------|-----------|------|------|
| 2026-07-13 | G0-G2 | 작업 tree | 정식 Node 계약 문서 3개 inventory, public export/package 검토 | 0 | PASS | `node-g0-contract-ledger.ko.md`; contract surface와 binding public-only tests |
| 2026-07-13 | G3 | 작업 tree | `npm test`와 `npm run verify:ci` | 0 | PASS | build, typecheck, lint와 contract/unit/integration 전체 파일별 실행 성공 |
| 2026-07-13 | G3 | 작업 tree | `npm run verify:coverage` | 0 | PASS | all files lines 82.36%, branches 81.57%, functions 82.16% |
| 2026-07-13 | G4 | 작업 tree | DDD/POSD production source 재검토 | 0 | PASS | `node-g4-refactoring-ledger.ko.md`; `NO DDD/POSD FINDINGS` |
| 2026-07-13 | G5 | 작업 tree | `npm run verify:samples` | 0 | PASS | TicTacToe, Bingo, DeliveryDispatch, SupportChat, GameQuest, ShoppingMall |
| 2026-07-13 | G6 | 작업 tree | `./e2e/run_e2e_all.sh` | 0 | PASS | config 1~11의 공통 scenario 181개, aggregate `total PASS (689s)`; 상세 selector와 로그는 Node G6 ledger |
| 2026-07-13 | runtime | 작업 tree | DSC-008 단독 process 종료 회귀 검증 | 0 | PASS | 정상 drain 뒤 deadline timer를 해제해 약 30.6초에서 0.61초로 단축 |
| 2026-07-13 | G3/G4 | 작업 tree | Node 20/22 `channel-client.test.js` 반복 실행 | 0 | PASS | 각 runtime에서 65/65 PASS; subscriber poller와 context 종료 순서 수정 뒤 멈춤 없음 |
| 2026-07-13 | G3 | 작업 tree | `npm run verify:runtime-matrix` | 0 | PASS | Node 20과 Node 22 전체 runtime gate PASS |
| 2026-07-13 | bindings | 작업 tree | Node local package 8.6.6 생성·설치 | 0 | PASS | package 8.6.6, runtime 9.0.0; .NET 8.6.6 native payload와 SHA-256 일치 |
| 2026-07-13 | G6 | 작업 tree | `npm run verify:cross-language` | 0 | PASS | public API만 사용한 channel/fanout/stream/flow/session-closing/Redis draining 양방향 9개 marker PASS |
| 2026-07-13 | G3 | 작업 tree | `npm run verify:runtime-matrix`, `npm run verify:abi-matrix` | 0 | PASS | Node 20/22 전체 gate와 ABI 선언·연결 검증 PASS |
| 2026-07-13 | G1/G7 | 작업 tree | `scripts/verify_packaged_contract.sh` | 0 | PASS | bindings 8.6.6 tarball을 포함한 clean consumer, packages=7 |
| 2026-07-13 | G5 | 작업 tree | `npm run verify:samples` | 0 | PASS | TicTacToe, Bingo, DeliveryDispatch, SupportChat, GameQuest, ShoppingMall 전체 PASS |
| 2026-07-13 | G7 | 작업 tree | `npm run verify:release` | 0 | PASS | CI, sample, runtime matrix와 cross-language gate 재실행 PASS |
| 2026-07-13 | G7 | 작업 tree | package metadata와 제거 대상 symbol 재검토 | 0 | PASS | contract/supporting package는 publishable이며 license 포함; 별도 HTTP client는 계획 분모 밖이라 private 유지 |
| 2026-07-13 | 환경 정리 | 작업 tree | `docker container prune -f`, `docker volume prune -af` | 0 | PASS | Docker volume 0개; 다른 언어가 실행 중인 Java E2E container는 제거하지 않음 |
| 2026-07-13 | G2 | 작업 tree | one-way `submit()`의 runtime/queue 동기 수락 계약 회귀 수정 | 0 | PASS | runtime 미시작, queue full, 즉시 transport 오류가 동기 예외이며 `channel-client.test.js` 69/69 PASS |
| 2026-07-13 | G0 | 작업 tree | binding 중앙 pin, lock, 설치 graph를 9.0.1로 정렬 | 0 | PASS | archive SHA-256 `f0d15bac...19f31e`; `npm ls @zlink-systems/zlink --all`에서 9.0.1 단일 resolve |
| 2026-07-13 | G6 | 작업 tree | 보완한 `npm run verify:cross-language` | 0 | PASS | fanout, route-mesh, STREAM session-closing, Redis row의 Node↔`.NET` 양방향 13개 marker PASS |
| 2026-07-14 | G4 | 작업 tree | browser/runtime 변경 후 DDD/POSD adversarial 재검토 | 0 | PASS | coalesced frame 분리, session actor binding 교체와 publish readiness가 기존 책임 경계 안에 있으며 `NO DDD/POSD FINDINGS` |
| 2026-07-14 | G6 | 작업 tree | `./e2e/run_e2e_all.sh` | 0 | PASS | config 1~11의 공통 scenario 181개, aggregate `total PASS (1135s)`; ST-F3와 SM-C4 수정 후 전체 재실행 |
| 2026-07-14 | G6 | 작업 tree | Chromium 집중 반복 검증 | 0 | PASS | GameQuest 20/20, SpotActorTransfer ST-F3 10/10, SpotService SM-C4 10/10 |
| 2026-07-14 | G3 | 작업 tree | `npm run verify:coverage` | 0 | PASS | all files lines 82.67%, branches 81.98%, functions 82.51% |
| 2026-07-14 | G3 | 작업 tree | `npm run verify:ci` | 0 | PASS | build, typecheck, lint와 CI 대상 contract/unit/integration 전체 PASS |
| 2026-07-14 | G1/G7 | 작업 tree | `./scripts/verify_packaged_contract.sh` | 0 | PASS | `NODE_PACKAGED_CONTRACT_PASS packages=7 browser=esm server=commonjs` |
| 2026-07-14 | G7 | 작업 tree | `npm run verify:release` | 0 | PASS | ABI, P0, sample, Node 20/22 runtime matrix와 cross-language 13개 marker PASS |
| 2026-07-14 | G4/G7 | 작업 tree | 최종 contract, package와 source 재검토 | 0 | PASS | browser-only connector와 framework gap 범위에 미해결 finding 없음; Node.js G0~G7 완료 |
