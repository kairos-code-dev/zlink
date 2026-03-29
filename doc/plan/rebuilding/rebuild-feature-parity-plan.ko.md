# `2026-03-05` baseline 기반 zlink 재구축 Main Plan

## 1. 이 문서의 역할

- 이 문서는 `무엇을 만들고 있는지`, `왜 이 순서로 가는지`,
  `지금 무엇이 먼저인지`를 설명하는 main 문서다.
- 실행 규칙, 반복 순서, 종료 판정은
  `doc/plan/rebuilding/rebuild-feature-parity-ralph-guide.ko.md`를 따른다.
- 즉 이 문서는 목표/범위/우선순위를 설명하고,
  실제 Ralph 반복 실행은 실행 가이드가 담당한다.

## 2. 최종 목표

- 이 프로젝트는 단순 기능 포팅 프로젝트가 아니다.
- 이미 기능적으로는 거의 완성된 참고 구현
  `/home/hep7/project/kairos/zlink/core`
  가 존재한다.
- 하지만 그 구현은 single one-way 벤치에서 `libzmq` 대비 약 `30%`
  느린 구간이 남아 있다.
- 그래서 이 워크스페이스는 그 상태를 그대로 이어가는 대신,
  `2026-03-05`의 좋은 성능 anchor에서 다시 시작해
  기능 parity를 다시 쌓는 프로젝트다.
- 즉 성능이 유지되지 않으면 이 프로젝트는 존재 이유가 약해진다.
- 기능만 같고 one-way 성능이 다시 무너지면 이 재구축은 실패로 본다.
- `2026-03-05` 시점의 좋은 성능 baseline을 유지한 상태에서
  현재 문서
  - `/home/hep7/project/kairos/zlink/doc/api`
  - `/home/hep7/project/kairos/zlink/doc/guide`
  - `/home/hep7/project/kairos/zlink/doc/internals`
  에 정의된 기능과 인터페이스를
  이 워크스페이스의 `core/`에 다시 단계적으로 이식한다.
- 즉 목표는 단순 기능 추가가 아니라
  `기능/인터페이스 parity + 성능 유지`다.
- 그리고 최종 목표는 baseline 방어에서 멈추는 것이 아니다.
- baseline은 최소 통과선이고,
  최종적으로는 `libzmq` 대비 더 좋은 성능으로 마무리하는 것을 목표로 한다.
- 다만 그 향상은 기능 parity와 기준선 복구를 먼저 끝낸 뒤,
  구조를 무너지지 않게 유지하는 방향에서 달성해야 한다.
- 기능 의미 reference는
  `/home/hep7/project/kairos/zlink/core`
  를 참고한다.
- 다만 이 upstream `core`는 기능은 거의 완료되어 있어도
  single one-way 쪽에서 약 `30%` 느린 구간이 존재하는 참고 구현이다.
  반면 echo는 대체로 잘 나오는 편이다.
- 따라서 upstream `core`의 공개 기능/계약/테스트 범위는 따라가되,
  one-way hot path를 악화시키는 내부 구조나 비용까지 그대로 이식하지 않는다.
- 결론적으로 기능 완료 기준은
  `/home/hep7/project/kairos/zlink/core/tests`
  의 테스트 의미를 현재 워크스페이스 `core/tests`에 동등하게 반영하고
  최종적으로 모두 통과하는 것이다.
- 최종 public header는
  `/home/hep7/project/kairos/zlink/core/include/zlink.h`
  와 동일해야 한다.
- 즉 최종본에서 `core/include/zlink.h`는 upstream header와
  선언/타입/상수/옵션/콜백 시그니처 차이가 있으면 안 된다.

## 2.1 POSD 기반 설계 목표

- 이 재구축은 John Ousterhout의 POSD 원칙을 따른다.
- 다만 현재 단계에서는 `성능 유지`가 최우선 제약이고,
  POSD 적용도 hot path 비용을 늘리지 않는 범위에서만 허용한다.
- 즉 목표 구조는 아래 둘을 동시에 만족해야 한다.
  - 사용자에게는 더 단순한 인터페이스와 설명 가능한 계약
  - 내부 hot path에는 추가 branch/lock/materialization이 없는 구조

## 2.2 목표 구조

- public C API:
  - 얇은 façade로 유지한다.
  - handle 검사, 인자 검증, errno/public contract만 담당한다.
- 공통 socket 층:
  - 공통 lifecycle, bind/connect/close, command drain 같은
    generic 책임만 가진다.
  - pattern/service 의미를 과도하게 흡수하지 않는다.
- pattern/runtime 층:
  - `PAIR`, `PUBSUB`, `DEALER`, `ROUTER`, `STREAM` 등
    패턴별 송수신 의미와 envelope, multipart continuity를 직접 소유한다.
- core primitive 층:
  - `msg`, `pipe`, `session`, `mailbox`, `options` 같은
    invariant-bearing module이 실제 소유권과 경계를 가진다.
- service 층:
  - discovery/registry/spot/gateway 류는 sidecar/control 성격으로 두고,
    raw socket hot path를 오염시키지 않는다.

## 2.3 POSD 금지 방향

- 단순 공통화를 이유로 hot path wrapper/helper를 늘리지 않는다.
- 의미가 다른 계층을 `socket_base` 하나에 몰아넣지 않는다.
- 기능을 빨리 붙이려고 temporary branch/flag를 공통 path에 누적하지 않는다.
- callback/monitor/service 계약 때문에 steady-state send/recv 경로에
  추가 serialization을 넣지 않는다.
- multipart send/recv/callback/pub/subscribe 경로에서 part array를 매번
  heap allocation해서 materialize하지 않는다.
- 메시지 소유권은 그대로 전달하되, multipart part array container는
  내부 `thread_local` 재사용을 기본으로 설계한다.
- 구조를 예쁘게 맞추기 위해 성능 기준선을 깨는 refactor를 선행하지 않는다.

## 3. 작업 단계

- 이 프로젝트는 대략 `10단계`로 나누는 것이 맞다.
- 이유는 아래와 같다.
  - 성능 회귀가 나도 직전 단계와 최근 변경 범위를 빠르게 되짚을 수 있다.
  - 기능 parity와 구조 개선을 너무 큰 묶음으로 밀지 않게 된다.
  - 단계 종료 때마다 성능 green / POSD 정리 / commit 경계를 설명하기 쉽다.

### 3.1 1단계: 성능 debt 재측정과 anchor 고정

- baseline artifact를 고정하고 authority를 문서화한다.
- baseline 파일이 이미 고정돼 있으면 재시작 직후 full perf 재측정을 강제하지 않는다.
- baseline refresh가 필요한 경우에만
  `tcp/ipc/inproc` compare와
  single `ws/wss/tls`, `multi stream ws/wss/tls` baseline을 다시 갱신한다.

### 3.2 2단계: 성능 안정화

- 회귀가 실제로 발생했을 때만 성능 안정화 단계로 되돌아간다.
- 새 기능 추가 중 회귀가 보이면 현재 slice를 멈추고 이 단계로 돌아와
  최근 변경 검토, 원인 축소, 원복 가능한 최소 수정부터 수행한다.
- 회귀가 보이면 최근 변경 검토, 원인 축소, 원복 가능한 최소 수정부터 수행한다.
- 이 단계가 끝나기 전에는 기능 parity로 넘어가지 않는다.

### 3.3 3단계: 성능 green anchor commit

- 회귀 없는 상태를 성능 안정화 전용 commit/push로 먼저 고정한다.
- 이 commit이 이후 모든 기능 작업의 출발점이다.

### 3.4 4단계: 공통 public/core parity

- public C API와 공통 socket/core primitive 계약을 작은 slice로 맞춘다.
- send/recv, msg, option, lifecycle, callback 같은 공통 의미를 먼저 정리한다.
- 특히 multipart public API와 callback export는
  `message ownership transfer + thread_local array reuse`를 기본 계약으로 둔다.
- 이 단계부터
  `doc/plan/rebuilding/rebuild-feature-parity-matrix.ko.md`
  를 채우기 시작한다.
- 각 public symbol은 upstream `zlink.h`와 현재 `core/include/zlink.h`를
  1:1로 대응시키고 상태를 기록한다.

### 3.5 5단계: pattern parity

- `PAIR`, `PUBSUB`, `DEALER`, `ROUTER`, `STREAM` 의미를
  pattern별 micro-slice로 맞춘다.
- pattern별 data-plane/callback/routing contract를 공통층과 분리해서 올린다.
- 각 pattern slice는 upstream `core/tests` 대응 항목을 함께 matrix에 연결한다.
- 즉 구현만 끝내고 테스트 대응을 뒤로 미루지 않는다.

### 3.6 6단계: transport / TLS / option parity

- `tcp`, `ipc`, `inproc`, `ws`, `wss`, `tls` transport 의미와
  관련 option/TLS 계약을 맞춘다.
- transport별 차이를 핫패스 오염 없이 제한한다.

### 3.7 7단계: monitoring / events / snapshot parity

- monitor/event/snapshot 계약을 맞춘다.
- 기능은 맞추되 steady-state hot path 비용이 늘지 않게 유지한다.

### 3.8 8단계: thread-safe / lifecycle / callback parity

- thread-safe surface, lifecycle edge, callback 계약을 정리한다.
- 이 단계는 성능 민감도가 높으므로 특히 작은 slice로 나눈다.
- callback multipart dispatch는 이전 프로젝트에서 큰 병목을 만들었던
  `per-call array allocation`을 다시 도입하지 않는 것을 전제로 한다.

### 3.9 9단계: services parity

- discovery / registry / spot / gateway 같은 service 계층을 맞춘다.
- raw socket fast path와 service/control 계층을 분리한 채 parity를 올린다.

### 3.10 10단계: hardening / final perf / release-readiness

- upstream `zlink.h`와 현재 `core/include/zlink.h` 완전 일치 확인
- upstream `core/tests` parity matrix 전체 닫힘 확인
- 전체 `core/tests` 통과
- full single / full multi / zlink-only transport 성능 확인
- dead code / dead file 정리
- POSD 관점의 최종 구조 점검
- 최종 목표인 `libzmq` 대비 더 좋은 성능으로 마무리 가능한지 확인

### 3.11 단계 종료 POSD 리팩토링

- 각 micro-slice 또는 단계가 끝날 때마다
  `POSD 관점에서 더 단순한 구조로 정리할 부분이 있는지`를 반드시 검토한다.
- 다만 이 리팩토링은 항상 다음 조건 아래에서만 진행한다.
  - 현재 단계의 성능 기준이 이미 green이다.
  - 리팩토링 범위가 현재 단계 경계를 넘지 않는다.
  - hot path 비용을 늘리지 않는다.
- 즉 순서는 항상 아래다.
  1. 기능/성능 기준 충족
  2. 같은 단계 범위 안에서 POSD 리팩토링 검토
  3. 성능 재확인
  4. green이면 다음 단계 진행
- 성능 기준을 깨는 POSD 리팩토링은 좋은 리팩토링이 아니므로 보류한다.

### 3.12 성능 개선 운영 방식

- 성능 개선은 큰 리팩토링 한 번으로 해결하려고 하지 않는다.
- 후보를 작은 단위로 하나씩 적용하고,
  동일 조건 측정으로 keep/discard를 바로 판정하는 방식으로 진행한다.
- 즉 이 프로젝트의 성능 개선은
  `작은 후보 -> 동일 조건 비교 -> keep/discard` 반복을 기본 운영 방식으로 삼는다.
- 기능 추가도 같은 원칙을 따른다.
- 너무 큰 단위로 작업해서 성능 회귀 원인 분리가 어려워지는 진행은 금지한다.
- 성능 회귀가 발생하면 먼저 가장 최근 변경 범위를 빠르게 다시 검토하고,
  작은 수정 또는 최근 변경 축소만으로 성능 원복이 가능한지부터 본다.
- 즉 회귀가 났을 때는 새 기능을 더 얹는 것보다
  `최근 변경 검토 -> 원인 축소 -> 원복 가능한 최소 수정`
  순서를 우선한다.
- 성능 저하가 난 상태에서 새 성능 실험을 넓게 벌리는 것보다,
  먼저 기존 코드의 원복 또는 최근 변경 축소로 빠르게 기준선 복귀를 시도한다.
- 그 과정에서 실제로 성능에 민감했던 hot path를 식별하고 문서에 업데이트한다.
- 이후에는 그 hot path 주의사항을 유지한 상태에서만 기능 추가와 구조 개선을 진행한다.
- 특히 multipart send/recv/callback/pub/subscribe에서
  `part array는 thread_local 재사용`, `message ownership만 caller/callback으로 전달`
  규칙을 재발 방지 항목으로 고정한다.

## 4. 지금 당장 할 일

- 랄프루프를 다시 시작하면 첫 iteration은 아래만 한다.
  1. main 문서의 가장 앞 미완료 기능 단계를 고른다.
  2. 관련 문서 계약과 parity matrix 항목을 먼저 확인한다.
  3. 필요한 `core/tests`를 보강한 뒤 기능 slice 구현을 시작한다.
  4. slice 종료 시 focused perf로 회귀가 없는지 확인한다.
  5. 회귀가 생기면 그때만 2단계 성능 안정화로 되돌아간다.

## 5. 지금 하지 말아야 할 일

- 성능 green anchor가 생기기 전에는 아래를 current 작업으로 선택하지 않는다.
  - 새 public symbol 추가
  - spot/service/monitor/alias parity 진행
  - 기능 계약 확대를 위한 새 테스트 추가
  - 성능 debt와 직접 무관한 구조 정리
- 단계 경계 밖까지 번지는 대규모 POSD 리팩토링
- upstream `core`의 one-way 저하 원인을 그대로 가져오는 구조 복사

## 6. 성능 기준

- `tcp/ipc/inproc`
  - rebuilding single compare baseline
- `ws/wss/tls`
  - zlink-only baseline
  - 기준 파일은
    `doc/plan/perf/feature-port-from-20260305-baseline.ko.md`
    에 고정된 single / multi zlink-only baseline을 따른다.
- 허용오차:
  - `baseline 대비 -5%`까지 허용
  - `-5% ~ -10%`는 warning
  - `-10% 이하`는 block
- 위 기준은 현재 iteration을 통제하기 위한 guardrail이다.
- 최종 제품 목표는 이 guardrail에 겨우 맞추는 것이 아니라,
  가능한 범위에서 `libzmq`보다 더 좋은 성능으로 마무리하는 것이다.

## 7. 다음 단계 전환 조건

- 아래가 모두 만족돼야 다음 기능 단계로 넘어간다.
  - `tcp/ipc/inproc` single compare 전체에서 warning/block 회귀 없음
  - `ws/wss/tls` zlink-only 전체에서 warning/block 회귀 없음
  - `multi compare`와 `multi stream ws/wss/tls`에서도 warning/block 회귀 없음
  - 현재 단계 범위의 parity matrix 항목이 모두 갱신돼 있음
  - 현재 단계 범위 안에서 필요한 POSD 리뷰/정리가 끝남
  - 성능 안정화 변경만 따로 설명 가능한 green commit 경계가 있음

## 8. 문서 관계

- main 문서:
  - `doc/plan/rebuilding/rebuild-feature-parity-plan.ko.md`
- 실행 가이드:
  - `doc/plan/rebuilding/rebuild-feature-parity-ralph-guide.ko.md`
- hot-path 주의 문서:
  - `doc/plan/rebuilding/rebuild-feature-parity-hotpath.ko.md`
- feature parity matrix:
  - `doc/plan/rebuilding/rebuild-feature-parity-matrix.ko.md`
- baseline 기준:
  - `doc/plan/perf/feature-port-from-20260305-baseline.ko.md`
  - single `ws/wss/tls`와 `multi stream ws/wss/tls`도 위 baseline 문서의
    결과 파일을 기준으로 사용한다.
- POSD / 구조 참고:
  - `/home/hep7/project/kairos/zlink/doc/internals/posd-module-structure.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/internals/architecture.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/internals/design-decisions.ko.md`
- 기능/테스트 참고:
  - `/home/hep7/project/kairos/zlink/core`
  - `/home/hep7/project/kairos/zlink/core/tests`
