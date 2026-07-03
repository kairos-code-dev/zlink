# .NET Framework Sample 공통 문서 재정렬 계획

## 목적

이 계획은 `framework/languages/dotnet/samples`의 샘플 구현을
`framework/doc/framework/common/sample` 아래 공통 샘플 문서에 다시 맞추기 위한 절차를 정의한다.

이번 작업에서는 공통 샘플 문서를 기준으로 삼는다. `.NET` 구현과 공통 문서가 충돌하면 공통 문서를
바꾸지 않고 `.NET` 구현, runner, client self-check, sample-local README를 수정한다.

## 기준

1. 공통 샘플 문서:
   - `framework/doc/framework/common/sample/README.ko.md`
   - `framework/doc/framework/common/sample/bingo/README.ko.md`
   - `framework/doc/framework/common/sample/tictactoe/README.ko.md`
   - `framework/doc/framework/common/sample/supportchat/README.ko.md`
   - `framework/doc/framework/common/sample/deliverydispatch/README.ko.md`
   - `framework/doc/framework/common/sample/event/shoppingmall.ko.md`
   - `framework/doc/framework/common/sample/event/gamequest.ko.md`
2. 대상 구현:
   - `framework/languages/dotnet/samples/<Sample>/`
3. 설계 판단 기준:
   - `doc/principal/software-design-principles.md`
   - `doc/principal/source-comment-principles.ko.md`
   - 루트 `AGENTS.md`의 기존 설계 우선 규칙, Codec 책임 경계, POSD 원칙

## 완료 원칙

1. 한 번에 한 샘플만 진행한다.
2. 한 샘플의 구현, runner proof, 문서 반영 확인, Codex 리뷰, POSD/DDD 리뷰가 모두 끝나기 전에는
   다음 샘플로 넘어가지 않는다.
3. 공통 문서의 메시지 이름, 역할 분리, 연결 방식, runner 계약, client self-check 완료 기준을
   구현에 반영한다.
4. 구현 gap을 private API, raw frame, internal helper, test-only adapter, sleep/retry 증가로 메우지 않는다.
5. 버그가 드러나면 실패 로그와 재현 조건을 먼저 좁히고, 원인을 수정한 뒤 runner proof나 회귀테스트를
   추가한다.
6. 서버 구동에는 build와 framework 초기화에 필요한 충분한 시간을 준다. 하지만 통과를 위해 납득하기
   어려운 긴 대기가 필요하면 readiness, role wiring, port allocation, dependency 초기화 버그로 보고
   원인을 수정한다.
7. Codex 리뷰 결과가 `이슈 없음`이고 POSD/DDD 리뷰 결과도 `이슈 없음`일 때만 해당 샘플을 완료로 적는다.
8. `TicTacToe`를 제외한 샘플은 framework location store와 sample store 기능을 위해 Redis를 사용한다.
   각 샘플 runner는 실행할 때마다 자기 실행에만 쓰는 전용 Docker Redis container를 직접 띄우고,
   그 endpoint와 key prefix를 서버 역할에 전달한 뒤 실행이 끝나면 그 container를 정리한다. 동시에 도는
   다른 테스트나 다른 샘플과 간섭하지 않도록 container 이름, host port, Redis key prefix는 실행별로
   고유해야 한다. host port는 고정값을 쓰지 말고 Docker가 빈 port를 배정하게 하거나 동등하게 충돌을
   피할 수 있는 방식으로 정한다. container 이름과 key prefix에는 sample 이름과 실행 id를 넣어야 하며,
   cleanup은 그 실행 id로 만든 container만 대상으로 삼는다. 샘플 runner는 외부 Redis endpoint 재사용
   mode를 제공하지 않는다. `TicTacToe`는 공유 location store 기반 자동 연결 샘플이 아니라 수동 endpoint와
   Redis room route store를 보여 주는 예외 샘플이다.

## 진행 순서

| 순서 | 샘플 | 현재 gap | 완료 조건 | 상태 |
|------|------|----------|-----------|------|
| 1 | `Bingo` | Redis 사용 계약을 명확히 해야 함 | shell/PowerShell runner가 실행별 전용 Docker Redis를 띄우고, 고유 endpoint/key prefix를 `BINGO_REDIS_ENDPOINT`와 `BINGO_REDIS_KEY_PREFIX`로 전달하며, 실행 종료 시 자신이 만든 container만 정리한다 | completed |
| 2 | `TicTacToe` | Redis room route key prefix가 없고 runner Redis 계약이 불명확함 | shell/PowerShell runner가 room route store용 실행별 전용 Docker Redis를 띄우고, 고유 endpoint/key prefix를 `TICTACTOE_REDIS_ENDPOINT`와 `TICTACTOE_REDIS_KEY_PREFIX`로 전달하며, 실행 종료 시 자신이 만든 container만 정리한다 | completed |
| 3 | `SupportChat` | Redis runner 계약, reconnect self-check, server evidence, capacity accounting 보강 필요 | Redis location store 계약, 공통 문서 전체 반영 리뷰, POSD/DDD 리뷰가 모두 `이슈 없음`이다. 이슈가 나오면 같은 샘플에서 수정·검증한다 | completed |
| 4 | `DeliveryDispatch` | 초기 구현 gap은 확인되지 않았지만 정본 6종 gate에 포함해야 함 | Redis location store 계약, 공통 문서 전체 반영 리뷰, POSD/DDD 리뷰가 모두 `이슈 없음`이다. 이슈가 나오면 같은 샘플에서 수정·검증한다 | completed |
| 5 | `ShoppingMall` | 동시 idempotency 경쟁과 죽은 뒤 재개 self-check가 공통 문서 수준까지 닫히지 않음 | Redis location store 계약, 두 API 동시 시작 경쟁, `InventoryReserved` 이후 `ContinueOrderWorkflowReq` 복구, server-side assertion이 runner에서 확인된다 | completed |
| 6 | `GameQuest` | client action tier가 공통 문서의 stream/session 중심 흐름과 다름 | Redis location store 계약, client self-check가 `JoinSessionReq` 이후 같은 stream으로 gameplay action과 push를 다루고, HTTP action은 self-check 주 경로에서 빠진다 | completed |

`SupportChat`과 `DeliveryDispatch`는 현재 알려진 구현 변경 항목이 없더라도 review-only 단계로 반드시
닫는다. 리뷰에서 공통 문서 미반영이나 POSD/DDD상 가치 있는 리팩토링 항목이 나오면 review-only가 아니라
수정 단계로 전환하고, build와 runner proof까지 포함해 같은 gate를 반복한다.

## 샘플 단위 절차

각 샘플은 아래 순서를 그대로 따른다.

1. `git status --short`로 기존 변경을 확인하고, 이번 샘플의 수정 범위를 명시한다.
2. 대상 공통 문서와 `.NET` sample-local README, runner, client scenario, shared contracts, server role을 읽는다.
3. 공통 문서 요구를 체크리스트로 만든다.
4. 구현을 수정한다. 공통 문서를 바꾸지 않는다.
5. sample-local README가 구현 사실과 충돌하면 sample-local README만 갱신한다.
6. focused build를 실행한다.
7. 개별 `run_sample.sh`와 `run_sample.ps1`을 실행한다. Redis는 store 기능의 실행 전제이므로 runner가
   실행별 전용 Docker Redis를 직접 띄우고, 고유 endpoint와 key prefix를 서버 역할에 전달하며, 실행 종료 시
   자신이 만든 container를 정리하는지 검증한다. 동시에 실행되는 다른 테스트와 간섭하지 않도록 Redis
   container 이름, host port, key prefix는 실행별로 달라야 한다. host port 고정값, 샘플 이름만 들어간
   container 이름, 여러 실행이 공유하는 key prefix, sample 이름으로 전체 container를 지우는 broad cleanup은
   모두 gap으로 처리한다.
8. 실패하면 같은 샘플 안에서 원인을 수정하고 다시 검증한다.
9. 아래 Codex 문서 반영 리뷰를 요청한다.
10. 리뷰에서 이슈가 있으면 수정, build, runner proof를 다시 수행하고 같은 리뷰를 반복한다.
11. Codex 문서 반영 리뷰가 `이슈 없음`이면 아래 POSD/DDD 리뷰를 요청한다.
12. POSD/DDD 리뷰에서 이슈가 있으면 설계상 가치가 있는 범위만 같은 샘플 안에서 리팩토링하고 다시 검증한다.
13. 두 리뷰가 모두 `이슈 없음`이면 이 문서의 상태와 검증 로그를 갱신하고 다음 샘플로 이동한다.

## 모든 샘플 공통 체크리스트

아래 항목은 여섯 샘플 모두에 적용한다. 구현 변경이 없는 review-only 샘플도 이 목록을 기준으로 확인한다.

- [x] 공통 샘플 문서의 서버 역할, 메시지 흐름, 메시지 필드, 검증 순서가 sample-local README와 코드에 맞다.
- [x] sample-local README가 공통 문서와 충돌하지 않는다.
- [x] public framework API로 구현하고, private/internal API, reflection, raw frame, test-only adapter를 쓰지 않는다.
- [x] 샘플 코드가 codec, route, endpoint, storage, retry 같은 내부 결정을 caller-facing 업무 코드로 밀어내지 않는다.
- [x] `TicTacToe`를 제외한 샘플은 Redis location store endpoint와 key prefix를 서버 역할에 전달한다.
- [x] `TicTacToe`는 location store 자동 연결 대신 Redis room route store endpoint와 key prefix를 전달한다.
- [x] runner는 실행별 전용 Docker Redis container를 직접 띄운다.
- [x] runner는 직접 띄운 Redis endpoint와 실행별 key prefix를 서버 역할에 전달한다.
- [x] runner의 Redis container 이름, host port, key prefix는 동시에 도는 다른 테스트와 충돌하지 않도록 실행별로 고유하다.
- [x] runner는 Redis host port 고정값을 쓰지 않고, 실행마다 충돌 없는 port를 배정한다.
- [x] runner는 sample 이름과 실행 id를 포함한 container 이름/key prefix를 사용한다.
- [x] runner는 자신이 만든 Redis container를 실행 종료 시 cleanup한다.
- [x] runner는 sample 이름만으로 여러 실행의 Redis container를 지우는 broad cleanup을 하지 않는다.
- [x] runner는 외부 Redis endpoint 재사용 mode를 제공하지 않는다.
- [x] framework message dispatch 오류가 샘플 로그에서 확인 가능하다.
- [x] dispatch 오류 로그는 최소한 surface, messageKind, reason, action, packetName, correlationId에 대응하는 정보를 남긴다.
- [x] handler가 framework dispatch 오류를 잡아서 임의의 정상 업무 응답으로 바꾸지 않는다.
- [x] `run_sample.sh`와 `run_sample.ps1`은 build, readiness, client self-check, server evidence, cleanup을 책임진다.
- [x] runner proof는 build-only가 아니라 client/server evidence marker를 포함한다.
- [x] 로컬 실행이 긴 sleep이나 과도한 retry에 기대지 않는다. 오래 기다려야 통과하면 readiness나 role wiring 버그로 다룬다.
- [x] Domain/Application/Infrastructure 의존 방향이 지켜진다.

## 샘플별 구현 체크리스트

### 1. Bingo

- [x] `run_sample.sh`는 실행별 전용 Docker Redis를 띄운다.
- [x] `run_sample.ps1`도 실행별 전용 Docker Redis를 띄운다.
- [x] runner가 만든 Redis container를 cleanup한다.
- [x] 외부 Redis endpoint 재사용 mode를 제공하지 않는다.
- [x] `BINGO_REDIS_ENDPOINT`는 Redis location store와 Redis match queue에 모두 전달된다.
- [x] `BINGO_REDIS_KEY_PREFIX`가 실행별로 설정되고 Play match queue와 location store에 전달된다.
- [x] build proof를 남긴다.
- [x] shell/PowerShell runner의 전용 Docker Redis proof를 남긴다.
- [x] Codex 문서 반영 리뷰 결과가 `이슈 없음`이다.
- [x] POSD/DDD 리뷰 결과가 `이슈 없음`이다.

### 2. TicTacToe

- [x] `run_sample.sh`는 room route store용 실행별 전용 Docker Redis를 띄운다.
- [x] `run_sample.ps1`도 room route store용 실행별 전용 Docker Redis를 띄운다.
- [x] runner가 만든 Redis container를 cleanup한다.
- [x] 외부 Redis endpoint 재사용 mode를 제공하지 않는다.
- [x] runner는 외부 base port, endpoint, log directory override를 읽지 않고 실행별 동적 port와 실행별 임시 log directory를 사용한다.
- [x] `TICTACTOE_REDIS_KEY_PREFIX`가 실행별로 설정된다.
- [x] `SampleSettings`가 Redis key prefix를 읽는다.
- [x] `RedisRoomRouteStore`가 room route key에 prefix를 적용한다.
- [x] actor message part가 callback turn 사이에 나뉘어 도착할 때 .NET binding이 partial actor message를 보류하고 다음 dispatch에서 이어 붙인다.
- [x] build proof를 남긴다.
- [x] shell/PowerShell runner의 전용 Docker Redis proof를 남긴다.
- [x] Codex 문서 반영 리뷰 결과가 `이슈 없음`이다.
- [x] POSD/DDD 리뷰 결과가 `이슈 없음`이다.

### 3. SupportChat

- [x] `SUPPORTCHAT_REDIS_ENDPOINT`와 `SUPPORTCHAT_REDIS_KEY_PREFIX`가 모든 서버 역할의 Redis location store에 전달된다.
- [x] 공통 문서의 conversation Spot, reconnect, idle timer, close, bound push 흐름을 다시 대조한다.
- [x] `WaitingForAgent`, `Active`, `WaitingForClose`, `Closed` 상태 전이가 client self-check와 server evidence에 반영되어 있는지 확인한다.
- [x] agent/customer 양쪽 idle notify가 `WaitingForClose` 상태를 검증하는지 확인한다.
- [x] 명시적 close와 closed conversation 재요청 오류가 검증되는지 확인한다.
- [x] build proof를 남긴다.
- [x] `run_sample.sh` proof를 남긴다.
- [x] `run_sample.ps1` proof를 남긴다.
- [x] shell/PowerShell runner의 전용 Docker Redis proof를 남긴다.
- [x] runner는 외부 base port, endpoint, log directory override를 읽지 않고 실행별 동적 port와 실행별 임시 log directory를 사용한다.
- [x] runner는 Redis host port 고정값을 쓰지 않고 Docker가 실행별 host port를 배정하게 한다.
- [x] runner는 sample 이름과 실행 id를 포함한 Redis container 이름과 Redis key prefix를 사용한다.
- [x] runner와 topology에서 실제 host wiring이 쓰지 않는 endpoint 환경 변수를 제거했다.
- [x] Codex 문서 반영 리뷰 결과가 `이슈 없음`이다.
- [x] POSD/DDD 리뷰 결과가 `이슈 없음`이다.

### 4. DeliveryDispatch

- [x] `DELIVERYDISPATCH_REDIS_ENDPOINT`와 `DELIVERYDISPATCH_REDIS_KEY_PREFIX`가 모든 서버 역할의 Redis location store에 전달된다.
- [x] 공통 문서의 Dispatch, CourierSession, Courier spot server node 2개, Tracking, CustomerGateway 역할을 다시 대조한다.
- [x] delivery reassignment, courier timeout, customer stream status push, tracking evidence가 client/server self-check에 반영되어 있는지 확인한다.
- [x] courier actor bind와 customer actor/session bind가 public actor/session/Spot API만 사용하는지 확인한다.
- [x] build proof를 남긴다.
- [x] `run_sample.sh` proof를 남긴다.
- [x] Codex 문서 반영 리뷰 결과가 `이슈 없음`이다.
- [x] POSD/DDD 리뷰 결과가 `이슈 없음`이다.

### 5. ShoppingMall

- [x] `SHOPPINGMALL_REDIS_ENDPOINT`와 `SHOPPINGMALL_REDIS_KEY_PREFIX`가 `CommerceApi`와 `OrderWorkflow`의 Redis location store에 전달된다.
- [x] sample store도 실행별 Redis endpoint와 key prefix를 사용하며, 파일 store fallback을 남기지 않는다.
- [x] runner는 외부 Redis endpoint, base port, log directory override를 읽지 않고 실행별 전용 Docker Redis와 동적 host port를 사용한다.
- [x] runner는 sample 이름과 실행 id를 포함한 Redis container 이름과 Redis key prefix를 사용하고, 자신이 만든 container만 cleanup한다.
- [x] client self-check가 같은 `IdempotencyKey`를 두 `CommerceApi`에 동시에 전송한다.
- [x] 두 응답이 같은 `OrderId`를 반환하는지 검증한다.
- [x] server-side assertion이 `OrderStartedEvent` 중복 기록이 없음을 확인한다.
- [x] test hook으로 `InventoryReserved`까지 진행된 주문을 만든다.
- [x] client self-check가 `ContinueOrderWorkflowReq` 경로로 주문을 `Confirmed`까지 재개한다.
- [x] 재개 과정에서 `ReservationId`와 `PaymentId`가 결정적으로 유지되는지 검증한다.
- [x] server-side assertion이 resumed order event sequence를 확인한다.
- [x] payment failure 보상 흐름이 `PaymentFailedEvent>InventoryReleasedEvent>OrderFailedEvent` 순서로 남고, read model은 공통 문서의 terminal `Failed` 계약을 유지한다.
- [x] terminal projection 삭제 뒤 `ContinueOrderWorkflowReq`가 event stream에서 read model을 복구하는지 검증한다.
- [x] build proof를 남긴다.
- [x] sample regression test proof를 남긴다.
- [x] `run_sample.sh` proof를 남긴다.
- [x] `run_sample.ps1` proof를 남긴다.
- [x] Codex 문서 반영 리뷰 결과가 `이슈 없음`이다.
- [x] POSD/DDD 리뷰 결과가 `이슈 없음`이다.

### 6. GameQuest

- [x] `GAMEQUEST_REDIS_ENDPOINT`와 `GAMEQUEST_REDIS_KEY_PREFIX`가 `GameApi`와 `QuestMission`의 Redis location store에 전달된다.
- [x] sample store도 실행별 Redis endpoint와 key prefix를 사용하며, 파일 store fallback을 남기지 않는다.
- [x] runner는 외부 Redis endpoint, base port, log directory, store directory override를 읽지 않고 실행별 전용 Docker Redis와 동적 host port를 사용한다.
- [x] runner는 sample 이름과 실행 id를 포함한 Redis container 이름과 Redis key prefix를 사용하고, 자신이 만든 container만 cleanup한다.
- [x] `Shared/Messages.cs`에 공통 문서의 `JoinSessionReq` / `JoinSessionRes` 흐름이 반영된다.
- [x] 기존 `SubscribeQuestReq` 호환 경로를 제거하고, client self-check 주 경로는 `JoinSessionReq` 하나만 사용한다.
- [x] stream session handler가 gameplay action request를 받는다.
- [x] `KillMonsterReq`, `CollectItemReq`, `CompleteMissionReq`, `EnterAreaReq`, `UnlockFeatureReq`는 stream request/reply로 처리된다.
- [x] handler는 기존 `GameplayActionService`를 재사용하고 action 책임을 client나 runner로 밀어내지 않는다.
- [x] client self-check는 join 후 같은 stream으로 gameplay action과 progress/completion push를 검증한다.
- [x] projection delete/rebuild, kill-without-publish, server assertion 같은 server-side self-check hook은 HTTP로 남겨도 된다.
- [x] server assertion이 reconnect, projection rebuild, reward idempotency, reset/reconcile evidence를 확인한다.
- [x] server assertion이 owner 비활성 또는 재활성에 대응하는 rehydrate 복원 evidence를 확인한다.
- [x] server assertion이 2 노드 scale-out과 player별 owner 분산 evidence를 확인한다.
- [x] build proof를 남긴다.
- [x] sample regression test proof를 남긴다.
- [x] `run_sample.sh` proof를 남긴다.
- [x] `run_sample.ps1` proof를 남긴다.
- [x] Codex 문서 반영 리뷰 결과가 `이슈 없음`이다.
- [x] POSD/DDD 리뷰 결과가 `이슈 없음`이다.

## Codex 문서 반영 리뷰 게이트

각 샘플 구현과 runner proof가 끝나면 다음 요청으로 Codex 에이전트 리뷰를 받는다. 이 리뷰는
READ-ONLY로 수행한다.

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
8. runner가 실행별 전용 Docker Redis container를 직접 띄우고, 고유 endpoint/key prefix를 전달하며,
   실행 종료 시 자신이 만든 container만 정리하는가.
9. 외부 Redis endpoint 재사용 mode가 남아 있지 않고, 동시에 도는 다른 테스트와 Redis container 이름,
   host port, key prefix가 충돌하지 않는가.
10. Redis host port 고정값, sample 이름만 들어간 container 이름, 실행 id 없는 key prefix, broad cleanup처럼
    병렬 테스트 간섭을 만들 수 있는 runner 구현이 남아 있지 않은가.

출력:
- findings를 심각도 순으로 먼저 적어줘.
- 각 finding은 파일:라인 근거를 붙여줘.
- 이슈가 없으면 정확히 `이슈 없음`이라고 적어줘.
```

리뷰 결과에 substantive issue가 하나라도 있으면 다음 샘플로 넘어가지 않는다. 같은 샘플 안에서 수정,
검증, 재리뷰를 반복한다.

## POSD/DDD 리뷰 게이트

Codex 문서 반영 리뷰가 `이슈 없음`이면 다음 요청으로 POSD/DDD 관점의 별도 READ-ONLY 리뷰를 받는다.

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

이 리뷰에서 나온 항목은 모두 같은 샘플 안에서 처리한다. 리팩토링이 public contract나 runner evidence를
바꾸면 build와 runner proof를 다시 수행하고 두 리뷰를 다시 받는다.

## 검증 기록

| 샘플 | build proof | runner proof | 문서 반영 리뷰 | POSD/DDD 리뷰 | 완료 판정 |
|------|-------------|--------------|----------------|---------------|-----------|
| `Bingo` | `dotnet build framework/languages/dotnet/samples/Bingo/Bingo.csproj --maxcpucount:1` 통과, `dotnet test framework/languages/dotnet/tests/Zlink.Framework.SampleRegressionTests/Zlink.Framework.SampleRegressionTests.csproj --filter "FullyQualifiedName~Bingo" --no-restore` 통과 | `BINGO_KEEP_RUN_DIR=1 timeout 240 ./run_sample.sh` 통과(runDir `/tmp/tmp.KqEHAJmUEZ`), `BINGO_KEEP_RUN_DIR=1 timeout 240 pwsh -NoProfile -File ./run_sample.ps1` 통과(runDir `/tmp/bingo-dotnet-8775f24ac7d44aee83db27617646ce63`). evidence: 실행별 전용 Docker Redis endpoint/key prefix 전달, 외부 Redis 재사용 mode 없음, startup sleep/env 없음, `bingo=completed`, `stream-inbound sample=Bingo ... Notify`, `message flow`, actor leave/destroy marker, runner가 만든 Redis container cleanup | `이슈 없음` | `이슈 없음` | completed |
| `TicTacToe` | `dotnet build bindings/dotnet/src/Zlink/Zlink.csproj --maxcpucount:1` 통과, `dotnet build framework/languages/dotnet/samples/TicTacToe/TicTacToe.sln --maxcpucount:1` 통과, `dotnet test framework/languages/dotnet/tests/Zlink.Framework.SampleRegressionTests/Zlink.Framework.SampleRegressionTests.csproj --filter "FullyQualifiedName~TicTacToe" --no-restore` 통과, `dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj --filter "FullyQualifiedName~actor_dispatch_preserves_multipart_message_across_no_data_turn" --no-restore` 통과 | `TICTACTOE_KEEP_RUN_DIR=1 timeout 240 ./run_sample.sh` 통과(runDir `/tmp/tmp.BFC56yVzJ7`), `TICTACTOE_KEEP_RUN_DIR=1 timeout 240 pwsh -NoProfile -File ./run_sample.ps1` 통과(runDir `/tmp/tictactoe-dotnet-8d0d0b2c9fb84ddea15b3503b25574f1`). evidence: room route store용 실행별 전용 Docker Redis endpoint/key prefix 전달, 외부 Redis 재사용 mode 없음, base port/endpoint/log directory 외부 override 없음, 실행별 동적 port와 실행별 임시 log directory 사용, `tictactoe=completed`, `stream-inbound sample=TicTacToe` response/push, `observer-win-milestone=verified`, `message flow`, `LeaveGameReq` 양 플레이어 완료, entry spot actor destroy 양 플레이어 완료, `dispatch-error`, `message flow outcome=error`, `ZlinkRecvException`, `Unhandled exception` 없음, runner가 만든 Redis container cleanup | `이슈 없음` | `이슈 없음` | completed |
| `SupportChat` | `dotnet build framework/languages/dotnet/samples/SupportChat/SupportChat.csproj --maxcpucount:1` 통과, `dotnet test framework/languages/dotnet/tests/Zlink.Framework.SampleRegressionTests/Zlink.Framework.SampleRegressionTests.csproj --filter "FullyQualifiedName~SupportChat_Runner" --no-restore` 통과 | `SUPPORTCHAT_KEEP_RUN_DIR=1 timeout 240 ./run_sample.sh` 통과(runDir `/tmp/tmp.TMHK1COjt9`), `SUPPORTCHAT_KEEP_RUN_DIR=1 timeout 240 pwsh -NoProfile -File ./run_sample.ps1` 통과(runDir `/tmp/supportchat-dotnet-924e218a00bd4beda541b4625ea17b74`). evidence: 실행별 전용 Docker Redis endpoint/key prefix 전달, 외부 Redis 재사용 mode 없음, base port/endpoint/log directory 외부 override 없음, 실행별 동적 port와 실행별 임시 log directory 사용, 실제 host wiring이 쓰지 않는 endpoint 환경 변수 제거, `supportchat=completed`, `supportchat-closed-typing-ignore=verified`, runner stdout의 `supportchat-server-evidence=completed`, `message flow`, `status=WaitingForAgent`, `status=Active`, `status=WaitingForClose`, `status=Closed`, `Unhandled exception`, `ZlinkRecvException`, `dispatch-error` 없음, runner가 만든 Redis container cleanup | `이슈 없음` | `이슈 없음` | completed |
| `DeliveryDispatch` | `dotnet build framework/languages/dotnet/samples/DeliveryDispatch/DeliveryDispatch.sln --maxcpucount:1` 통과, `dotnet test framework/languages/dotnet/tests/Zlink.Framework.SampleRegressionTests/Zlink.Framework.SampleRegressionTests.csproj --filter "FullyQualifiedName~DeliveryDispatch" --no-restore` 통과 | `timeout 240 ./run_sample.sh` 통과, `timeout 240 pwsh -NoProfile -File ./run_sample.ps1` 통과. evidence: 실행별 전용 Docker Redis endpoint/key prefix 전달, `deliverydispatch=completed`, `topology=ready`, `deliverydispatch-reassignment=completed`, `deliverydispatch-server-evidence=completed`, `deliverydispatch-runner-evidence=completed`, `deliverydispatch courier-session: bound courier=courier-a`, `deliverydispatch courier-session: bound courier=courier-b`, `deliverydispatch tracking: status`, `message flow`, runner가 만든 Redis container cleanup | `이슈 없음` | `이슈 없음` | completed |
| `ShoppingMall` | `dotnet build framework/languages/dotnet/samples/ShoppingMall/ShoppingMall.csproj --maxcpucount:1` 통과, `dotnet test framework/languages/dotnet/tests/Zlink.Framework.SampleRegressionTests/Zlink.Framework.SampleRegressionTests.csproj --filter "FullyQualifiedName~ShoppingMall_Runner" --no-restore` 통과 | `SHOPPINGMALL_KEEP_RUN_DIR=1 timeout 240 ./run_sample.sh` 통과(runDir `/tmp/tmp.PLRkl2NHCL`), `SHOPPINGMALL_KEEP_RUN_DIR=1 timeout 240 pwsh ./run_sample.ps1` 통과(runDir `/tmp/shoppingmall-dotnet-68faf73ba42a4482bb3f85f63ab576da`). evidence: 실행별 전용 Docker Redis endpoint/key prefix 전달, 외부 Redis 재사용 mode 없음, base port/endpoint/log directory 외부 override 없음, 실행별 동적 host port와 실행별 임시 log directory 사용, sample store도 Redis 사용, runner가 만든 Redis container cleanup, `shoppingmall=completed`, `shoppingmall-server-evidence=completed`, `shoppingmall evidence:`, `startedIdempotency=7`, `owners=workflow-a,workflow-b`, `PaymentFailedEvent>InventoryReleasedEvent>OrderFailedEvent`, `PrepareInventoryReservedCheckpointReq`, `ContinueOrderWorkflowReq`, `message flow` | `이슈 없음` | `이슈 없음` | completed |
| `GameQuest` | `dotnet build framework/languages/dotnet/samples/GameQuest/GameQuest.csproj --maxcpucount:1` 통과, `dotnet test framework/languages/dotnet/tests/Zlink.Framework.SampleRegressionTests/Zlink.Framework.SampleRegressionTests.csproj --filter "FullyQualifiedName~GameQuest_Runner" --no-restore` 통과 | `GAMEQUEST_KEEP_RUN_DIR=1 timeout 240 ./run_sample.sh` 통과(runDir `/tmp/tmp.2li6QfOVU0`), `GAMEQUEST_KEEP_RUN_DIR=1 timeout 240 pwsh ./run_sample.ps1` 통과(runDir `/tmp/gamequest-dotnet-caa88b0fc78e49f89d1929537116b559`). evidence: 실행별 전용 Docker Redis endpoint/key prefix 전달, 외부 Redis 재사용 mode 없음, base port/endpoint/log/store directory 외부 override 없음, 실행별 동적 host port와 실행별 임시 log directory 사용, sample store도 Redis 사용, runner가 만든 Redis container cleanup, stream `JoinSessionReq`, stream gameplay action request/reply, route mesh `ApplyGameplayEventReq`가 `PlayerId` owner로 전달된 뒤 `PlayerQuestSpot` SpotRoute request로 처리됨, event stream replay/fold 기반 판정, owner close 뒤 재활성 evidence, `gamequest-server-evidence=completed`, `gamequest api event routed`, `gamequest mission processed`, `gamequest player quest spot ready`, `QuestProgressReconciledEvent`, `message flow` | `이슈 없음` | `이슈 없음` | completed |

## 작업 진행 프롬프트

아래 프롬프트로 이 계획을 실행한다.

```text
작업 디렉토리: /home/hep7/project/kairos/zlink

목표:
framework/doc/plan/framework-dotnet-sample-common-conformance-plan.ko.md 계획을 기준으로
framework/languages/dotnet/samples 의 정본 6종을 공통 샘플 문서에 맞춘다.

대상 샘플:
- Bingo
- TicTacToe
- SupportChat
- DeliveryDispatch
- ShoppingMall
- GameQuest

절대 기준:
- 공통 문서가 기준이다.
- 공통 문서를 구현에 맞춰 바꾸지 않는다.
- 한 번에 한 샘플만 진행한다.
- 한 샘플의 구현, build, runner proof, Codex 문서 반영 리뷰, POSD/DDD 리뷰가 모두 끝나기 전에는 다음 샘플로 넘어가지 않는다.
- Codex 문서 반영 리뷰와 POSD/DDD 리뷰가 모두 정확히 `이슈 없음`일 때만 해당 샘플을 완료로 기록한다.
- dirty worktree가 있을 수 있다. 기존 변경을 되돌리지 말고, 현재 샘플 범위만 좁게 수정한다.
- private/internal API, reflection, raw frame, test-only adapter, sleep/retry 증가로 gap을 숨기지 않는다.
- `TicTacToe`를 제외한 모든 샘플은 store 기능 때문에 Redis location store를 사용해야 한다.
- `TicTacToe`는 location store 자동 연결 샘플이 아니라 Redis room route store를 사용하는 예외 샘플이다.
- 모든 Redis 사용 runner는 실행별 전용 Docker Redis container를 직접 띄운다. 외부 Redis endpoint 재사용
  mode는 제공하지 않는다.
- Redis container 이름, host port, key prefix는 동시에 도는 다른 테스트와 간섭하지 않도록 실행별로 고유해야 한다.
- Redis host port 고정값을 쓰지 않는다. Docker가 빈 host port를 배정하게 하거나 동등하게 충돌을 피할 수
  있는 방식으로 port를 정한다.
- Redis container 이름과 key prefix에는 sample 이름과 실행 id를 함께 넣는다. cleanup은 그 실행 id로 만든
  container만 대상으로 삼고, sample 이름만으로 여러 실행의 container를 지우지 않는다.
- 샘플 코드는 사용자가 따라 할 public API 예시다. runner 통과를 위해 업무 코드에 codec, route, endpoint,
  storage, retry 같은 내부 결정을 노출하지 않는다.

시작 절차:
1. `git status --short`로 기존 변경을 확인한다.
2. plan 문서의 진행 순서에서 첫 pending 샘플 하나만 고른다.
3. 해당 샘플의 공통 문서, sample-local README, runner, client scenario, shared contracts, server role을 읽는다.
4. plan 문서의 "모든 샘플 공통 체크리스트"와 해당 샘플 체크리스트를 작업 체크리스트로 옮긴다.
5. 이미 완료된 샘플은 다시 수정하지 않는다. 단, 현재 샘플 검증에 필요한 공통 regression test만 좁게 갱신한다.

샘플별 진행:
1. 공통 문서 요구를 구현에 반영한다.
2. sample-local README가 구현 사실과 충돌하면 sample-local README만 갱신한다.
3. focused build를 실행한다.
4. sample regression test가 있거나 추가할 가치가 있으면 focused test를 실행한다.
5. `run_sample.sh`와 `run_sample.ps1`을 모두 실행하고 client/server evidence marker를 확인한다.
6. Redis runner 계약을 검증한다.
   - runner가 실행별 전용 Docker Redis container를 직접 띄우는지 확인한다.
   - Redis container 이름, host port, key prefix가 실행별로 고유해서 동시에 도는 다른 테스트와 간섭하지 않는지 확인한다.
   - Redis host port 고정값, sample 이름만 들어간 container 이름, 실행 id 없는 key prefix, broad cleanup이 없는지 확인한다.
   - `TicTacToe`를 제외한 샘플은 Redis location store endpoint와 key prefix가 서버 역할에 전달되는지 확인한다.
   - `TicTacToe`는 Redis room route store endpoint와 key prefix가 전달되는지 확인한다.
   - runner가 자신이 만든 Redis container를 cleanup하는지 확인한다.
   - 외부 Redis endpoint 재사용 mode가 남아 있으면 gap으로 처리한다.
7. runner proof는 build-only가 아니어야 한다. client 완료 marker, server evidence marker, message flow,
   dispatch 오류 부재 또는 오류 검증 marker처럼 공통 문서의 self-check 완료 조건을 보여 주는 로그를 확인한다.
8. 실패하면 같은 샘플 안에서 원인을 수정하고 다시 검증한다. 실패를 숨기기 위해 긴 sleep, 과도한 retry,
   private helper, raw frame, test-only adapter를 추가하지 않는다.
9. Codex 문서 반영 리뷰를 READ-ONLY로 요청한다. 리뷰 템플릿은 plan 문서의
   "Codex 문서 반영 리뷰 게이트"를 그대로 사용한다.
10. 문서 반영 리뷰에서 이슈가 있으면 같은 샘플 안에서 수정, build, runner proof, 재리뷰를 반복한다.
11. 문서 반영 리뷰가 정확히 `이슈 없음`이면 POSD/DDD 리뷰를 READ-ONLY로 요청한다. 리뷰 템플릿은 plan
    문서의 "POSD/DDD 리뷰 게이트"를 그대로 사용한다.
12. POSD/DDD 리뷰에서 가치 있는 리팩토링 이슈가 나오면 같은 샘플 안에서 수정하고 build/runner/review를 반복한다.
13. 두 리뷰가 모두 정확히 `이슈 없음`이면 plan 문서의 체크박스, 진행 순서 상태, 검증 기록을 갱신하고 다음 샘플로 넘어간다.

샘플 완료 기록:
- 진행 순서 표의 상태를 `completed`로 바꾼다.
- 해당 샘플 체크리스트를 실제 확인한 항목만 체크한다.
- 검증 기록 표에 build/test/runner 명령과 핵심 evidence marker를 적는다.
- 리뷰 결과는 두 리뷰가 모두 `이슈 없음`일 때만 `이슈 없음`으로 적는다.

완료 보고:
- 완료한 샘플
- 수정 파일 목록
- focused test 명령과 결과
- build 명령과 결과
- runner 명령과 evidence marker
- Codex 문서 반영 리뷰 결과
- POSD/DDD 리뷰 결과
- 남은 pending 샘플
```

## 최종 완료 조건

1. 진행 순서 표의 모든 샘플 상태가 완료다.
2. 검증 기록 표에 build proof와 runner proof가 실제 명령과 로그 marker로 기록되어 있다.
3. 각 샘플의 Codex 문서 반영 리뷰가 `이슈 없음`이다.
4. 각 샘플의 POSD/DDD 리뷰가 `이슈 없음`이다.
5. `git diff --check`가 통과한다.
6. 최종 `git status --short`에서 이번 작업 범위와 무관한 변경을 건드리지 않았음을 확인한다.
