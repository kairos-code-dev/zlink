# S5 Core 리뷰 finding ledger — iteration 9 병합

Snapshot: `f5000d2fe` (631 files, scope hash `5eeb7c90…`). 연장 전체 pass
5회차.

## 1. 판정 요약

| 리뷰어 | 판정 | 축 |
|---|---|---|
| R1 Codex | **NOT CLEAN** | I1 NOT CLEAN(high 2·medium 1) · I2 NOT CLEAN · I3 NOT CLEAN |
| R2 Claude Sonnet | **NOT CLEAN** | I1 NOT CLEAN(high 1·low 1) · I2 NOT CLEAN(low 1) · I3 NOT CLEAN(low 2) |

두 리뷰 결과를 원인별로 병합하면 유효 finding은 7건이다. 두 high finding은
request 전달과 terminal completion의 실패 원자성이라는 하나의 operation
lifecycle 경계에 속하지만, 시작 transaction과 종료 transaction의 검증
조건이 다르므로 별도 항목으로 유지했다.

## 2. 병합 finding과 Coordinator 해결

### 2.1 S5-R1-09-01 (high) — request 전달 뒤 timeout 준비 실패

기존 구현은 operation 등록과 request 전달 뒤 timeout task를 할당했다. 마지막
할당이 실패하면 호출자는 실패를 받지만 target은 request를 처리할 수 있고,
output ID가 없는 operation과 reply route가 남았다.

두 설계안을 비교했다.

- 대안 A: 각 Node·Channel·Spot·Actor·STREAM 호출부에 전달 실패와 timeout
  실패 rollback을 각각 추가한다. 호출부마다 operation, route와 scheduler
  지식이 반복되고 새 request 경로가 같은 순서 오류를 만들 수 있다.
- 대안 B: operation, 선택적 reply route와 timeout 준비를 하나의 request
  transaction이 소유한다. commit 전 모든 return은 소멸자 rollback을 거치고,
  target 전달이 끝난 호출부만 commit한다.

대안 B를 선택해 `operation_submission_t`와
`operation_timeout_guard_t`를 추가했다. scheduler callback은 transaction의
commit 또는 cancel 결정을 기다린다. 모든 local·remote Node, Channel, Spot,
Actor, STREAM request와 비동기 lifecycle operation이 같은 객체를 사용하며
`register_operation()`은 이 구현 파일의 private helper가 됐다.

검증은 timeout 준비 단계에 allocation fault를 주입하고
`ZLINK_SUBMIT_OUT_OF_MEMORY`/`ENOMEM`, 0인 output ID, operation 0개, reply
route 0개를 함께 확인한다.

### 2.2 S5-R1-09-02 = Sonnet high (high) — reply와 completion 선소비

기존 generic reply와 Actor join reply는 payload·wire·completion admission의
fallible 단계보다 먼저 token, operation 또는 membership을 소비했다.
completion record 할당 실패도 조용히 terminal 결과를 버렸다.

두 설계안을 비교했다.

- 대안 A: 각 reply·timeout·shutdown·Actor·STREAM 경로가 실패 시 token,
  operation과 domain state를 역순 복원한다. 복원 자체가 다시 할당에 실패할
  수 있고 state 전이 지식이 여러 파일에 남는다.
- 대안 B: operation 등록 시 terminal record와 list node를 미리 할당하고,
  terminal payload 준비와 mailbox admission 뒤 같은 node lock에서 operation,
  token과 관련 domain state를 commit한다.

대안 B를 선택했다. `completion_reservation_t`가 operation과 함께 저장소를
소유하고 `complete_pending_operation()`은 `list::splice`로 이를
infrastructure mailbox에 옮긴다.
`complete_pending_operation_with_commit()`은 Actor membership·destroy와
STREAM binding 전이를 같은 lock의 non-allocating callback으로 commit한다.
generic reply는 payload를 먼저 준비하고 remote reply route는 성공한 wire
submit 뒤에만 소비한다.

검증은 reply payload 준비와 ready-index admission에 각각 fault를 주입한다.
두 실패 뒤 같은 token으로 재시도할 수 있고, 성공 뒤 두 번째 reply만
`EALREADY`이며 requester가 terminal completion을 정확히 한 건 받는지
확인한다. STREAM bind, unbind와 bound-session close도 completion admission
fault 뒤 binding과 연결 상태가 바뀌지 않고 clean retry가 성공하는지 같은
lifecycle case에서 검사한다. bound-session close는 raw STREAM observer를
node lock 안에서 호출하지 않도록 fallible terminal 준비와 non-allocating
commit을 둘로 나눴다.

### 2.3 S5-R1-09-03 (medium) — package version·ABI 불일치

Debian·RPM·NuGet recipe의 6.0.3/4.2.3 식별자를 CMake의
10.0.0/SOVERSION 10에 맞췄다. Debian binary package와 ABI 파일 이름은
`libzlink10*`, RPM library 이름은 `libzlink10`, NuGet version과 binary
경로는 `10.0.0`/`10_0_0`이다.

`contract_public_surface`가 CMake version과 SOVERSION을 읽어 세 package
recipe의 version, ABI 이름과 Debian 파일 이름을 검사한다. 이전 ABI package
이름은 upgrade 충돌 선언에서만 유지한다.

### 2.4 S5-R1-09-04 (low) — 제거 구현 type의 dead declaration

사용처가 없는 `spot_node_t`, `spot_pub_t`, `spot_sub_t`,
`spot_internal_receiver_t` forward declaration을
`monitor_api_internal.hpp`에서 제거했다. 전체 Core source에서 이름이 남지
않는지 확인한다.

### 2.5 Sonnet low — 사용되지 않는 operation deadline

timeout 판정은 scheduler가 전담하고 읽는 곳이 없던
`pending_operation_t::deadline_ms` 필드와 등록 인자를 제거했다.

### 2.6 Sonnet low — 제거 식별자의 같은 철자 재사용을 gate가 놓침

제거된 struct typedef 철자 `zlink_actor_join_result_t`가 현재 enum typedef로
쓰이는 의도된 예외를 `REUSED_IDENTIFIER`와 허용 kind `ENUM_TYPE`으로
기록했다. gate는 이 이름이 허용 kind 하나로만 선언되는지 검사한다. 옛 struct
모양이나 다른 kind가 같은 이름으로 추가되면 실패한다.

### 2.7 Sonnet low — C ABI OOM policy 27곳 중복

27개 공개 submit 진입점의 `bad_alloc` 장벽은 유지하되 결과·errno 결정은
`submit_out_of_memory_result()` 한 곳으로 이동했다. C ABI 경계는 각 진입점이
직접 봉인하고 정책 지식만 깊은 helper가 소유한다.

## 3. POSD·DDD 재점검

- request transaction은 operation, reply route와 timeout 준비 순서를 한
  모듈에 숨겨 호출부가 rollback 순서를 알 필요가 없게 한다.
- completion reservation은 storage, mailbox admission과 operation 소비
  순서를 한 곳에 숨긴다.
- Actor와 STREAM의 domain 전이는 terminal admission과 같은 lock에서
  commit하되, entity 정책은 각 API 파일의 callback에 남겨 runtime이
  entity 의미를 소유하지 않게 했다.
- 공개 API와 호출자 설정은 추가하지 않았다.

## 4. 검증

- 전체 Core CTest 85/85와 public-surface/package metadata gate가 통과했다.
- ASAN/UBSAN 5개 Mesh 바이너리의 42개 case가 report 없이 통과했다.
- shared library SONAME `libzlink.so.10`, Debian changelog `10.0.0-0.1`,
  NuGet XML parse와 recipe의 정적 version·ABI 검사가 통과했다. 현재 환경에
  Debian build dependency와 RPM/NuGet package CLI가 없어 실제 세 package
  산출물 생성은 S6 release-candidate gate에서 수행한다.
- TSAN lifecycle 13/13과 stress 3/3은 통과했다. 최종 lifecycle 실행의 경고
  15건은 기존 auto-HWM lock-order 14와 raw mailbox 1이고 stress 경고 3건은
  기존 auto-HWM 계열이다. 같은 수정 소스의 앞선 실행에서는 기존 pipe
  teardown race 1건도 재현됐다. 신규 Mesh operation/completion race나 STREAM
  잠금 순환은 관찰되지 않았다.

## 5. 다음 단계

수정 commit을 고정한 뒤 iteration 10에서 Codex와 Claude Sonnet에 동일한
프롬프트와 동일한 전체 scope를 제공한다. 두 리뷰어가 I1·I2·I3를 모두 다시
검토하며, 리뷰를 시작하기 전에는 이 수정 묶음을 clean 근거로 간주하지 않는다.
