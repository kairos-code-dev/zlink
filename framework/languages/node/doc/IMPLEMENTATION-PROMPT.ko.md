# ZLink Framework for Node.js — 끝까지 구현하는 실행 프롬프트

이 문서는 `IMPLEMENTATION-PLAN.ko.md` 를 기준으로 Node.js/NestJS 버전의
ZLink Framework 를 dotnet framework 와 같은 구조, 기능, 사용성, 샘플 수준까지
완료하기 위해 새 작업 세션에 그대로 전달할 수 있는 프롬프트다.

이 프롬프트의 목적은 단일 기능 구현이 아니라, plan 문서가 요구하는 모든 작업을 끝까지
닫는 것이다. 따라서 중간 Phase 가 통과해도 전체 완료로 보지 않는다. 구현, 테스트, 문서,
샘플, cross-language smoke, release gate, dotnet 대비 parity 감사가 모두 끝났을 때만
완료로 판단한다.

## 실행 프롬프트

```text
너는 /home/hep7/project/kairos/zlink 저장소에서 Node.js/NestJS 버전의
ZLink Framework 를 구현하는 코딩 에이전트다.

목표:
- framework/languages/node/doc/IMPLEMENTATION-PLAN.ko.md 를 제어 문서로 사용한다.
- P0~P9 전체 Phase 와 각 Phase 의 POSD 게이트를 모두 통과시킨다.
- 최종 완료 기준은 dotnet framework 와 구조, 기능, 사용성, 샘플 4축이 동등한 것이다.
- 기준 구현은 framework/languages/dotnet/src 이다. 문서와 코드가 다르면 dotnet 코드를 따른다.
- 단순히 일부 테스트가 통과했다고 완료하지 않는다. plan 의 모든 Phase, regression matrix,
  sample, guide, cross-language smoke 까지 닫혀야 완료다.
- 작업이 길어져도 중간 상태를 완료로 보고하지 않는다. 남은 항목을 계속 추적하고,
  검증 가능한 단위로 구현, 테스트, 리뷰, 문서 갱신, 커밋, 푸시를 반복한다.
- 한 번의 세션에서 전부 끝나지 않으면, 마지막 보고에 완료한 Phase, 통과한 gate,
  남은 Phase, 다음에 바로 실행할 명령을 남긴다. 다음 세션은 그 지점부터 이어서 진행한다.
- 계획 문서의 체크박스는 진행 단서일 뿐 최종 증거가 아니다. 체크되어 있어도 실제 코드,
  dotnet 대응 코드, 테스트, 샘플 실행 결과로 다시 검증한다.
- "현재 가능한 수준"이나 "대체로 완료"로 멈추지 않는다. dotnet 대비 빠진 항목이 0 이
  될 때까지 구현, 리뷰, 수정, 검증을 반복한다.

반드시 먼저 읽을 문서:
1. framework/languages/node/doc/IMPLEMENTATION-PLAN.ko.md
2. framework/languages/node/doc/README.ko.md
3. framework/languages/node/doc/sample-implementation-plan.ko.md
4. framework/languages/node/doc/internals/dotnet-to-node-surface-mapping.ko.md
5. framework/languages/node/doc/internals/backend-dependency-policy.ko.md
6. framework/languages/node/doc/internals/lifecycle-and-failure-semantics.ko.md
7. framework/languages/node/doc/internals/regression-test-matrix.ko.md
8. 현재 Phase 가 지정한 spec 문서
9. 모호한 부분의 dotnet 대응 코드

시작 직후 상태 확인:
- `git status --short` 로 dirty tree 를 확인한다.
- unrelated dirty changes 는 사용자가 만든 것으로 보고 되돌리지 않는다.
- Node 작업 범위는 기본적으로 `framework/languages/node` 이다. binding public API gap 을
  닫아야 할 때만 `bindings/node` 를 함께 수정한다.
- 이미 구현된 Phase 는 plan 의 체크리스트, 테스트, 실제 코드로 재검증한 뒤 이어서 진행한다.

작업 규칙:
- Phase 순서를 지킨다. P0 → P1 → P1.5 → P2 → P3 → P4 → P5 → P6 → P7 → P8 → P9.
- 각 Phase 는 구현, DoD 검증, POSD 리팩토링 게이트, 재검증을 모두 통과해야 완료다.
- 게이트에서 이슈가 남으면 다음 Phase 로 넘어가지 말고 같은 Phase 안에서 수정한다.
- review-only 상태로 끝내지 않는다. 명확한 이슈가 발견되면 같은 턴에서 수정하고,
  수정 후 같은 기준으로 다시 리뷰한다. 이슈가 0 이 될 때까지 이 루프를 반복한다.
- provider token 만 노출하고 실제 기능이 unavailable placeholder 로 남아 있으면 완료가 아니다.
  DI 에서 꺼낸 client, manager, resolver, outbound 가 dotnet 과 같은 실제 동작 경로에
  연결되는지 테스트로 증명한다.
- backend 의존은 adapter 한 층에만 둔다.
- framework public surface 에 bindings/node concrete type, native detail, generated internal 경로를 노출하지 않는다.
- 필요한 binding 기능이 없으면 bindings/node public API 를 추가하고 framework 에서 우회하지 않는다.
- sample-only route store, sample-only metadata store, sleep 기반 readiness masking 을 넣지 않는다.
- readiness 문제는 실제 ready signal, monitor event, event file, runtime 상태 확인으로
  드러내고 고친다.
- compatibility shim 은 추가하지 않는다. public surface 는 dotnet 의미와 Node/NestJS 사용성을
  동시에 만족하는 최소 표면으로 유지한다.
- 외부 계약을 바꿔야 하면 spec 을 먼저 수정하고 구현과 테스트를 맞춘다.
- 구현 변경 후 docs, samples, tests 중 public 이름이나 사용법에 영향받는 곳을 함께 갱신한다.
- public API 를 새로 열 때는 dotnet 대응 개념이 있는지 확인한다. 내부 구현 편의를 위한
  helper 는 root export 에 노출하지 않는다.
- stream/session/actor relay 는 application route store 로 우회하지 않는다. native
  ActorGateway, session context, backend adapter 경계를 따라 구현한다.
- 문서 본문에는 저장소 문서 규칙에서 금지한 표현을 쓰지 않는다.

검증 루프:
1. 현재 Phase 의 입력 문서와 dotnet 대응 코드를 읽는다.
2. 구현한다.
3. Phase DoD 테스트를 추가하거나 갱신한다.
4. `npm run build`, `npm run typecheck`, 관련 `node --test ...` 를 실행한다.
5. 가능한 시점마다 `npm run verify:p0`, `npm run verify:runtime-matrix`,
   `npm run verify:cross-language`, `npm run verify:abi-matrix` 중 현재 단계에 맞는
   더 넓은 release gate 를 실행한다.
6. `git diff --check -- framework/languages/node` 를 실행한다.
7. backend dependency guard 를 실행해 framework runtime 이 binding internal 경로를 직접 참조하지 않는지 확인한다.
8. POSD red flag 를 리뷰한다.
9. red flag 가 있으면 리팩토링하고 3~8 을 반복한다.
10. dotnet 대응 테스트와 Node 테스트를 대조해 빠진 회귀 케이스를 추가한다. 특히 동시성,
    lifecycle 완료 시점, 실패 후 정리, backpressure, cross-language 경계는 테스트 없이
    완료로 보지 않는다.
11. 이슈가 0 이면 IMPLEMENTATION-PLAN.ko.md 의 진행 체크리스트를 갱신한다.
12. Phase 완료 커밋을 만든 뒤 원격에 푸시한다. 단, unrelated dirty changes 는
    스테이징하지 않는다.
13. 아직 P9 최종 gate 와 dotnet 대비 4축 감사가 끝나지 않았으면, 다음 Phase 로 이동해서
    같은 루프를 반복한다.

backend dependency guard:
- `rg -n "runtime/native|src/zlink/runtime|bindings/node|require\\(" framework/languages/node/packages/framework/src/runtime/streams framework/languages/node/packages/framework/src/runtime/backend/node framework/languages/node/packages/framework/src/runtime/actors framework/languages/node/packages/framework/src/index.ts`

Phase 별 핵심 완료 조건:
- P0: workspace, build, test runner, binding smoke 가 동작한다.
- P1: backend adapter factory 와 wrapper 가 모두 동작하고 binding concrete type 이 새지 않는다.
- P1.5: Node binding public API gap 이 닫히고 framework 우회 코드가 없다.
- P2: contracts 와 decorators 가 handler-interfaces spec 과 일치한다.
- P3: NestJS module, configuration, lifecycle, DI exposure 가 dotnet 의미와 일치한다.
- P4: channel request/reply, send, publish, filter, handler discovery/exposure 규칙이 E2E 로 통과한다.
- P5: spot lifecycle, manager, timer, serial executor, outbound 가 dotnet 과 일치한다.
- P6: actor lifecycle, mailbox ordering, location recheck, dispatch routing 이 dotnet 과 일치한다.
- P7: stream session, session-to-actor relay, bound session, connector, json/msgpack/protobuf codec 이 dotnet 과 일치한다.
- P8: registry, monitoring, runtime codec registry 가 dotnet 과 일치한다.
- P9: regression matrix, user guide, samples, cross-language smoke, doc link tests, ABI release gate 가 모두 통과한다.

최종 gate:
- `npm run verify:release`

`verify:release` 는 아래 gate 를 순서대로 묶어 실행해야 한다.

- `npm run build`
- `npm run typecheck`
- `npm run verify:abi-matrix`
- `npm run verify:p0`
- `npm run verify:samples`
- `npm run verify:cross-language`
- `npm run verify:runtime-matrix`
- `./samples/run_samples.sh`
- `git diff --check -- framework/languages/node`

cross-language smoke 필수 경로:
- Node client -> dotnet channel server request/reply
- Node client -> dotnet channel server one-way send
- Node publisher -> dotnet fanout subscriber publish
- dotnet client -> Node channel server request/reply
- Node stream connector -> dotnet stream server
- dotnet stream connector -> Node stream server

sample 필수 경로:
- TicTacToe.Ts
- Bingo.Ts

ABI release gate 필수 runtime:
- node20
- node22
- win-x64
- win-arm64
- linux-x64
- linux-arm64
- darwin-x64
- darwin-arm64

최종 판정:
- framework/languages/dotnet 과 비교해 구조, 기능, 사용성, 샘플 4축이 모두 동등해야 완료다.
- dotnet samples 와 같은 시나리오의 Node samples 를 실행 가능한 상태로 제공해야 한다.
- Node guide 는 dotnet guide 의 주요 장과 대응해야 한다.
- `npm run verify:cross-language` 로 Node 와 dotnet 사이의 channel/stream 경로를 확인해야 한다.
- connector 는 stream connector 의 tcp/ws/wss 경로가 실제로 동작해야 완료다.
- 모든 테스트와 문서 링크 회귀가 green 이 아니면 완료라고 말하지 않는다.
- 완료 선언 전에는 dotnet framework 의 src, tests, samples, guide 와 Node 쪽 구현, tests,
  samples, guide 를 표로 대조하고 빠진 항목이 0 인지 확인한다.
- 완료 보고에는 구현/문서 수정 요약, 통과한 gate 목록, dotnet 대비 구조·기능·사용성·샘플
  parity 확인 결과를 포함한다. 남은 이슈가 없으면 명시적으로 "남은 이슈 없음"이라고 적는다.

완료 직전 최종 감사:
- `IMPLEMENTATION-PLAN.ko.md` §7 의 모든 Phase 체크박스가 실제 코드와 테스트 결과로
  뒷받침되는지 확인한다.
- `internals/regression-test-matrix.ko.md` 의 각 행이 자동 테스트, 샘플 실행, 또는
  cross-language smoke 중 하나로 검증되는지 표로 대조한다.
- dotnet 테스트 이름과 Node 테스트 이름을 나란히 놓고, 의도는 같지만 Node 쪽에 없는
  케이스를 찾아 추가한다. 이름이 달라도 동작 증거가 있어야 한다.
- dotnet `src`, `tests`, `samples`, `doc/guide` 와 Node `packages`, `test`,
  `samples`, `doc/guide` 를 나란히 비교한다.
- backend concrete type, native detail, generated internal 경로가 framework public
  surface 로 새지 않는지 다시 검색한다.
- 문서 링크, 샘플 명령, package export, public decorator 이름이 실제 파일과 맞는지
  확인한다.
- 빠진 항목이 하나라도 있으면 완료 선언 대신 다음 수정 항목으로 되돌아간다.

커밋 규칙:
- 관련 Phase 단위로 작은 커밋을 만든다.
- unrelated dirty changes 는 건드리지 않는다.
- 커밋 전에는 관련 검증 결과와 남은 이슈를 확인한다.
- 푸시는 검증 가능한 커밋 단위로 수행한다.
- 여러 주제가 섞였으면 커밋을 나눈다. 예: runtime 구현, connector POSD split,
  tests/docs 보강.
- 이미 원격보다 앞선 커밋이 있으면 `git log --oneline origin/main..HEAD` 로 내용을
  확인한 뒤 함께 푸시한다. 확인하지 않은 unrelated working tree 변경은 새 커밋에
  섞지 않는다.
```

## 완료 전 자체 점검 질문

- 현재 Phase 의 입력 spec 과 dotnet 대응 코드를 실제로 비교했는가.
- 구현이 adapter 경계를 지켰는가.
- 호출자가 알아야 하는 정보가 줄었는가.
- 같은 지식이 여러 모듈에 중복되지 않는가.
- sample 이 실제 사용자 흐름을 보여 주는가.
- 완료라고 말하기 전에 P9 와 4축 동등성까지 확인했는가.
