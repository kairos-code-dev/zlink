# .NET Framework Sample 공통 문서 정합성 작업자 프롬프트

## 사용 방법

이 문서는 별도 Codex 작업자에게 그대로 전달할 실행 프롬프트다. 작업자는 이 문서를 읽은 뒤
`framework/doc/plan/framework-dotnet-sample-common-conformance-plan.ko.md`를 기준으로 첫 번째
`pending` 샘플 하나만 처리한다.

한 샘플을 끝내기 전에는 다음 샘플로 넘어가지 않는다. "끝냈다"는 뜻은 구현, build, runner proof,
Codex 문서 반영 리뷰, POSD/DDD 리뷰가 모두 끝났고 두 리뷰가 정확히 `이슈 없음`이라고 나온 상태다.

## 역할

너는 `/home/hep7/project/kairos/zlink` 저장소에서 `.NET` framework sample을 공통 샘플 문서에 맞추는
작업자다. 기준 계획은 `framework/doc/plan/framework-dotnet-sample-common-conformance-plan.ko.md`다.

## 목표

`framework/languages/dotnet/samples` 아래 정본 6종 샘플이 `framework/doc/framework/common/sample`
아래 공통 샘플 문서의 역할, 메시지 흐름, 연결 방식, runner 계약, client self-check 완료 기준을 따르도록
구현, runner, sample-local README, 회귀테스트를 정렬한다.

공통 문서가 기준이다. `.NET` 구현과 공통 문서가 충돌하면 공통 문서를 바꾸지 않고 `.NET` 구현 쪽을
수정한다.

## 기준 문서

먼저 아래 문서를 읽고 현재 상태를 확인한다.

- `framework/doc/plan/framework-dotnet-sample-common-conformance-plan.ko.md`
- `framework/doc/framework/common/sample/README.ko.md`
- `framework/doc/framework/common/sample/bingo/README.ko.md`
- `framework/doc/framework/common/sample/tictactoe/README.ko.md`
- `framework/doc/framework/common/sample/supportchat/README.ko.md`
- `framework/doc/framework/common/sample/deliverydispatch/README.ko.md`
- `framework/doc/framework/common/sample/event/shoppingmall.ko.md`
- `framework/doc/framework/common/sample/event/gamequest.ko.md`
- `doc/principal/software-design-principles.md`
- `doc/principal/source-comment-principles.ko.md`
- 루트 `AGENTS.md`

## 작업 범위

계획 문서의 진행 순서 표에서 첫 `pending` 샘플 하나만 선택한다. 현재 계획이 그대로라면 다음 대상은
`DeliveryDispatch`다.

대상 샘플은 아래 여섯 개다.

1. `Bingo`
2. `TicTacToe`
3. `SupportChat`
4. `DeliveryDispatch`
5. `ShoppingMall`
6. `GameQuest`

이미 `completed`로 닫힌 샘플은 다시 고치지 않는다. 단, 현재 샘플 검증에 필요한 공통 회귀테스트나
공유 framework 버그 수정은 좁은 범위에서 처리할 수 있다.

한 번의 작업 보고는 선택한 샘플 하나에 대해서만 작성한다. 이후 샘플은 이전 샘플의 두 리뷰가 모두
`이슈 없음`이고 계획 문서가 갱신된 뒤에만 새 작업으로 시작한다.

## 반드시 지킬 규칙

- 한 번에 한 샘플만 진행한다.
- 한 샘플의 구현, build, runner proof, Codex 문서 반영 리뷰, POSD/DDD 리뷰가 모두 끝나기 전에는 다음
  샘플로 넘어가지 않는다.
- Codex 문서 반영 리뷰와 POSD/DDD 리뷰가 모두 정확히 `이슈 없음`일 때만 해당 샘플을 완료로 기록한다.
- dirty worktree가 있을 수 있다. 기존 변경을 되돌리지 말고, 현재 샘플 범위만 좁게 수정한다.
- 작업을 시작하기 전에 현재 변경과 사용자 변경을 구분한다. 요청 범위와 무관한 기존 변경은 되돌리지 않는다.
- private/internal API, reflection, raw frame, test-only adapter, sleep/retry 증가로 gap을 숨기지 않는다.
- 샘플은 사용자가 따라 할 public API 예시다. runner 통과를 위해 업무 코드에 codec, route, endpoint,
  storage, retry 같은 내부 결정을 노출하지 않는다.
- framework 기능 누락이나 framework 버그가 드러나면 샘플 우회로 통과시키지 말고 원인을 좁혀 framework
  또는 runtime 책임 위치에서 고친다.
- framework 버그 수정에는 가능한 한 focused regression test를 추가한다.
- `TicTacToe`를 제외한 모든 샘플은 store 기능 때문에 Redis location store를 사용해야 한다.
- `TicTacToe`는 Redis room route store를 보여 주는 예외 샘플이다. Redis location store 자동 연결 샘플처럼
  바꾸지 않는다.
- 공통 샘플 문서는 수정하지 않는다.
- 공통 문서와 구현이 충돌하면 공통 문서를 바꾸지 않고 sample 구현, runner, sample-local README,
  regression test만 고친다.

## 샘플 단위 절차

각 샘플마다 아래 순서를 그대로 따른다.

1. `git status --short`로 기존 변경을 확인하고 현재 샘플의 수정 범위를 명시한다.
2. 계획 문서에서 첫 `pending` 샘플 하나를 고른다.
3. 대상 공통 문서, sample-local README, runner, client scenario, shared contracts, server role을 읽는다.
4. 계획 문서의 "모든 샘플 공통 체크리스트"와 해당 샘플 체크리스트를 현재 작업 체크리스트로 사용한다.
5. 공통 문서 요구를 구현에 반영한다.
6. sample-local README가 공통 문서 또는 구현 사실과 충돌하면 sample-local README만 갱신한다.
7. focused build를 실행한다.
8. sample regression test가 있거나 새로 추가할 가치가 있으면 focused test를 실행한다.
9. `run_sample.sh`와 `run_sample.ps1`을 실행하고 client/server evidence marker를 확인한다.
10. Redis 실행 mode를 가능한 범위에서 모두 검증한다.
    - `TicTacToe`를 제외한 샘플은 Redis location store endpoint와 key prefix가 서버 역할에 전달되어야 한다.
    - `TicTacToe`는 Redis room route store endpoint와 key prefix가 전달되어야 한다.
    - 외부 Redis endpoint를 사용하면 runner가 외부 Redis를 cleanup하지 않는다는 증거를 남긴다.
    - 외부 Redis endpoint가 없으면 runner가 전용 Docker Redis를 준비하고, 자신이 만든 container만 cleanup해야 한다.
11. runner proof는 build-only가 아니어야 한다. client 완료 marker, server evidence marker, message flow,
    dispatch 오류 부재 또는 오류 검증 marker처럼 공통 문서의 완료 조건을 보여 주는 로그를 확인한다.
12. 실패하면 같은 샘플 안에서 원인을 수정하고 다시 검증한다.
13. 구현과 runner proof가 끝나면 Codex 문서 반영 리뷰를 READ-ONLY로 요청한다.
14. 문서 반영 리뷰에서 이슈가 있으면 같은 샘플 안에서 수정, build, runner proof, 재리뷰를 반복한다.
15. 문서 반영 리뷰가 정확히 `이슈 없음`이면 POSD/DDD 리뷰를 READ-ONLY로 요청한다.
16. POSD/DDD 리뷰에서 가치 있는 리팩토링 이슈가 나오면 같은 샘플 안에서 수정하고 build, runner proof,
    두 리뷰를 다시 반복한다.
17. 두 리뷰가 모두 정확히 `이슈 없음`이면 계획 문서의 체크박스, 진행 순서 상태, 검증 기록을 갱신하고
    다음 샘플로 넘어간다.

## Codex 문서 반영 리뷰 요청문

각 샘플 구현과 runner proof가 끝나면 별도 Codex 에이전트에 아래 요청을 그대로 전달한다.

```text
READ-ONLY로 리뷰해줘.

대상:
- framework/languages/dotnet/samples/<Sample>

기준:
- framework/doc/framework/common/sample/README.ko.md
- framework/doc/framework/common/sample/<해당 샘플 문서>
- event 샘플이면 framework/doc/framework/common/sample/event/<해당 샘플 문서>

확인할 것:
1. 공통 문서의 모든 역할, 메시지, 연결 방식, runner 계약, client self-check 완료 기준이 .NET 구현에 반영되었는가.
2. 공통 문서와 충돌하는 sample-local README 또는 runner 설명이 남아 있지 않은가.
3. 공통 문서에 없는 public API나 우회 helper를 새로 만들지 않았는가.
4. raw frame, private/internal API, reflection, test-only adapter로 gap을 숨기지 않았는가.
5. runner proof가 build-only가 아니라 실제 client/server evidence marker를 확인하는가.
6. `TicTacToe`를 제외한 샘플은 Redis location store endpoint와 key prefix를 서버 역할에 전달하는가.
7. `TicTacToe`는 Redis room route store endpoint와 key prefix를 전달하고, location store 자동 연결 샘플처럼 다루지 않았는가.
8. 외부 Redis endpoint 재사용과 Docker fallback처럼 필요한 실행 mode가 빠지지 않았는가.

출력:
- findings를 심각도 순으로 먼저 적어줘.
- 각 finding은 파일:라인 근거를 붙여줘.
- 이슈가 없으면 정확히 `이슈 없음`이라고 적어줘.
```

## POSD/DDD 리뷰 요청문

Codex 문서 반영 리뷰가 `이슈 없음`이면 별도 Codex 에이전트에 아래 요청을 그대로 전달한다.

```text
READ-ONLY로 리뷰해줘.

대상:
- framework/languages/dotnet/samples/<Sample>

관점:
- POSD: 깊은 모듈, 정보 은닉, 복잡성을 아래로 내리기, 오류를 정의로 없애기, 얕은 모듈과 패스스루 제거
- DDD/Hexagonal: Domain/Application/Infrastructure 의존 방향, 도메인 순수성, adapter 책임 경계, use case 응집도
- 샘플성: 사용자가 따라 할 public API 예시로 자연스러운가

확인할 것:
1. 공통 문서 반영을 위해 추가한 코드가 호출자에게 codec, route, endpoint, storage, retry 같은 내부 결정을 노출하지 않는가.
2. Domain 코드가 framework 타입, stream/session/socket, Redis/파일 저장소, logger, DI container에 의존하지 않는가.
3. Application/use case가 도메인 동작을 설명하는 깊은 진입점인지, 단순 패스스루나 시간 순서별 얕은 분해가 아닌가.
4. Infrastructure adapter가 framework와 외부 저장소 책임을 흡수하고, handler/client scenario가 반복 helper 주입으로 복잡해지지 않았는가.
5. 리팩토링하면 실제 복잡성이 줄어드는 항목이 남아 있는가. 단순 취향 변경이나 대규모 미관 refactor는 이슈로 잡지 않는다.

출력:
- 가치가 있는 리팩토링 finding만 심각도 순으로 적어줘.
- 각 finding은 파일:라인 근거와 어떤 POSD/DDD 원칙을 위반하는지 적어줘.
- 이슈가 없으면 정확히 `이슈 없음`이라고 적어줘.
```

## 계획 문서 갱신 규칙

샘플 하나가 완료될 때만 `framework/doc/plan/framework-dotnet-sample-common-conformance-plan.ko.md`를 갱신한다.

- 진행 순서 표의 해당 샘플 상태를 `completed`로 바꾼다.
- 해당 샘플 체크리스트에서 실제 확인한 항목만 체크한다.
- 검증 기록 표에 build, test, runner 명령과 핵심 evidence marker를 적는다.
- 문서 반영 리뷰와 POSD/DDD 리뷰는 실제 리뷰 결과가 정확히 `이슈 없음`일 때만 그렇게 기록한다.
- 실패나 미검증 항목은 완료로 적지 않는다.

## 최종 보고 형식

샘플 하나를 닫을 때마다 아래 항목을 간결하게 보고한다.

- 완료한 샘플
- 수정 파일 목록
- focused test 명령과 결과
- build 명령과 결과
- runner 명령과 evidence marker
- Redis 실행 mode 검증 결과
- Codex 문서 반영 리뷰 결과
- POSD/DDD 리뷰 결과
- 계획 문서 갱신 내용
- 남은 pending 샘플
- `git diff --check` 결과
- 이번 작업 범위와 무관한 기존 변경을 되돌리지 않았다는 확인

## 바로 사용할 실행 프롬프트

아래 블록을 새 Codex 작업자에게 그대로 전달한다.

```text
작업 디렉토리: /home/hep7/project/kairos/zlink

framework/doc/plan/framework-dotnet-sample-common-conformance-worker-prompt.ko.md 를 먼저 읽고,
framework/doc/plan/framework-dotnet-sample-common-conformance-plan.ko.md 의 진행 순서에서 첫 번째
pending 샘플 하나만 처리해줘.

공통 문서가 기준이다. framework/doc/framework/common/sample 아래 공통 문서를 구현에 맞춰 바꾸지 말고,
.NET sample 구현, runner, sample-local README, 필요한 regression test를 공통 문서에 맞춰라.

절대 지킬 것:
- 한 번에 한 샘플만 진행한다.
- 선택한 샘플의 구현, build, runner proof, Codex 문서 반영 리뷰, POSD/DDD 리뷰가 모두 끝나기 전에는 다음 샘플로 넘어가지 않는다.
- 두 리뷰가 모두 정확히 `이슈 없음`일 때만 plan 문서에서 해당 샘플을 completed로 갱신한다.
- dirty worktree가 있을 수 있으니 기존 변경을 되돌리지 말고 현재 샘플 범위만 좁게 수정한다.
- private/internal API, reflection, raw frame, test-only adapter, sleep/retry 증가로 gap을 숨기지 않는다.
- TicTacToe를 제외한 모든 샘플은 store 기능 때문에 Redis location store endpoint와 key prefix를 서버 역할에 전달해야 한다.
- TicTacToe는 Redis room route store를 사용하는 예외 샘플이며 Redis location store 자동 연결 샘플처럼 바꾸지 않는다.
- framework 버그가 드러나면 샘플 우회로 통과시키지 말고 책임 위치에서 고치고 가능한 focused regression test를 추가한다.

진행 순서:
1. git status --short 로 기존 변경을 확인하고 수정 범위를 선언한다.
2. plan 문서, 대상 공통 문서, sample-local README, runner, client scenario, shared contracts, server role을 읽는다.
3. plan 문서의 공통 체크리스트와 대상 샘플 체크리스트를 기준으로 gap을 닫는다.
4. focused build, 필요한 focused test, run_sample.sh, run_sample.ps1을 실행한다.
5. Redis external endpoint mode와 Docker fallback mode를 가능한 범위에서 검증하고 evidence marker를 남긴다.
6. 실패하면 같은 샘플 안에서 원인을 수정하고 다시 검증한다.
7. plan 문서의 Codex 문서 반영 리뷰 게이트를 READ-ONLY로 수행하고, 이슈가 있으면 수정과 검증을 반복한다.
8. 문서 반영 리뷰가 `이슈 없음`이면 POSD/DDD 리뷰 게이트를 READ-ONLY로 수행하고, 이슈가 있으면 수정과 검증을 반복한다.
9. 두 리뷰가 모두 `이슈 없음`이면 plan 문서의 상태, 체크박스, 검증 기록을 실제 증거만으로 갱신한다.

최종 보고에는 완료한 샘플, 수정 파일, focused test/build/runner 명령과 결과, Redis mode 검증,
두 리뷰 결과, plan 문서 갱신 내용, 남은 pending 샘플, git diff --check 결과,
무관한 기존 변경을 되돌리지 않았다는 확인을 포함해줘.
```
