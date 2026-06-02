# ZLink Framework for Node.js — 끝까지 구현하는 실행 프롬프트

이 문서는 `IMPLEMENTATION-PLAN.ko.md` 를 기준으로 Node.js/NestJS 버전의
ZLink Framework 를 dotnet framework 와 같은 구조, 기능, 사용성, 샘플 수준까지
완료하기 위해 새 작업 세션에 그대로 전달할 수 있는 프롬프트다.

## 실행 프롬프트

```text
너는 /home/hep7/project/kairos/zlink 저장소에서 Node.js/NestJS 버전의
ZLink Framework 를 구현하는 코딩 에이전트다.

목표:
- framework/languages/node/doc/IMPLEMENTATION-PLAN.ko.md 를 제어 문서로 사용한다.
- P0~P9 전체 Phase 와 각 Phase 의 POSD 게이트를 모두 통과시킨다.
- 최종 완료 기준은 dotnet framework 와 구조, 기능, 사용성, 샘플 4축이 동등한 것이다.
- 기준 구현은 framework/languages/dotnet/src 이다. 문서와 코드가 다르면 dotnet 코드를 따른다.

반드시 먼저 읽을 문서:
1. framework/languages/node/doc/IMPLEMENTATION-PLAN.ko.md
2. framework/languages/node/doc/internals/dotnet-to-node-surface-mapping.ko.md
3. framework/languages/node/doc/internals/backend-dependency-policy.ko.md
4. framework/languages/node/doc/internals/lifecycle-and-failure-semantics.ko.md
5. 현재 Phase 가 지정한 spec 문서
6. 모호한 부분의 dotnet 대응 코드

작업 규칙:
- Phase 순서를 지킨다. P0 → P1 → P1.5 → P2 → P3 → P4 → P5 → P6 → P7 → P8 → P9.
- 각 Phase 는 구현, DoD 검증, POSD 리팩토링 게이트, 재검증을 모두 통과해야 완료다.
- 게이트에서 이슈가 남으면 다음 Phase 로 넘어가지 말고 같은 Phase 안에서 수정한다.
- backend 의존은 adapter 한 층에만 둔다.
- framework public surface 에 bindings/node concrete type, native detail, generated internal 경로를 노출하지 않는다.
- 필요한 binding 기능이 없으면 bindings/node public API 를 추가하고 framework 에서 우회하지 않는다.
- sample-only route store, 임시 metadata store, sleep 기반 readiness masking 을 넣지 않는다.
- 외부 계약을 바꿔야 하면 spec 을 먼저 수정하고 구현과 테스트를 맞춘다.
- 구현 변경 후 docs, samples, tests 중 public 이름이나 사용법에 영향받는 곳을 함께 갱신한다.

검증 루프:
1. 현재 Phase 의 입력 문서와 dotnet 대응 코드를 읽는다.
2. 구현한다.
3. Phase DoD 테스트를 추가하거나 갱신한다.
4. `npm run build`, `npm run typecheck`, 관련 `node --test ...` 를 실행한다.
5. 가능한 시점마다 `npm run verify:p0` 또는 더 넓은 release gate 를 실행한다.
6. `git diff --check -- framework/languages/node` 를 실행한다.
7. backend dependency guard 를 실행해 framework runtime 이 binding internal 경로를 직접 참조하지 않는지 확인한다.
8. POSD red flag 를 리뷰한다.
9. red flag 가 있으면 리팩토링하고 3~8 을 반복한다.
10. 이슈가 0 이면 IMPLEMENTATION-PLAN.ko.md 의 진행 체크리스트를 갱신한다.

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
- P9: regression matrix, user guide, samples, cross-language smoke, doc link tests 가 모두 통과한다.

최종 판정:
- framework/languages/dotnet 과 비교해 구조, 기능, 사용성, 샘플 4축이 모두 동등해야 완료다.
- dotnet samples 와 같은 시나리오의 Node samples 를 실행 가능한 상태로 제공해야 한다.
- Node guide 는 dotnet guide 의 주요 장과 대응해야 한다.
- cross-language smoke 로 Node 와 dotnet 사이의 channel/stream/session actor 경로를 확인해야 한다.
- 모든 테스트와 문서 링크 회귀가 green 이 아니면 완료라고 말하지 않는다.

커밋 규칙:
- 관련 Phase 단위로 작은 커밋을 만든다.
- unrelated dirty changes 는 건드리지 않는다.
- 커밋 전에는 관련 검증 결과와 남은 이슈를 확인한다.
- 푸시는 검증 가능한 커밋 단위로 수행한다.
```

## 완료 전 자체 점검 질문

- 현재 Phase 의 입력 spec 과 dotnet 대응 코드를 실제로 비교했는가.
- 구현이 adapter 경계를 지켰는가.
- 호출자가 알아야 하는 정보가 줄었는가.
- 같은 지식이 여러 모듈에 중복되지 않는가.
- sample 이 실제 사용자 흐름을 보여 주는가.
- 완료라고 말하기 전에 P9 와 4축 동등성까지 확인했는가.
