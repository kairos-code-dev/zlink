# POSD 2차 리팩토링 Post-Residual 재리뷰

> 상태: active
> 대상 범위: `core/`, `core/tests/`
> 기준 원칙: POSD(깊은 모듈 + 얇은 hot-path + 성능 포함 구조 판단) + 공개 API/ABI 유지
> 관련 문서:
> - `doc/plan/refactor/2nd/core-system-posd-refactor-master-plan.ko.md`
> - `doc/plan/refactor/2nd/core-system-posd-refactor-gap-review.ko.md`
> - `doc/plan/refactor/2nd/core-system-posd-refactor-remaining-execution-guide.ko.md`
> - `doc/plan/refactor/2nd/core-system-posd-refactor-residual-execution-spec.ko.md`
> 기준 시점: 2026-03-25 현재 워크트리 재리뷰

## 1. 문서 목적

이 문서는 `master plan -> gap review -> remaining execution guide ->
residual execution spec`까지 진행된 뒤, 현재 코드 상태를 다시 읽어
남은 구조 갭을 고정하는 후속 재리뷰 문서다.

이 문서는 기존 문서의 완료 판정을 뒤집기 위한 문서가 아니다.
또한 기존 residual 실행 스펙을 대체하는 문서도 아니다.

이 문서의 목적은 아래 세 가지다.

- residual 실행 이후에도 왜 추가 리팩토링 계획이 필요한지 현재 코드 기준으로 설명한다.
- 팀장님이 고정한 새 기준인 `깊은 모듈 + 얇은 hot-path`를 기준으로 다음 구조 우선순위를 다시 고정한다.
- 현재 진행 중인 성능 개선 작업이 중간에 방향을 잃지 않고,
  필요한 추가 구조 개선까지 완료한 뒤 perf 최종 마감으로 연결되게 만든다.

핵심 해석은 아래처럼 고정한다.

```text
이번 문서에서 말하는 'POSD 관점의 올바른 구조'는
정보 은닉과 변경 증폭 억제만 만족하는 구조가 아니라,
공통 hot-path의 비용까지 함께 설명되는 구조다.
```

## 2. 현재 단계 해석

현재 단계는 "2차 리팩토링을 다시 처음부터 설계하는 단계"가 아니다.
이미 아래 순서를 한 번 거친 뒤의 후속 재판정 단계다.

1. 2차 POSD 마스터 플랜 작성
2. gap review로 실제 진전과 residual 구분
3. remaining execution guide로 남은 항목 실행 순서 고정
4. residual execution spec으로 `5.2A`, `5.3A`, `5.6A` 구현 스펙 고정
5. 현재 워크트리 기준 post-residual 재리뷰

따라서 지금 해야 할 일은 새 방향을 찾는 것이 아니다.
이미 맞게 잡힌 방향을 현재 코드 기준으로 다시 검증하고,
남은 구조-성능 갭을 어디까지 더 닫아야 하는지 정하는 일이다.

추가 해석:

- `remaining-execution-guide`를 authority로 한 랄프 루프 작업 로그는
  현재 실행이 실제로 어떤 owner에 묶여 있는지 보여주는 보조 근거로 사용한다.
- 새 문서의 구조 판단은 코드 기준으로 내리되,
  immediate next step과 current owner는 latest 실행 로그와 충돌하지 않아야 한다.

## 3. 실행 로그 기준 현재 active issue

`remaining-execution-guide`와 `doc/plan/refactor/2nd/logs/`의 latest 기록을 기준으로,
현재 실제로 진행 중인 문제는 아래처럼 좁혀진다.

- `core/`와 `core/tests/` 범위에서 실행 가이드 `5.0` 표 밖의 새 구현 항목은
  추가로 발견되지 않았다는 기록이 반복해서 남아 있다.
- current worst tuple은 주로 `SPOT multi wss` 경로에 남아 있다.
- 특히 `wss 64B`, `wss 256B`, `wss 262144B`가 서로 다른 방향으로 흔들리며,
  단일 HWM 상향 같은 one-knob 조정으로는 닫히지 않는다고 기록돼 있다.
- latest owner는 `subscription replay`보다
  `mesh_pub` live budget, ready-peer driven budget application,
  secure steady-state send path, large-payload carryover 쪽으로 더 좁혀졌다.
- 관련 owner 경계로는
  `spot_data_plane_internal.hpp`, `spot_node_control.cpp`,
  `spot_data_plane_runtime.cpp`가 반복해서 지목된다.

핵심 해석:

```text
현재 immediate next step은
새로운 대형 구조 후보를 무작정 여는 것이 아니라,
`spot` secure multi-peer hot-path에서 이미 드러난 active owner를
더 좁혀 구조와 성능을 함께 정리하는 데 있다.
```

추가 해석:

- 현재 단계는 "구조 작업이 끝난 뒤 나중에 perf를 시작하는 단계"가 아니다.
- 이미 perf 회복 루프가 진행 중이고,
  그 perf를 최종 마감하기 위해 필요한 추가 구조 개선 항목을 같이 수행하는 단계다.
- 따라서 이 문서의 작업 목록은 별도 차기 phase가 아니라,
  현재 성능 개선 작업을 끝내기 위한 in-flight 구조 보강 목록으로 해석한다.

## 4. 왜 residual까지 했는데도 추가 계획이 필요한가

이 질문의 답은 "기존 계획이 잘못됐다"가 아니다.
답은 아래 세 가지가 동시에 참이기 때문이다.

### 3.1 기존 문서도 residual을 이미 인정했다

기존 2차 문서군은 애초에 모든 구조 갭이 닫혔다고 말하지 않았다.

- `gap review`는 `socket_base_t`, `ctx_t`, 일부 service 허브를 미완료 항목으로 남겼다.
- `remaining execution guide`도 `socket_base`, `ctx`, `registry`, `spot_data_plane` 등을 남은 큰 허브로 적고 있었다.
- `residual execution spec`도 `5.2A`, `5.3A`, `5.6A`만을 대상으로 owner 이동을 고정했지,
  `core` 전체가 최종 구조에 도달했다고 선언하지 않았다.

즉 현재 추가 후보가 보이는 것은 계획 위반의 증거가 아니라,
이미 문서상 남아 있던 residual이 계속 보인다는 뜻에 가깝다.

### 3.2 기존 완료 판정과 현재 판정 기준이 다르다

기존 문서의 완료 판정은 대체로 아래를 기준으로 읽을 수 있었다.

- 허브 축소
- ownership 분업 도입
- 공개 계약 유지
- 테스트와 thread-safe 계약 유지
- 성능 비퇴행

반면 현재 팀장님이 고정한 기준은 더 엄격하다.

- deep module이어야 한다.
- hot-path는 얇아야 한다.
- 구조 자체가 성능 비용을 설명 가능해야 한다.
- perf 개선이 구조와 충돌하면 구조 기준부터 다시 본다.

즉 지금 보이는 추가 리팩토링 후보는 "문서가 틀려서 새로 생긴 것"이 아니라,
POSD를 성능까지 포함한 구조 기준으로 다시 읽었기 때문에 더 선명해진 것이다.

### 3.3 후속 변경과 perf 회복 작업이 공통 경로를 다시 무겁게 만들 수 있다

리팩토링 이후 기능 추가와 perf 회복 작업이 반복되면,
구조상 중요한 공통 경로에 다시 정책과 상태가 재집중되기 쉽다.

특히 아래 같은 허브는 그런 재집중이 일어나기 쉬운 지점이다.

- `socket_base_t`
- `socket_message_api.cpp`
- `ctx_t`
- `spot_data_plane`
- `registry/discovery`

즉 현재의 추가 계획은 "새로운 리팩토링 욕심"이 아니라,
이미 분리된 구조 위에 다시 쌓인 hot-path 부담을 정리하는 후속 마감 작업으로 보는 것이 맞다.

핵심 평결은 아래처럼 고정한다.

```text
즉 "기존 계획이 잘못됐다"가 아니라,
"기존 계획은 실제로 큰 진전을 만들었고,
그 위에서 남아 있던 residual과 더 높은 구조 기준이 다시 드러난다"가 정확한 해석이다.
```

## 5. 현재 코드 전반 평가

### 4.1 좋은 점

현재 `core`는 전면 재설계가 필요한 상태가 아니다.
오히려 아래 기반은 분명히 생겼다.

- `api` 분해는 실제로 진전됐다.
- `service_runtime_base_t`, `socket_close_ops_t`, `ctx_t` close-wait 분업은 실제 구조로 들어갔다.
- `ctx_bootstrap_t`, `ctx_termination_t` 같은 private owner 분리가 시작됐다.
- `multipart_send_txn` 같은 공통 deep module 추출은 방향이 맞다.
- `gateway`, `discovery`, `spot` 일부 분해는 helper 수준을 넘어 의미 있는 ownership 분리를 만들었다.

### 4.2 부분 달성

반면 아래 항목은 여전히 부분 달성으로 보는 편이 맞다.

- `socket_base_t`는 runtime state를 묶었지만 semantic facade로 충분히 줄지는 않았다.
- `ctx_t`는 bootstrap/termination helper가 생겼지만 registry와 orchestration을 아직 넓게 안고 있다.
- `socket_message_api.cpp`는 예전 `zlink.cpp` mega-hub를 줄였지만 새로운 message 집선 허브가 됐다.
- `spot_data_plane`, `registry`, `spot_subject_access`는 helper 분리는 진행됐지만 ownership 완결은 아니다.

### 4.3 현재 핵심 문제

현재 문제는 "나쁜 코드가 사방에 많다"가 아니다.
현재 문제는 "소수의 공통 허브가 구조 복잡도와 hot-path 비용을 동시에 쥐고 있다"는 점이다.

핵심 요약:

```text
현재 `core`는 대수술이 필요한 상태는 아니다.
반면 공통 경로의 중심 허브 몇 개가 과체중이라,
구조 변경과 성능 회복이 같은 파일군으로 계속 재집중되는 상태다.
```

## 6. POSD 구조 판정 기준

이후 구조 평가는 아래 체크리스트로 고정한다.

- 호출자는 내부 세부를 덜 알아야 한다.
- ownership, lifecycle, invariant는 owner 하나에 모여야 한다.
- send/recv/dispatch/close-wait 같은 공통 hot-path는 얇아야 한다.
- hot-path에서 generic coordinator, monitor, callback, close orchestration이 직접 섞이지 않아야 한다.
- helper 추가만으로는 완료로 보지 않는다.
- owner 이동과 변경 범위 축소가 보여야 한다.
- perf 개선이 필요할 때 국소 변경으로 끝나는 구조여야 한다.

추가 해석 규칙:

```text
POSD 기준에서 깊은 모듈화와 성능은 분리된 목표가 아니다.
성능을 희생한 정보 은닉도 실패고,
구조를 무너뜨린 성능 편법도 실패다.
```

추가 고정 원칙:

```text
POSD 관점에서 옳은 구조라면,
공통 hot-path의 처리량·지연시간·락 경합까지 함께 개선 방향이 설명돼야 한다.
그 설명이 없으면 이번 문서에서는 미완료로 본다.
```

즉 이번 문서에서의 구조 평가는 아래를 함께 만족해야 한다.

- ownership과 information hiding이 좋아진다.
- send/recv/dispatch/forwarding 같은 공통 경로의 분기, 원자연산, 락, cross-layer hop이 줄어든다.
- perf 회복이 아니라도, 구조 변경 자체만으로 hot-path 비용이 설명 가능하게 낮아진다.

## 7. POSD 기준 재리뷰

이번 재리뷰에서는 "구조 냄새가 있다"만으로 우선순위를 주지 않는다.
아래처럼 `구조를 바로잡는 것이 곧 성능 판단까지 포함하는 곳`을 우선 대상으로 본다.

핵심 해석:

```text
이번 단계의 구조 리팩토링은 perf 이전 준비 작업이 아니라,
POSD 판단 자체가 공통 경로 성능개선을 포함하도록 설계돼야 한다.
```

### 6.1 최우선 기준

아래 조건을 동시에 만족하는 축을 최우선으로 둔다.

- 공통 owner가 너무 크다.
- hot-path에서 반복 호출된다.
- lock/atomic/dispatch 분기 비용이 직접 걸린다.
- 구조를 바로잡으면 perf tuple 회복 범위도 넓다.

### 6.2 공통 hot-path에서 다시 봐야 하는 지점

#### A. `socket_base_t` dispatch / monitor / lifecycle 축

구조 이유:

- semantic facade 안에 dispatch, callback, monitor, lifecycle 세부가 계속 같이 남아 있다.

성능 이유:

- dispatch 경로에서 반복되는 상태 조회, recursive lock, atomic load/store가 공통 경로에 걸린다.
- monitor snapshot과 attached pipe 조회도 socket 중심 state를 계속 스캔한다.

이번 문서에서의 요구:

- `socket_base_t` 분해는 "의미가 더 잘 보인다" 수준으로 끝내지 않는다.
- dispatch, monitor, lifecycle owner를 분리해 공통 경로의 동기화 비용을 실제로 낮춰야 한다.

#### B. `socket_message_api.cpp` message 집선 허브

구조 이유:

- socket/service/stream/spot 경로가 한 파일에서 계속 fallback과 분기로 모인다.

성능 이유:

- send/recv/publish/subscribe 경로에서 type 판별과 중복 검증이 누적된다.
- hot-path entry가 얇은 진입점이 아니라 집선 허브를 통과한다.

이번 문서에서의 요구:

- domain entry를 분리하고 공통 검증은 최소 helper로만 남긴다.
- 구조 분리가 곧 send/recv 경로 길이 감소로 이어져야 한다.

#### C. `ctx_t` registry + orchestration 축

구조 이유:

- registry owner와 startup/shutdown/resource coordinator가 한 객체에 과도하게 응집돼 있다.

성능 이유:

- 소켓 생성/종료, endpoint 연결, io thread 선택, pending inproc 정리의 판단이 같은 중심 객체에 모인다.
- perf 회복 시에도 변경 owner가 `ctx_t`로 재집중되기 쉽다.

이번 문서에서의 요구:

- `ctx_t`는 registry + termination contract owner로 줄인다.
- bootstrap/resource sequencing detail 분리는 구조 개선일 뿐 아니라,
  resource-path 성능 회귀를 더 국소화하는 효과까지 가져와야 한다.

#### D. `spot_data_plane` runtime / budget / protocol 축

구조 이유:

- runtime wiring, budget/hwm 정책, protocol forwarding owner가 완전히 분리되지 않았다.

성능 이유:

- secure multi transport 경로에서 budget, queue, forwarding, control snapshot 비용이 직접 throughput에 닿는다.
- data plane owner가 넓게 퍼져 있으면 perf 회복 루프가 다시 허브 수정으로 돌아간다.

이번 문서에서의 요구:

- runtime owner와 budget/policy owner를 더 분명히 나눈다.
- 분리의 목적은 코드 정리뿐 아니라 secure transport tuple의 회복 경로를 좁히는 데 있다.
- 현재 active issue 기준으로는
  `wss` small-message steady-state와 large-payload carryover를 함께 흔드는
  `mesh_pub` live budget / secure publish path를 먼저 owner 수준으로 더 좁혀야 한다.

#### E. `options_t`, `spot_node_t`, `discovery_t`, engine/transport 축

구조 이유:

- central option bag, top-level service coordinator, 대형 engine 구현이 아직 허브처럼 읽히는 부분이 있다.

성능 이유:

- 이 영역은 공통 hot-path보다는 한 단계 뒤지만,
  해석 owner가 넓으면 perf 개선 시 변경 범위가 다시 커진다.

이번 문서에서의 요구:

- 이번 단계에 작업 목록으로 포함하되,
  current active owner를 흐리지 않도록 공통 hot-path 축보다 뒤에서 정리한다.
- 구조 정리가 실제 perf owner 축소에 기여하는 경우에만 우선한다.

## 8. 코드 전반 우선순위 재판정

### 8.1 A등급 immediate active owner

현재 실행 로그와 코드 상태를 함께 보면,
즉시 수행 우선순위는 아래처럼 다시 좁혀진다.

1. `socket_base_t`
2. `socket_message_api.cpp`
3. `ctx_t`
4. `spot_data_plane_internal.hpp` / `spot_data_plane_runtime.cpp` /
   `spot_node_control.cpp` 경계의 secure `mesh_pub` hot path

즉 `spot` secure multi-peer owner는 넓은 service residual의 한 부분이 아니라,
현재 구조와 성능이 동시에 얽힌 immediate active owner로 취급한다.

### 8.2 A등급: POSD 최우선

#### A-1. `socket_base_t`

현재 책임:

- semantic entrypoint
- public API admission
- callback depth / deferred close
- send-ready sequencing
- monitor queue/thread
- endpoint bookkeeping
- async mailbox quiesce

구조 문제:

- semantic과 mechanism이 계속 같이 읽힌다.
- family-neutral facade보다 공통 runtime 허브에 더 가깝다.

hot-path 비용:

- 공통 socket 경로에 상태 조회와 조정 비용이 계속 모인다.
- callback, monitor, lifecycle 세부가 같은 중심 객체를 공유한다.

다음 단계 목표:

- semantic facade만 남기고 lifecycle, monitor, endpoint/runtime owner를 더 숨긴다.

#### A-2. `socket_message_api.cpp`

현재 책임:

- socket/service/stream/spot send/recv/publish/subscribe 경로
- 공통 검증과 handle fallback
- message helper와 route-specific 변형

구조 문제:

- 예전 `zlink.cpp` mega-hub가 줄어든 대신 message API 집선 허브가 커졌다.
- 도메인별 entry와 공통 helper가 한 파일에 과도하게 섞였다.

hot-path 비용:

- type 판별, fallback, 중복 검증이 공통 send/recv 경로에 누적된다.
- socket과 service 경계가 같은 분기 허브를 통과한다.

다음 단계 목표:

- 도메인별 message entry를 분리하되, 공통 검증은 얇은 helper로만 유지한다.

#### A-3. `ctx_t`

현재 책임:

- socket registry
- global socket removal wait
- inproc endpoint registry
- bootstrap 시작 조건
- termination/shutdown sequencing
- io thread selection
- pending inproc 연결 정리

구조 문제:

- registry owner이면서 startup/shutdown/resource coordinator 역할도 과도하게 가진다.
- `ctx_t` 설명이 여전히 너무 길다.

hot-path 비용:

- 소켓 생성/종료, endpoint 연결, resource orchestration 변경이 같은 중심 객체로 모인다.
- perf 회복 과정에서도 owner 판단이 `ctx_t`로 다시 재집중되기 쉽다.

다음 단계 목표:

- registry + termination contract owner 수준으로 줄이고 bootstrap/resource sequencing detail은 private owner로 밀어낸다.

### 8.3 B등급: service residual deep-module finish

#### B-1. `spot_data_plane`

현재 책임:

- semantic entry
- runtime socket wiring
- budget/hwm 정책
- polling loop
- control protocol과 forwarding owner 일부

구조 문제:

- helper 분리가 진행됐지만 data plane runtime과 policy가 완전히 분리되지는 않았다.

hot-path 비용:

- multi secure transport와 budget 조정 비용의 owner가 넓게 퍼질 위험이 있다.

다음 단계 목표:

- runtime owner와 budget/policy owner를 더 분명히 분리한다.

#### B-2. `registry/discovery`

현재 책임:

- state/rules
- query reply assembly
- socket ensure/replace
- observer/update 흐름

구조 문제:

- 서비스 facade와 topology/query/runtime owner가 완전히 분리되지 않았다.

hot-path 비용:

- discovery update/control 경로 조정 비용이 계속 같은 허브에 모인다.

다음 단계 목표:

- service facade는 유지하되 state, query, runtime owner를 문장 하나로 설명 가능한 수준까지 정리한다.

#### B-3. `spot_subject_access`

현재 책임:

- subject facade seam
- publish/recv/subscription/query/admission 일부 집선

구조 문제:

- seam은 생겼지만 subject-local owner 경계가 완전히 닫히지는 않았다.

hot-path 비용:

- publish/recv 쪽 owner가 access seam을 통해 다시 넓게 연결될 위험이 있다.

다음 단계 목표:

- access seam을 얇게 유지하고 subject-local runtime/state owner를 더 분명히 정리한다.

### 8.4 C등급: 현재 방향 유지, polish 중심

아래 항목은 현재 방향을 유지하고 재설계보다 polish를 우선한다.

- `service_runtime_base_t`
- `socket_close_ops_t`
- `ctx_bootstrap_t`
- `ctx_termination_t`
- `multipart_send_txn`

해석:

- 이들은 지금 기준에서 큰 구조 실패 지점이 아니다.
- 새 리팩토링은 되도록 이 경계를 다시 흔들지 않는다.

## 9. 현재 성능 개선을 마무리하기 위한 추가 작업 순서

이후 실행 순서는 아래처럼 고정한다.

1. `socket_base_t` residual split 재개
2. `socket_message_api.cpp` 구조 재편
3. `ctx_t` runtime orchestration 재정리
4. `spot` secure multi-peer current owner를
   `spot_data_plane_internal.hpp`, `spot_data_plane_runtime.cpp`,
   `spot_node_control.cpp` 경계에서 먼저 더 좁혀 마감
5. `spot_data_plane`, `registry`, `spot_subject_access` ownership 마감
6. `options_t` ownership 축을 "central bag이지만 owner는 분리됨" 수준에서 더 진행해,
   구조상 허브처럼 읽히지 않는 상태까지 마감
7. `spot_node_t`, `discovery_t` top-level facade를 재정리해
   coordinator가 과도한 세부 owner를 직접 쥐지 않게 정리
8. engine / transport 대형 구현 축 중 owner 설명이 약한 파일을 다시 읽고,
   실제 공통 구조 허브로 작동하는 파일만 선별해 정리
9. 구조가 안정화된 뒤 perf smoke, targeted recheck, full baseline 회복

추가 원칙:

- perf는 이미 진행 중인 작업의 최종 마감 단계다.
- 추가 구조 개선은 perf와 분리된 별도 트랙이 아니라,
  perf 최종 마감을 가능하게 만드는 수단이다.
- 구조 phase 중 baseline 미달은 기록하되, 구조 owner가 흔들리는 상태에서는 최종 perf 완료 판정을 하지 않는다.
- `core/perf`는 측정 surface로 유지하고, baseline 회복은 `core/` 내부 owner 수정으로 해결한다.
- 각 구조 단계는 끝난 뒤 "더 설명이 좋아졌다"만으로 닫지 않는다.
  hot-path 비용이 왜 줄었는지까지 설명 가능해야 한다.
- latest 실행 로그가 이미 current owner를 좁혀 줬다면,
  그 owner를 먼저 닫기 전까지 새로운 breadth-first 구조 항목으로 점프하지 않는다.

즉 이 순서는 "구조 작업 후 perf 시작"이 아니라,
"진행 중인 perf를 끝내기 위해 필요한 구조 개선을 current owner 순서대로 수행"하는 순서다.

## 10. 현재 perf 마감을 위해 함께 닫아야 하는 구조 범위

이번 문서는 현재 perf 회복 루프를 끝내는 과정에서
추가 구조 후보가 다시 계속 나오는 상황을 막기 위해,
기존에 후속 polish 후보로 남겨둘 수 있다고 본 축도
이번 구조 마감 범위에 포함하도록 해석을 올린다.

핵심 해석은 아래처럼 고정한다.

```text
이번 단계의 목표는 핵심 공통 허브만 줄이는 데서 멈추지 않고,
지금 코드 기준으로 이미 보이는 후속 구조 후보까지 함께 흡수해
다음 리뷰에서 다시 대형 구조 작업이 새로 열리지 않게 만드는 것이다.
```

### 10.1 이번 단계에서 최종으로 닫아야 하는 범위

아래 축은 이번 구조 마감 범위에 포함한다.

#### A. 공통 허브 최우선 범위

- `socket_base_t`
- `socket_message_api.cpp`
- `ctx_t`
- `spot_data_plane`
- `registry`
- `spot_subject_access`

#### B. 추가 포함 범위

- `options_t` ownership 축
- `spot_node_t`
- `discovery_t`
- engine / transport 대형 구현 축 중 구조 owner가 불명확한 파일

즉 아래 항목은 더 이상 "나중에 볼 polish 후보"로 미루지 않는다.

- `options_t`
- `spot_node_t`
- `discovery_t`
- `asio_engine.cpp`
- `asio_ws_engine.cpp`
- 기타 transport/engine large file 중 owner 설명이 약한 축

### 10.2 추가 포함 범위의 해석

#### A. `options_t` 축

- owner map은 이미 들어갔지만 central storage bag 성격이 여전히 남아 있다.
- 이번 단계에서는 "누가 해석하는가" 수준을 넘어서,
  구조상 왜 `options_t`가 더 이상 허브처럼 읽히지 않는지까지 마감 대상으로 본다.

#### B. service top-level facade 축

- `spot_node_t`
- `discovery_t`

이 둘은 이번 단계에서 즉시 리팩토링 대상으로 포함한다.
목표는 top-level facade가 남더라도,
coordinator가 너무 많은 세부 owner를 직접 쥐고 있지 않은 상태로 정리하는 것이다.

#### C. engine / transport 대형 구현 축

- `asio_engine.cpp`
- `asio_ws_engine.cpp`
- 일부 transport/engine large file

이 영역은 이전 문서에서는 차기 구조 후보로 미뤄둘 수 있었지만,
이번 문서에서는 "현재 코드 기준으로 이미 보이는 구조 허브"로 취급한다.
단, 단순히 파일이 크다는 이유만으로 작업을 열지 않고,
owner 설명이 약하거나 hot-path 구조와 강하게 연결된 경우에만 포함한다.

### 10.3 최종 구조 완료 판정 기준

이번 단계 완료 후 다시 리뷰했을 때 아래가 모두 참이면,
현재 단계의 구조는 사실상 완성으로 판정한다.

- 새로 발견되는 항목이 `socket_base_t` / `socket_message_api.cpp` / `ctx_t` 급의
  공통 허브 리팩토링이 아니다.
- `spot_data_plane`, `registry`, `spot_subject_access`,
  `spot_node_t`, `discovery_t`가 다시 핵심 residual로 승격되지 않는다.
- `options_t`가 central 허브로 다시 읽히지 않는다.
- engine/transport large file 이슈가 남더라도,
  그것이 새 구조 phase를 열 정도의 공통 owner 문제로 보이지 않는다.
- perf 회복 작업이 공통 구조 허브를 다시 열지 않고 국소 owner 수정으로 설명 가능하다.

이 판정이 성립하면 이후 단계는
"perf 최종 마감 + 국소 polish"로 해석한다.

여기서 `국소 polish`는 아래만 뜻한다.

- naming 정리
- 작은 응집도 개선
- 특정 transport 내부의 국소 구조 정리
- perf 회복 과정에서 드러나는 미세 owner 조정

아래는 이 판정 이후 새 구조 phase를 여는 근거로 사용하지 않는다.

- 단순 대형 파일 크기
- 이미 닫힌 허브를 다시 helper 수준으로 미세 분할하고 싶은 욕구
- 성능 수치를 이유로 구조 경계를 다시 크게 흔드는 시도
- 후속 기능 추가 없이 나타난 설명 수준의 개선 아이디어

즉 이번 범위까지 완료한 뒤 다시 리뷰했을 때
또 하나의 대형 POSD 리팩토링 phase가 필요하다는 결론이 나오면,
우선 그 결론이 실제 코드 상태 때문인지,
아니면 이번 문서의 범위 해석이나 실행이 덜 끝난 것인지부터 다시 확인한다.

## 11. 공개 인터페이스 정책

이 문서가 허용하는 변화는 private owner 이동과 private 파일 경계 재조정뿐이다.

- `core/include/zlink.h` 변경 없음
- `core/src/libzlink.vers` 변경 없음
- 새 public class/header 추가 없음
- `bindings` 수정은 이 문서 범위 밖

## 12. 테스트 및 완료 기준

구조 phase의 대표 검증군은 기존 residual 문서군의 gate를 재사용한다.

대표 검증 대상:

- `test_stream_socket`
- `test_stream_threadsafe`
- `test_stream_send_blocking_wakeup`
- `unittest_service_runtime_base`
- `test_service_introspection_discovery_self_close`
- `test_gateway_send_ready_self_close`
- `test_spot_service_introspection_handler_monitor_close`
- `unittest_spot_subject_access`
- `unittest_spot_data_plane_budget`
- `test_single_spot_benchmark_process`
- `test_multi_spot_benchmark_process`

공통 게이트:

- `./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 10`
- `./core/tests/run_thread_safe_contract_perf.sh --build-dir core/build --min-ratio 0.85`
- `./core/tests/run_test_lanes.sh --include-e2e`

추가 구조 범위 검증 원칙:

- `options_t` 축은 `unittest_typed_option`, stream/socket/service 대표 회귀로 검증한다.
- `spot_node_t`, `discovery_t`는 service introspection, monitoring, self-close 계열 회귀로 검증한다.
- engine/transport large file 정리는 관련 transport 대표 smoke와 perf tuple로 검증한다.
- A등급 구조 작업은 perf 회복 전에라도,
  해당 hot-path 비용 감소를 설명할 수 있는 targeted recheck 근거를 남긴다.
- current `spot/wss` owner 정리는
  latest targeted recheck와 exact tuple 비교 로그를 함께 증거로 남긴다.

완료 해석 규칙:

- 구조 개선 중간 단계의 개별 perf 관측은 참고 지표일 수 있지만,
  현재 문서 전체는 진행 중인 perf 회복 작업의 최종 완료까지 포함해 해석한다.
- 최종 완료는 구조 residual 닫힘, 대표 lane/stress 통과, baseline 비교 회복을 함께 만족해야 한다.
- 최종 완료 후 다시 리뷰했을 때 남는 항목은 원칙적으로 국소 polish 또는 perf 연계 미세 조정으로만 해석한다.
- 성능개선 없는 POSD 리팩토링은 이번 문서 기준으로 완료가 아니다.
- latest 실행 로그가 반복해서 같은 current owner를 가리키는 상태라면,
  그 owner가 닫히기 전까지는 구조 완료로 판정하지 않는다.

현재 단계의 직접 완료 조건:

- 진행 중인 perf 회복 작업이 추가 구조 개선 항목까지 흡수한 상태로 마감된다.
- latest active owner가 더 이상 반복해서 같은 secure `spot/wss` 경계를 가리키지 않는다.
- baseline exact tuple 비교에서 미달 항목이 남지 않는다.

## 13. 최종 평결

현재 `core`는 POSD 2차 리팩토링이 실패한 상태가 아니다.
이미 큰 진전이 들어갔고, 많은 기반도 실제 코드에 반영됐다.

다만 팀장님이 고정한 현재 기준,
즉 `성능을 포함한 올바른 구조`로 보면
`socket_base_t`, `socket_message_api.cpp`, `ctx_t`, service residual,
`options_t`, `spot_node_t`, `discovery_t`, engine/transport 구조 owner 축은 아직 정리 대상이다.

다만 현재 immediate active owner는
`spot` secure multi-peer `mesh_pub` hot path와 그 주변 budget/runtime/control 경계다.
따라서 breadth를 넓힌 작업 목록을 유지하더라도,
실행 우선순위는 latest log가 가리키는 current owner와 충돌하지 않게 운영해야 한다.

따라서 이후 목표는 리팩토링 여부를 다시 논쟁하는 것이 아니다.
이미 맞게 잡힌 방향을 `깊은 모듈 + 얇은 hot-path` 기준으로 끝까지 밀어,
구조와 성능을 같은 기준으로 설명 가능한 상태를 만드는 것이다.

또한 이번 단계의 의미는
현재 코드 기준으로 이미 보이는 후속 구조 후보까지
이번 작업 범위에 포함해 함께 닫는 데 있다.
따라서 `options_t`, `spot_node_t`, `discovery_t`, engine/transport 구조 owner 축은
이번 단계의 작업 목록으로 본다.

이 범위를 끝까지 완료하면,
그 이후 다시 나오는 항목은 원칙적으로
새로운 대형 POSD residual이 아니라 국소 polish 또는 perf 연계 미세 조정으로 판정한다.

핵심 마무리 문장:

```text
이후 작업의 목표는 리팩토링 여부를 다시 논쟁하는 것이 아니라,
남은 공통 허브를 얇은 hot-path와 깊은 owner 구조로 끝까지 밀어
POSD 판단과 성능 판단이 같은 문장으로 설명 가능한 상태를 만드는 것이다.
```
