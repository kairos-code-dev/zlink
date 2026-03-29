# `2026-03-05` baseline 기반 zlink 재구축 Ralph 실행 가이드

## 1. 문서 역할

- 이 문서는 현재 워크트리의 `zlink 재구축 작업`을 반복 실행하기 위한
  실행 authority 문서다.
- main 문서는
  `doc/plan/rebuilding/rebuild-feature-parity-plan.ko.md`
  하나만 유지한다.
- main 문서는 `무엇을 만들고 있는지`, `현재 우선순위`, `다음 단계 전제`를 설명한다.
- 이 실행 가이드는 `반복 순서`, `검증`, `종료 판정`을 담당한다.
- 별도 master/gap/residual/보조 계획 문서를 새로 만들지 않는다.
- 이 문서 안에 과거 iteration 메모나 historical backlog가 남아 있더라도
  상단 규칙, main 문서, baseline 문서보다 우선하지 않는다.
- 가이드 하단의 과거 상태표/백로그/증거는 `historical record only`다.
  현재 iteration의 current/next 작업 선택 근거로 직접 사용하지 않는다.

## 1.1 실행 진입점

- Ralph loop 재시작은 아래 래퍼를 기준으로 한다.

```bash
./doc/plan/rebuilding/run_rebuild_feature_parity_ralphloop.sh
```

- 이 래퍼는 현재 저장소의 실행 가이드와 로그 디렉터리를 고정한다.
- 기존 supervisor 확인/정리는 기본으로 강제하지 않는다.
  필요할 때만 `--check-existing-supervisor`를 명시한다.

## 1.2 main 문서

- 작업의 본체 설명과 현재 우선순위는 아래 main 문서를 먼저 본다.

```text
doc/plan/rebuilding/rebuild-feature-parity-plan.ko.md
```

- Ralph loop는 이 main 문서의 목표를 전제로,
  현재 실행 가이드의 반복 규칙대로 동작한다.

## 1.3 main 단계 연결 규칙

- 현재 작업 단계의 authority는 항상 main 문서의 `3. 작업 단계`다.
- 이 가이드의 `Phase`는 실행 편의를 위한 운영 단위일 뿐이고,
  main 문서의 단계보다 우선하지 않는다.
- 따라서 Ralph loop는 아래 순서로 현재 작업 위치를 판정한다.
  1. main 문서에서 현재 미완료 `단계(stage)`를 먼저 고른다.
  2. 이 가이드의 대응 `Phase`와 `micro-slice`를 그 단계 안에서 고른다.
  3. 현재 slice가 그 단계 목표와 직접 연결되지 않으면 current에서 제외한다.
- 즉 `어느 단계를 하고 있는가`는 main 문서가 정하고,
  `그 단계를 어떻게 잘게 나눠 반복할 것인가`는 이 가이드가 정한다.

## 2. 목적과 완료 조건

- 이 프로젝트는 이미 기능적으로 거의 완성된 upstream `core`가 있음에도
  single one-way 성능이 `libzmq` 대비 약 `30%` 느린 문제 때문에
  다시 시작한 재구축 프로젝트다.
- 따라서 성능 유지에 실패하면 이 프로젝트의 의미도 크게 줄어든다.
- 기능 parity만 맞추고 one-way 성능이 다시 무너지면 완료로 인정하지 않는다.
- 현재 워크스페이스의 `2026-03-05` good baseline 성능을 유지한다.
- `/home/hep7/project/kairos/zlink/doc/api`,
  `/home/hep7/project/kairos/zlink/doc/guide`,
  `/home/hep7/project/kairos/zlink/doc/internals`에 정의된 기능과
  인터페이스를 이 워크스페이스의 `core/`에서 제공한다.
- 기존 시스템 의미와 동등한 `core/tests` 검증을 추가/유지하고 모두 통과한다.
- 최종 `core/include/zlink.h`는
  `/home/hep7/project/kairos/zlink/core/include/zlink.h`
  와 동일해야 한다.
- 최종 `core/tests`는
  `/home/hep7/project/kairos/zlink/core/tests`
  의미를 모두 구현하고 통과해야 한다.
- `core/bench/with_zmq`로 baseline 대비 핵심 성능 guardrail을 유지한다.
- 현재 iteration 운영은 guardrail 기준으로 통제하지만,
  최종 목표는 가능한 범위에서 `libzmq`보다 더 좋은 성능으로 마무리하는 것이다.
- 위 조건이 모두 충족되고 아래 작업 레지스터에 미완료 항목이 없을 때만
  루프를 종료한다.

## 2.1 현재 시작 조건

- single/multi baseline artifact는 이미 고정돼 있다.
- 따라서 Ralph loop 재시작 직후 full perf 재측정을 먼저 강제하지 않는다.
- 기본 시작점은 `main 문서의 가장 앞 미완료 기능 단계`다.
- 성능 회귀가 실제로 발생했을 때만 현재 slice를 멈추고
  `성능 안정화 slice`로 되돌아간다.
- 성능 판정 기준은 아래 baseline을 사용한다.
  - single compare:
    `doc/plan/rebuilding/baseline/perf_linux_20260329_200606.txt`
  - single zlink-only:
    `doc/plan/rebuilding/baseline/perf_linux_20260329_155434.txt`
  - multi compare:
    `doc/plan/rebuilding/baseline/perf_linux_20260329_162318_multi_baseline_20260329.txt`
  - multi stream `ws/wss/tls`:
    `doc/plan/rebuilding/baseline/perf_linux_20260329_163829_multi_stream_ws_wss_tls_baseline_20260329_rerun.txt`
- 허용오차범위는 각 기준 대비 `-5%`까지다.
- `-5% ~ -10%` 하락은 warning이다.
- `-10% 이하` 하락은 block이다.
- 위 순서와 충돌하는 하단 historical backlog/phase 메모가 있으면
  항상 이 시작 조건 섹션과 main 문서를 우선한다.

## 3. 상위 규칙

- `/home/hep7/project/kairos/zlink/AGENTS.md`를 최우선으로 따른다.
- 사용자를 항상 `팀장님`으로 부른다.
- build/test/bench는 오직 `core/build/`만 사용한다.
- 문서가 1차 truth다.
- `/home/hep7/project/kairos/zlink/core/include`,
  `/home/hep7/project/kairos/zlink/core/src`는 `semantic reference only`다.
- `/home/hep7/project/kairos/zlink/core`는 기능 범위 reference로 본다.
  다만 single one-way 성능이 약 `30%` 느린 참고 구현이므로,
  echo가 잘 나온다는 이유만으로 내부 hot path 구조를 그대로 이식하지 않는다.
- main 쪽 현재 구현은 기능 의미 참고만 허용한다.
- main 코드의 구조를 hot path에 자동 이식하지 않는다.
- `성능 > 구조 미학`이다.
- POSD는 따르되, hot path에 새 wrapper, 새 lock, 새 branch,
  새 materialization 비용을 넣는 방식이면 적용하지 않는다.
- 기능 계약은 항상 `core/tests`에 먼저 고정한다.
- `core/tests` 통과가 먼저고, `with_zmq`는 그 다음 2차 verification이다.
- 사용자 명시 요청이 없으면 `core/perf/`나 `core/bench/`를 수정하지 않는다.
- bench/perf 결과만으로 기능 구현을 완료로 간주하지 않는다.
- fail-fast 원칙을 따른다.
  - retry loop 금지
  - sleep 기반 동기화 금지
  - 첫 실패 즉시 surface 노출

## 4. 범위와 기준 문서

### 4.1 문서 기준

- API 기준
  - `/home/hep7/project/kairos/zlink/doc/api/README.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/api/context.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/api/socket.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/api/message.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/api/monitoring.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/api/polling.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/api/discovery.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/api/spot.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/api/registry.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/api/utilities.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/api/events.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/api/errors.ko.md`
- Guide 기준
  - `/home/hep7/project/kairos/zlink/doc/guide/01-overview.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/guide/02-core-api.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/guide/03-0-socket-patterns.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/guide/03-*`
  - `/home/hep7/project/kairos/zlink/doc/guide/04-transports.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/guide/05-tls-security.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/guide/06-monitoring.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/guide/07-*`
  - `/home/hep7/project/kairos/zlink/doc/guide/08-routing-id.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/guide/09-message-api.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/guide/10-performance.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/guide/11-thread-safety.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/guide/12-socket-options.ko.md`
- Internals 기준
  - `/home/hep7/project/kairos/zlink/doc/internals/architecture.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/internals/design-decisions.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/internals/posd-module-structure.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/internals/thread-safety.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/internals/threading-model.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/internals/multipart-atomicity.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/internals/protocol-zmp.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/internals/protocol-raw.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/internals/services-internals.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/internals/socket-option-defaults.ko.md`

### 4.2 코드 기준

- 구현 대상: 현재 워크스페이스의 `core/`
- 기능 참고: `/home/hep7/project/kairos/zlink/core/include`,
  `/home/hep7/project/kairos/zlink/core/src`
- 테스트 기준: `/home/hep7/project/kairos/zlink/core/tests`
- 최종 기능 완료 기준은 upstream `core/tests`의 의미를 현재 워크스페이스
  `core/tests`에 동등하게 반영하고 모두 통과시키는 것이다.
- header/API 완료 기준:
  `/home/hep7/project/kairos/zlink/core/include/zlink.h`
  와 현재 `core/include/zlink.h` 완전 일치
- 성능 기준: `doc/plan/perf/feature-port-from-20260305-baseline.ko.md`
- 성능 guardrail surface: `core/bench/with_zmq`
- parity matrix:
  `doc/plan/rebuilding/rebuild-feature-parity-matrix.ko.md`

## 5. baseline과 성능 판정

### 5.1 baseline anchor

- 기준 커밋: `7bea9e3f6af1542d0f216397e729b8d9f529372d`
- 현재 작업 브랜치: `wip/feature-port-from-20260305`
- baseline 보고서:
  - `doc/plan/perf/feature-port-from-20260305-baseline.ko.md`
  - raw report artifact는 retention으로 현재 워크트리에 남아 있지 않다.
  - baseline 수치와 당시 실행 명령은 위 perf 문서의 요약값을 기준으로 유지한다.

### 5.2 핵심 판정 셀

- `PAIR 64B tcp`
- `PAIR 64B inproc`
- `DEALER_DEALER 64B tcp`
- `DEALER_DEALER 64B inproc`

- 위 4셀은 `빠른 경보용 핵심 셀`이다.
- 즉 이 4셀은 가장 먼저 보는 smoke surface일 뿐,
  성능 안정화 완료나 green anchor 판정을 단독으로 대신하지 않는다.
- 실제 성능 green 판정은 아래 추적 대상 전체를 기준으로 한다.
  - single compare:
    `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`
    x `tcp`, `ipc`, `inproc`
  - single zlink-only:
    `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`
    x `ws`, `wss`, `tls`
  - 필요 시 multi compare까지 포함한 full gate

### 5.3 판정 규칙

- 먼저 가장 단순한 기준은 아래다.
  - `tcp/ipc/inproc`는 `libzmq compare`와
    `2026-03-05 zlink baseline` 기준으로 본다.
  - `ws/wss/tls`는 baseline 문서에 고정된 `zlink-only baseline` 기준으로 본다.
  - `multi stream ws/wss/tls`도 baseline 문서에 고정된 별도
    zlink-only baseline 기준으로 본다.
  - 허용오차범위는 각 기준 대비 `-5%`까지다.
  - `-5% ~ -10%` 하락은 warning이다.
  - `-10% 이하` 하락은 block이다.
  - 이 기준은 핵심 4셀뿐 아니라 추적 대상 전체 pattern/transport에 적용한다.
  - 즉 어느 기준이든 허용오차범위를 넘겨 떨어진 셀이 하나라도 남아 있으면
    성능 안정화는 미완료다.
- 그 위에서 iteration 운영 규칙은 아래를 따른다.
- slice 기본 확인은 `runs=1` smoke로 시작한다.
- 아래 중 하나면 즉시 같은 조건으로 총 3회까지 재측정한다.
  - 핵심 4셀 중 하나라도 baseline 대비 `-5%` 이하
  - 확장 패턴에서 구조적 하락이 보임
- 3회 재측정 후 median으로 판정한다.
- block 조건:
  - 핵심 4셀 중 하나라도 median `-10%` 이하
  - 핵심 4셀 중 2개 이상 median `-7%` 이하
  - 팀장님 승인 없는 hot path 구조 변경 + 성능 하락 동반
- warning 조건:
  - 핵심 4셀 중 하락이 `-5% ~ -10%` 사이지만 block은 아님
- warning이면 원인 메모를 작업 레지스터에 남기고 다음 slice로 넘어갈지
  판단한다.
- 성능 하락이 관측돼도 매번 즉시 rollback하지는 않는다.
- 현재 slice의 계약 경계가 명확하고, 성능 원인이 그 slice 내부로 충분히 좁혀져 있으면
  같은 slice 안에서 짧은 개선 루프를 허용한다.
- 다만 기능 완료 판정은 단계 완료가 아니라 parity matrix 전체 닫힘으로 본다.
- 즉 upstream `zlink.h`와 `core/tests` 매핑이 비어 있으면 다음 큰 완료 판정은 금지한다.
- 성능 안정화가 미완료면 새 기능 micro-slice로 넘어가지 않는다.
- 성능 안정화가 미완료면 기능 backlog를 조사하거나 triage하는 것도
  다음 current slice의 기본 선택지가 아니다.
- 성능 안정화 완료 판정 전에는 최소한
  - 핵심 4셀 median이 baseline band 안
  - `tcp/ipc/inproc` compare 전체에서 warning/block 회귀 없음
  - `ws/wss/tls` zlink-only 전체에서 warning/block 회귀 없음
  - `multi stream ws/wss/tls` zlink-only에서 warning/block 회귀 없음
  이 모두 남아 있어야 한다.
- 위 조건을 만족하는 `성능 안정화 전용 green anchor commit`이 생기기 전까지는
  어떤 기능 parity 진척도도 다음 current slice의 근거가 될 수 없다.
- 기능 slice가 green이더라도 다음 단계로 바로 넘기지 말고,
  현재 단계 범위 안에서 POSD 리팩토링 필요 여부를 한 번 더 검토한다.
- 다만 POSD 리팩토링은 항상 `성능 > 구조` 원칙 아래에서만 허용한다.
- 다만 아래 중 하나면 `rollback 또는 slice 재분해`를 강제 검토한다.
  - 같은 slice에서 focused perf debt가 3 iteration 이상 계속 남는다
  - dirty 변경이 둘 이상 기능 묶음으로 섞여 원인 분리가 흐려진다
  - 한 slice에서 unrelated API/monitoring/pattern/service 변경이 함께 쌓인다
  - commit/push 가능한 green 경계를 더 이상 설명하기 어렵다

### 5.4 slice 크기 규칙

- 기본 단위는 `phase`가 아니라 `micro-slice`다.
- 한 micro-slice는 가능한 한 아래 중 하나만 다룬다.
  - public symbol 1묶음
  - callback contract 1개
  - socket semantics 1개
  - service fanout/event contract 1개
  - test file 1개와 직접 연결된 core change 1묶음
- 한 micro-slice 안에 아래를 동시에 섞지 않는다.
  - public API parity
  - monitoring/service monitor parity
  - socket pattern semantics
  - transport/TLS semantics
  - service surface unification
- 각 micro-slice는 아래 순서를 따른다.
  1. 문서 계약 1개를 고른다.
  2. `core/tests`에 그 계약을 먼저 고정한다.
  3. `core/` 최소 수정으로 통과시킨다.
  4. focused `with_zmq`로 확인한다.
  5. green이면 즉시 commit/push한다.
- 성능 개선이 짧게 끝나지 않으면 더 큰 slice로 버티지 말고,
  현재 변경을 더 작은 micro-slice 둘 이상으로 다시 나눈다.
- 전체 micro-slice master 목록을 처음부터 고정하지는 않는다.
- 큰 단위 변경으로 여러 기능/경로를 한 번에 섞는 진행은 피한다.
- 성능 회귀가 보이면 먼저 방금 들어간 변경 묶음을 다시 읽고,
  최근 수정 코드에서 원인을 빠르게 축소할 수 있어야 한다.
- 회귀가 난 상태에서 큰 다음 단계로 밀어붙이지 않는다.
- 성능 회귀가 난 상태에서 새 성능 실험 후보를 넓게 추가하지 않는다.
- 우선 최근 변경의 원복 또는 slice 축소만으로 기준선 복귀가 가능한지부터 본다.
- 기준선 복귀 과정에서 실제 hot path를 식별하면 그 내용을 관련 문서에 즉시 업데이트한다.
- hot path 문서화가 끝난 뒤 그 주의사항을 유지하면서 다음 기능 추가/구조 개선으로 넘어간다.
- 우선순위는 항상 아래다.
  1. 최근 변경 검토
  2. 회귀 원인 축소
  3. 원복 가능한 최소 수정 또는 slice 축소
  4. 동일 조건 재측정
  5. hot path 식별/문서 업데이트
  6. green이면 다음 단계 진행

### 5.5 성능 개선 실험 규칙

- 성능 개선 후보는 한 iteration에 가능하면 1개만 다룬다.
- 한 iteration에서 여러 hot path 후보를 동시에 섞지 않는다.
- 다만 기준선 복귀가 먼저 필요한 상황에서는
  새 후보 실험보다 최근 변경 원복/축소를 우선한다.
- 각 후보는 아래 순서를 고정한다.
  1. 변경 전 동일 조건 측정
  2. 후보 1개 적용
  3. 변경 후 동일 조건 측정
  4. keep/discard 판정
- 비교 조건은 항상 동일하게 유지한다.
  - 같은 surface
  - 같은 pattern
  - 같은 transport
  - 같은 msg size
  - 같은 runs
- 기본 budget은 아래를 따른다.
  - 1차: focused smoke `runs=1`
  - 이상 징후나 keep 후보면: 같은 조건 `runs=3` 재측정 후 median 판정
  - broad-impact 후보면: full single 또는 필요한 확장 gate까지 같은 iteration 안에 확인
- keep 규칙:
  - 허용오차 밖 회귀를 만들지 않는다.
  - broad win 또는 명확한 targeted win이 확인된다.
  - 테스트/계약 의미를 악화시키지 않는다.
- discard 규칙:
  - broad win이 없고 노이즈 수준이면 버린다.
  - 일부 셀 개선 대신 다른 핵심 셀을 악화시키면 버린다.
  - 원인 설명이 불가능한 애매한 변화면 버린다.
- 성능 개선 iteration의 산출물은 반드시 남긴다.
  - 실행 명령
  - 결과 파일
  - before/after 핵심 수치
  - keep/discard 결론
- multipart send/recv/callback/pub/subscribe 경로에서
  part array를 매번 새로 할당하는 구현은 기본적으로 금지한다.
- 이 경로의 기본 원칙은
  `message ownership transfer`와 `thread_local part-array reuse`다.
- upstream `/home/hep7/project/kairos/zlink/core`에서 이미 적용된 동일 방향을
  reference로 삼되, 현재 tree에서는 같은 실수를 반복하지 않도록 hot-path 문서에
  재발 방지 항목으로 유지한다.
- 실제 효과가 있었던 변경만 hot-path 문서에 남긴다.
- 기준선 복귀에 직접 기여한 원복/축소도 hot-path 문서에 남긴다.
- 효과가 없었던 후보는 iteration 로그에 남기고 같은 후보를 반복 시도하지 않는다.
- 대신 작업 레지스터의 rolling micro-slice queue만 현재 truth로 유지한다.
- 즉 전체 단계는 phase/macro-step으로 고정하고,
  실제 micro-slice 목록은 작업하면서 계속 재분해/재정렬한다.

### 5.5 micro-slice 성능 확인 규칙

- 모든 micro-slice는 끝날 때마다 `짧은 focused perf 확인`을 기본으로 수행한다.
- full gate까지 기다렸다가 성능 문제를 한꺼번에 발견하는 방식은 금지한다.
- 기본 확인은 핵심 4셀 smoke다.
  - `PAIR 64B tcp`
  - `PAIR 64B inproc`
  - `DEALER_DEALER 64B tcp`
  - `DEALER_DEALER 64B inproc`
- 기능 계약이 green이어도, 이 짧은 perf 확인 전에는 다음 micro-slice로 넘어가지 않는다.
- 기본은 항상 핵심 4셀 smoke를 먼저 돈다.
- 회귀 복구 중인 iteration에서는 `확장 focused perf`도 매 iteration 의무다.
- 즉 회귀 복구 중인 iteration에서는 quick smoke만 보고 green 판정하지 않는다.
- 핵심 4셀이 green이어도 확장 패턴이나 다른 transport에서 회귀가 있으면
  그 iteration은 green이 아니다.
- 다만 아래처럼 영향 범위가 넓은 변경은 같은 iteration 안에서
  `확장 focused perf` 또는 `full verification`까지 바로 수행해야 한다.
  - public send/recv ABI
  - multipart array export / callback dispatch container
  - canonical multipart API
  - callback/poller contract 전반
  - monitor/event fanout 공통 경로
  - unified service surface
  - transport/option/lifecycle 공통 경로
  - hot path 공통 helper/shared path
- 즉 `quick smoke`는 기본 진입점이고, 작업 특성상 전체 영향이 의심되면
  full/expanded verification은 선택이 아니라 의무다.
- 성능 문제는 `초기에 작게 발견하고 작게 해결`하는 것이 원칙이다.
- focused perf debt가 보이면 그 micro-slice 범위 안에서 먼저 원인을 좁히고,
  오래 끌 조짐이 보이면 즉시 더 작은 micro-slice로 재분해한다.

## 6. 반복 실행 순서

각 iteration은 아래 순서를 반드시 지킨다.

1. 이 가이드 전체와 현재 작업 레지스터를 다시 읽는다.
2. `git status`, 현재 branch, 최근 commit을 확인한다.
3. main 문서의 가장 앞 미완료 `단계(stage)`를 먼저 고른다.
   그 다음 이 가이드의 대응 `phase`와 그 안의 가장 작은 `micro-slice`를 고른다.
4. 현재 slice가 현재 단계 목표와 직접 연결되는지 먼저 판정한다.
   - 아니면 current slice에서 제외하고 `parked` 또는 `next`로 내린다.
6. 관련 문서 항목에서 계약을 추출한다.
7. `core/tests`에 기능 계약을 먼저 추가하거나 보강한다.
8. `core/` 코드만 최소 범위로 수정한다.
9. `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`이 필요하면 갱신하고,
   `cmake --build core/build -j$(nproc)`로 빌드한다.
10. 관련 `core/tests` surface를 먼저 통과시킨다.
11. 먼저 핵심 4셀 focused `with_zmq` smoke를 짧게 돌린다.
12. 변경 영향 범위가 넓으면
    같은 iteration 안에서 확장 focused perf 또는 full verification까지 바로 수행한다.
13. 필요하면 3회 재측정과 원인 정리를 수행한다.
14. 결과를 이 가이드의 작업 레지스터와 진행 메모에 갱신한다.
    - 회귀가 발생했다면 핵심 4셀과 확장 패턴 결과를 둘 다 남긴다.
    - 현재 slice가 겨냥한 residual 셀/패턴에 어떤 변화가 있었는지 한 줄로 적는다.
15. slice가 격리되고 green이면 즉시 commit/push한다.
    - 회귀 복구 slice라면 `성능 안정화 전용 commit/push`가 먼저다.
16. phase의 마지막 미완료 slice가 끝났으면 phase 종료 POSD review checkpoint를 수행한다.
    - 현재 단계 범위 안에서만 구조 단순화 여지가 있는지 본다.
    - hot path 비용이 늘거나 단계 경계를 넘으면 보류한다.
    - POSD 정리를 수행했다면 같은 단계 안에서 성능을 다시 확인한다.
17. phase 완료 commit/push를 확인한다.
18. 미완료 항목이 남으면 다음 iteration으로 넘어간다.

### 6.1 성능 debt 처리 규칙

- focused perf가 깨졌다고 즉시 rollback하지 않는다.
- 먼저 아래를 수행한다.
  1. 원인이 현재 micro-slice 하나로 충분히 좁혀졌는지 확인
  2. 같은 micro-slice 안에서 가능한 작은 수정으로 1~2 iteration 개선 시도
  3. 개선 후에도 경계가 깨끗하면 계속 진행
- 성능 debt 확인은 가능한 한 같은 세션 안에서 바로 한다.
- perf debt를 “나중에 한 번에 보자”는 식으로 다음 여러 micro-slice로 넘기지 않는다.
- 회귀 복구 중인 iteration에서는 기능 parity triage나 신규 구현으로 attention을 분산하지 않는다.
- 성능 debt와 직접 연결되지 않는 spot/service/monitor/API 작업은
  모두 `next-after-green`으로 미룬다.
- 회귀 복구 iteration이 특정 residual 셀/패턴에 대한
  관측/수정/재측정을 끝내지 못했으면 그 iteration은 미완료다.
- 다른 transport/pattern에서 새 warning/block이 생기면
  그 iteration은 기존 대상 셀 개선 여부와 무관하게 미완료다.
- 핵심 4셀 smoke가 warning 이상이면, 다음 기능 micro-slice로 진행하기 전에
  최소한 원인 메모와 처리 방향이 작업 레지스터에 있어야 한다.
- 변경 영향 범위가 넓은데 quick smoke만 보고 넘어가는 것은 금지한다.
- 그런 경우에는 같은 iteration 안에서 확장 focused perf 또는 full verification까지 수행한 뒤
  다음 micro-slice 진행 여부를 판단한다.
- 아래 중 하나면 rollback 또는 re-slice로 전환한다.
  - 2 iteration 이상 개선했는데도 핵심 debt가 같은 수준으로 남는다
  - perf debt 원인이 현재 micro-slice 밖 변경과 섞여 보인다
  - 현재 dirty tree를 green commit 경계로 설명할 수 없다
- rollback의 목적은 “실패 숨기기”가 아니라 “원인 분리”다.
- rollback이 필요하면 마지막 green commit으로 되돌아가기보다,
  가능하면 현재 dirty tree에서 unrelated micro-slice를 먼저 분리/폐기하고
  필요한 최소 변경만 다시 적용하는 순서를 우선 검토한다.

### 6.2 restart / re-entry 규칙

- Ralph loop를 중단했다가 다시 시작할 때, worktree가 dirty면
  첫 iteration의 목표는 새 기능 추가가 아니라 `dirty tree 재분해`다.
- 회귀 복구가 끝나지 않은 상태에서 재시작하면
  첫 iteration의 목표는 `회귀 복구 재진입`이다.
- 즉 재시작 후에도 회귀가 남아 있으면
  새 기능 slice를 더 얹지 않고 같은 복구 구간을 이어서 수행한다.
- 재시작 후 current slice가 현재 회귀 또는 현재 단계 목표와 직접 연결되지 않으면
  그 slice는 잘못 고른 것이다. 즉시 `parked`로 내리고 다시 고른다.
- 재시작 첫 iteration은 아래 순서를 우선 수행한다.
  1. 현재 dirty 변경 파일을 기능 묶음별로 inventory한다.
  2. `current micro-slice / next / parked`로 다시 분류한다.
  3. `현재 회귀 복구나 현재 단계 완료에 직접 기여하지 않는 항목`은 전부 `next-after-green`으로 보낸다.
  4. green 경계가 없는 변경은 `re-slice candidate`로 표시한다.
  5. 성능 debt와 기능 debt가 섞여 있으면 분리 방향을 먼저 문서에 기록한다.
  6. 그 다음에만 가장 작은 current micro-slice의 구현/검증으로 들어간다.
- 즉 재시작 직후에는 기존 dirty tree 위에 새 기능을 바로 더 얹지 않는다.
- 아래 중 하나면 다음 iteration 전체를 `stabilization / re-slice only`로 사용한다.
  - 3 iteration 연속 commit/push 없음
  - broad-impact 변경이 둘 이상 섞여 있음
  - current dirty tree를 green micro-slice 경계로 설명할 수 없음
  - focused perf debt와 기능 parity debt가 같은 변경 묶음에 얽혀 있음

## 7. 금지 규칙

- top-level `build/` 디렉터리 사용 금지
- perf/bench helper 수정으로 core 버그를 가리는 행위 금지
- ad-hoc repro 프로그램이나 `/tmp` 실험으로 정당화 금지
- sleep/retry 기반 test 추가 금지
- unrelated 변경 commit/push 금지
- 문서 contract보다 main 구현을 우선하는 판단 금지
- main 쪽 later 리팩터링에서 생긴 분리 파일명을 현재 워크트리에 자동 투영하지 않는다
- 현재 워크트리에 없는 later split 파일을 가정해서 작업을 시작하지 않는다
- 이미 대체된 dead code, stale helper, unused file을 계속 누적한 채 phase를 넘기지 않는다
- hot path 공통화만을 위한 wrapper/helper 추가 금지
- `socket_base`에 의미를 몰아넣는 얕은 구조 확장 금지
- 한 slice에서 기능, 대규모 구조 변경, 광범위 리팩터링을 한 번에 섞지 않는다
- 회귀 복구 중인 iteration에서 spot/service/monitor/alias parity 같은
  기능 backlog를 current slice로 선택하는 것 금지

## 8. 검증 절차

### 8.1 기본 configure/build

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON
cmake --build core/build -j$(nproc)
```

### 8.2 기능 검증 우선 순서

관련 변경에 맞춰 아래 중 필요한 surface를 먼저 돌린다.

```bash
ctest --test-dir core/build --output-on-failure -L unittest -j$(nproc)
ctest --test-dir core/build --output-on-failure -L integration -j1
ctest --test-dir core/build --output-on-failure -L e2e -j1
ctest --test-dir core/build --output-on-failure -L regression -j1
./core/tests/run_test_lanes.sh
./core/tests/run_test_lanes.sh --include-e2e
./core/tests/run_test_lanes.sh --include-e2e --include-regression
```

### 8.3 thread-safe 계약 검증

Phase 6 또는 lifecycle/callback/thread-safe 관련 slice는 아래를 사용한다.

```bash
./core/tests/run_thread_safe_contract_stress.sh --count 10 --build-dir core/build
./core/tests/run_thread_safe_contract_perf.sh --min-ratio 0.85 --build-dir core/build
```

### 8.3 최종 conformance 검증

최종 완료 또는 Phase 8 종료 직전에는 아래를 추가로 확인한다.

```bash
diff -u /home/hep7/project/kairos/zlink/core/include/zlink.h core/include/zlink.h
./core/tests/run_test_lanes.sh --include-e2e --include-regression
./core/tests/run_thread_safe_contract_stress.sh --count 10 --build-dir core/build
./core/tests/run_thread_safe_contract_perf.sh --min-ratio 0.85 --build-dir core/build
```

- `diff` 출력이 비어 있어야 `zlink.h` parity가 닫힌다.
- 위 검증이 모두 green이고 parity matrix에 미완료 행이 없어야
  최종 완료로 본다.

장시간 stress gate를 백그라운드로 감시해야 하면 공유 gate 스크립트를 사용한다.

```bash
./core/tools/ralphloop/run_execution_gate_loop.sh \
  --build-dir core/build \
  --logs-dir <session gate dir> \
  --label rebuild_feature_parity \
  --count 10
```

### 8.4 focused with_zmq guardrail

Ralph loop 재시작 직후 current perf debt anchor를 다시 잡을 때는
아래 full single 측정을 먼저 실행한다.

```bash
./core/bench/with_zmq/run_benchmarks.sh \
  --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER \
  --msg-sizes 64,256,1024,65536,131072,262144 \
  --transports tcp,ipc,inproc \
  --runs 1 \
  --reuse-build
```

`ws/wss/tls`는 zlink-only baseline 기준으로 전 패턴을 별도로 확인한다.

```bash
./core/bench/with_zmq/run_benchmarks.sh \
  --zlink-only \
  --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER \
  --msg-sizes 64,256,1024,65536,131072,262144 \
  --transports ws,wss,tls \
  --runs 1 \
  --reuse-build
```

위 결과들 중 warning/block이 하나라도 있으면 다음 current slice는
반드시 그 residual을 직접 줄이는 성능 안정화 slice여야 한다.

핵심 4셀 smoke는 아래를 기준으로 사용한다.

```bash
BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py \
  --patterns PAIR,DEALER_DEALER \
  --msg-sizes 64 \
  --transport tcp,inproc \
  --runs 1 \
  --build-dir core/build
```

`zlink`만 ws/wss/tls를 확인할 때는 아래를 사용한다. (libzmq는 미지원이므로 zlink-only 실행.)

```bash
BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py \
  --patterns PAIR,DEALER_DEALER \
  --msg-sizes 64 \
  --transport ws,wss,tls \
  --runs 1 \
  --zlink-only \
  --build-dir core/build
```

확장 패턴 확인이 필요하면 아래를 사용한다.

```bash
BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py \
  --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER \
  --msg-sizes 64 \
  --transport tcp,ipc,inproc \
  --runs 1 \
  --build-dir core/build
```

run_benchmarks 쉘 래퍼를 쓰는 경우에는 `--zlink-only` 플래그를 추가해
`ws,wss,tls`를 포함시킨 동일 조건을 실행한다.

```bash
./core/bench/with_zmq/run_benchmarks.sh --zlink-only \
  --patterns PAIR,DEALER_DEALER \
  --msg-sizes 64 \
  --transports ws,wss,tls \
  --runs 1 \
  --reuse-build \
  --output /tmp/with_zmq_ws_zlink_only.txt
```

### 8.5 최종 full gate

모든 phase 완료 직전과 완료 판정 직전에 아래를 수행한다.

```bash
ctest --test-dir core/build --output-on-failure -L unittest -j$(nproc)
ctest --test-dir core/build --output-on-failure -L integration -j1
ctest --test-dir core/build --output-on-failure -L e2e -j1
ctest --test-dir core/build --output-on-failure -L regression -j1
./core/tests/run_test_lanes.sh --include-e2e --include-regression
./core/tests/run_thread_safe_contract_stress.sh --count 10 --build-dir core/build
./core/tests/run_thread_safe_contract_perf.sh --min-ratio 0.85 --build-dir core/build
BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py \
  --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER \
  --msg-sizes 64,256,1024,65536,131072,262144 \
  --transport tcp,ipc,inproc \
  --runs 1 \
  --build-dir core/build
BENCH_NO_AUTOBUILD=1 \
BENCH_TRANSPORTS=tcp,ipc \
BENCH_MSG_SIZES=64,256,1024,65536,131072,262144 \
python3 core/bench/with_zmq/multi/run_comparison.py \
  --runs 1 \
  --build-dir core/build
```

## 9. main 단계와 phase 연결

아래 표를 기준으로, Ralph loop는 항상 main 문서의 현재 단계에서만
slice를 고른다.

| Main 단계 | 의미 | Guide phase | current slice 선택 규칙 |
| --- | --- | --- | --- |
| 1단계 | 성능 debt 재측정과 anchor 고정 | Phase 0 | 측정/비교/anchor 기록만 수행 |
| 2단계 | 성능 안정화 | Phase 1 일부 | 성능 debt를 직접 줄이는 slice만 허용 |
| 3단계 | 성능 green anchor commit | Phase 1 일부 | 성능 안정화 전용 commit/push만 허용 |
| 4단계 | 공통 public/core parity | Phase 2 | public API, 공통 core, header parity, matrix 갱신 |
| 5단계 | pattern parity | Phase 3 | pattern별 의미와 대응 test/matrix를 함께 갱신 |
| 6단계 | transport/TLS/option parity | Phase 4 | transport, TLS, option 계약만 다룸 |
| 7단계 | monitoring/events/snapshot parity | Phase 5 | monitor/event/snapshot 계약만 다룸 |
| 8단계 | thread-safe/lifecycle/callback parity | Phase 6 | thread-safe, lifecycle, callback 계약만 다룸 |
| 9단계 | services parity | Phase 7 | discovery/registry/spot/gateway 계층만 다룸 |
| 10단계 | hardening/final perf/release-readiness | Phase 8 | 전체 matrix 닫힘, 전체 tests, full perf, dead code 정리 |

- phase를 고를 때는 항상 위 표의 같은 행 안에서만 고른다.
- 예를 들어 main 문서가 6단계면, guide에서 Phase 4 slice만 current가 될 수 있다.
- 다른 phase의 slice는 `next` 또는 `parked`로 내린다.

## 10. phase별 적용 순서

### Phase 0. 기준선과 작업 프레임 고정

- baseline anchor 재확인
- 작업 레지스터와 parity inventory 유지
- slice별 기록 형식 고정

### Phase 1. Hot Path 보호형 코어 골격 정리

- 시작 기준은 현재 워크트리의 실제 파일 배치다.
  - `core/src/sockets/socket_base.cpp`
  - `core/src/sockets/socket_base.hpp`
  - `core/src/core/*`
  - `core/src/services/*`
- later main 쪽에 있을 수 있는 split file 이름을 먼저 찾지 말고,
  현재 파일에서 ownership/invariant를 추출한 뒤 필요한 최소 분리만 검토한다.
- send/recv fast path ownership
- multipart atomicity
- routing-id attach/detach 규칙
- thread-safety tier
- fast path forbidden list

#### Phase 1 책임표

| 영역 | 현재 파일/모듈 | 책임 | hot path 규칙 |
| --- | --- | --- | --- |
| Public C API 진입 | `core/src/api/*` | handle 검사, 인자 검증, public errno 계약, thin façade 유지 | send/recv steady-state 비용을 늘리는 새 wrapper/branch 추가 금지 |
| 공통 socket façade | `core/src/sockets/socket_base.*` | command drain, generic send/recv/bind/connect/close, endpoint bookkeeping, monitor fanout 진입 | pattern/service 의미를 흡수하지 않는다. `xsend/xrecv` 호출 전후 최소 비용만 허용 |
| Socket semantic/runtime | `core/src/sockets/pair.*`, `pub.*`, `sub.*`, `dealer.*`, `router.*`, `stream.*`, `xpub.*`, `xsub.*` | pattern별 송수신 의미, routing envelope, multipart 경계 유지 | 공통화를 위해 추가 indirection/lock 도입 금지. pattern별 thin path 우선 |
| Core runtime primitives | `core/src/core/*` | `msg_t`, `pipe_t`, `session_base_t`, `mailbox_t`, `options_t`, thread/object ownership | queue/message/session invariant 소유. service 전용 의미 주입 금지 |
| Service orchestration | `core/src/services/*` | discovery/registry/spot runtime, topology/metadata/control orchestration | raw socket fast path를 건드리지 않고 sidecar/control 계층에 머문다 |

#### Phase 1 코드 고정 계약

- send fast path ownership
  - `socket_base_t::send()`는 context/lifecycle 확인, command drain, public flag 정규화만 담당한다.
  - 실제 steady-state 송신 의미와 queue 선택은 socket family의 `xsend()`가 소유한다.
- recv fast path ownership
  - `socket_base_t::recv()`는 command throttling과 public flag 추출만 담당한다.
  - 실제 message 선택, routing envelope 처리, multipart continuity는 socket family의 `xrecv()`와 하위 `fq/lb/dist/pipe` 계층이 소유한다.
- multipart atomicity
  - multipart 경계 유지 책임은 `socket_base`가 아니라 `core/src/core/pipe.*`, `core/src/sockets/lb.*`, `core/src/sockets/dist.*`, `core/src/sockets/fq.*`에 둔다.
  - public façade는 반쪽 multipart를 관찰 가능하게 만드는 임시 버퍼링/재조립 wrapper를 추가하지 않는다.
- routing-id attach/detach 규칙
  - 소켓 own routing id 기본값 생성 위치는 `socket_base_t` constructor다.
  - peer routing-id attach/detach와 pipe별 상태 전파는 connect/session/socket-family 계층이 담당한다.
  - service/helper는 routing-id override를 할 수 있어도 기본 생성 ownership을 가져가면 안 된다.
- thread-safety tier
  - hot path admission은 public socket/service handle 경계에서만 최소화해 처리한다.
  - control/lifecycle strictness는 close/control/callback registration 계층에서 강제하고, steady-state send/recv에 추가 serialization을 넣지 않는다.

#### Phase 1 fast path forbidden list

- `socket_base_t::send()` / `recv()` common path에 service-specific branch 추가 금지
- monitor/service/runtime callback 등록 상태 때문에 steady-state send/recv에 새 lock 추가 금지
- routing-id/peer snapshot 조회를 위해 steady-state path에 heap allocation/materialization 추가 금지
- option parsing/default 해석을 `xsend()` / `xrecv()` 내부 steady-state path로 재유입 금지
- hot path 공통화만을 위한 새 wrapper/helper/virtual hop 추가 금지
- later main branch의 split 구조를 맞추기 위한 파일 분리 선행 금지

### Phase 2. Core Public API parity

- context/socket/message/errors/polling/utilities
- routing-id
- message lifecycle
- option ownership and defaults
- thread-safe send/control/lifecycle tier contract 기초

#### Phase 2 초기 symbol inventory 메모

- 문서 기준 1차 gap으로 확인된 항목
  - `zlink_msg_refcnt()` 문서화됨, 현재 워크트리 public header/implementation 미반영
- 문서/코드 불일치 후보
  - monitoring open API는 guide 예제가 options 구조체 기반으로 서술되지만 현재 헤더/구현은 `int events` 인자 기반이다.
  - `zlink_monitor_close()`, `zlink_monitor_snapshot()`, recv/callback 모델 전환 API, service monitor API는 문서화돼 있으나 현재 public header/implementation에 없다.
  - 이 항목은 별도 slice로 계약 확정 후 수정한다.

### Phase 3. Socket pattern parity

순서는 아래를 고정한다.

1. `PAIR`
2. `PUBSUB`
3. `DEALER`
4. `ROUTER`
5. `STREAM`
6. `PROXY`

### Phase 4. Transport / TLS / socket-option parity

- tcp/ipc/inproc
- ws/wss/tls
- reconnect/timeout/heartbeat/keepalive
- wildcard endpoint / last endpoint

### Phase 5. Monitoring / Events / Snapshot parity

- monitor callback mode
- event flags and aliases
- disconnect reason / handshake failure detail
- snapshot/detail mask

### Phase 6. Thread-safety / lifecycle / callback parity

- hot path guaranteed
- control path serialized
- lifecycle strict
- callback register/unregister/close race

### Phase 7. Service layer parity

순서는 아래를 고정한다.

1. `Registry`
2. `Discovery`
3. `Socket-family discovery integration`
4. `SPOT Node`
5. `Unified SPOT`

### Phase 8. Full conformance / hardening / release-readiness

- 문서 parity checklist 100%
- 전체 lane 통과
- single/multi full 통과
- known limitation / unsupported matrix 정리
- migration notes 정리

## 11. commit / push 운영 규칙

- commit은 micro-slice 단위로 한다.
- 한 commit은 하나의 문서 계약 묶음과 그에 필요한 코드/테스트만 담는다.
- green slice는 local 변경 상태로 오래 쌓아두지 말고, 검증이 끝난 즉시 commit/push한다.
- phase를 넘길 때는 직전 phase 상태가 원격까지 push된 상태여야 한다.
- 즉 `다음 phase 시작 전 = 이전 phase 완료 commit/push 완료`를 기본 규칙으로 한다.
- phase 내부에서도 micro-slice 경계가 분명하면 micro-slice별 commit/push를 우선한다.
- 한 phase가 너무 커서 micro-slice가 여러 개인 경우에도, phase 끝까지 모아서 한 번에 push하지 않는다.
- commit 전에 최소한 아래를 만족해야 한다.
  - 관련 `core/tests` 통과
  - focused `with_zmq` guardrail 통과 또는 warning 기록 완료
  - 작업 레지스터 갱신 완료
- unrelated 변경은 commit에 포함하지 않는다.
- push는 green slice가 쌓일 때마다 바로 수행한다.
- phase 완료 시에는 commit hash, push 여부, 핵심 검증 명령을 작업 레지스터에 반드시 남긴다.
- push한 commit hash와 핵심 검증 명령은 작업 레지스터에 남긴다.

## 12. phase 종료 POSD review checkpoint

각 phase의 마지막 slice가 끝날 때마다, 다음 phase로 넘어가기 전에 아래 POSD review를
반드시 한 번 수행한다.

- 목적은 `성능을 유지하면서 complexity를 줄일 수 있는 작은 구조 정리`만
  opportunistic하게 반영하는 것이다.
- 별도 POSD 전용 phase를 만들지 않는다.
- 기능 parity와 성능 guardrail이 우선이고, POSD review는 phase 종료 게이트의 일부다.

### 12.1 review 질문

- 이번 phase에서 생긴 새 wrapper/helper가 hot path 호출 수를 늘렸는가
- ownership/invariant를 더 깊은 모듈 하나로 숨길 수 있는가
- public facade는 얇고 내부 모듈은 더 깊게 만들 수 있는가
- temporal decomposition이나 change amplification이 새로 생겼는가
- service/control/lifecycle 경계로 밀어낼 수 있는 slow-path 의미가 hot path에 남아 있는가
- 이번 phase 구현으로 완전히 대체된 dead code, stale branch, unused helper, unused header, obsolete file이 남았는가

### 12.2 keep 조건

- 관련 `core/tests`가 모두 green이다.
- focused `with_zmq` guardrail이 pass이거나 최소 warning 범위 안이다.
- hot path에 새 lock, 새 branch, 새 materialization 비용이 늘지 않는다.
- phase 범위를 넘어서는 대규모 리팩터링으로 번지지 않는다.
- commit 경계를 흐리지 않고 현재 phase 결과물에 포함 가능한 작은 정리다.
- dead code/file 삭제가 현재 phase 계약과 직접 연결되고, unrelated 변경과 섞이지 않는다.

### 12.3 defer 조건

아래 중 하나면 이번 phase에서는 POSD 정리를 적용하지 않고 `design debt`로 남긴다.

- 핵심 4셀에서 warning 이상 하락이 있다.
- perf debt 원인이 아직 분리되지 않았다.
- 정리가 hot path 구조 변경을 요구한다.
- 다음 phase 범위까지 건드려야 의미가 생긴다.
- unrelated 변경과 섞여 안전한 commit/push 경계를 만들기 어렵다.
- 파일 삭제가 실제 참조 관계를 아직 확정하지 못한 상태다.

### 12.4 phase 종료 기록 규칙

작업 레지스터에는 아래를 남긴다.

- `POSD review: keep 없음` 또는 `POSD review: keep N건 / defer N건`
- keep한 구조 정리의 한 줄 요약
- defer한 항목의 한 줄 요약과 이유
- defer 항목이 있으면 다음 phase backlog 또는 design debt 메모에 연결
- dead code / dead file 정리를 수행했으면 삭제한 파일과 제거한 stale path를 함께 남긴다.

## 13. 작업 레지스터 갱신 규칙

각 iteration 끝에서 아래를 현재 상태에 맞게 갱신한다.

- 현재 macro-step
- 현재 phase / 현재 slice
- 현재 micro-slice 이름
- 다음 micro-slice 3개 이내
- 보류 micro-slice 3개 이내
- 관련 문서 항목
- 변경 대상 파일
- 추가/보강한 `core/tests`
- 실행한 검증 명령
- 성능 측정 명령과 결과 파일
- 핵심 4셀 대비 판정
- 성능 코멘트
- POSD review 결과
- dead code / dead file 정리 결과
- commit / push 여부
- 다음 iteration의 첫 작업

## 14. 작업 레지스터 아카이브

이 장 전체는 과거 iteration 기록 보존용이다.

- current/next 작업을 여기서 직접 뽑지 않는다.
- 현재 작업 선택은 항상
  1. main 문서
  2. 이 가이드 상단 규칙
  3. baseline 문서
  순서로 판단한다.
- 아래 표/메모는 historical evidence일 뿐 현재 authority가 아니다.

### 14.1 현재 기준 상태

- branch: `wip/feature-port-from-20260305`
- baseline anchor: `7bea9e3f6af1542d0f216397e729b8d9f529372d`
- baseline status: `fixed`
- with_zmq port status: `single/multi runnable`
- benchmark output shape status: `aligned to current branch policy`

### 14.2 macro-step 진행 현황

| Macro-step | 연결 phase | 상태 | 최근 성능 판정 | 코멘트 |
| --- | --- | --- | --- | --- |
| M0 baseline/frame | Phase 0 | completed | baseline fixed | `2026-03-05` 기준선과 with_zmq 프레임 고정 완료 |
| M1 core invariant | Phase 1 | completed | no new debt noted | 현재 트리 기준 ownership/invariant 정리 완료 |
| M2 public/core parity | Phase 2 | in_progress | quick smoke에서 inproc/tcp debt 혼재 | public socket/message/monitoring/service parity를 micro-slice로 계속 분해 중 |
| M3 pattern parity | Phase 3 | pending | gated by upper parity | `PAIR` 일부 계약은 선반영됐지만 상위 public parity가 먼저 |
| M4 transport/tls/option | Phase 4 | pending | not started | transport/TLS/option parity 대기 |
| M5 monitoring/events | Phase 5 | pending | not started | monitoring/events/snapshot parity 대기 |
| M6 thread-safe/lifecycle | Phase 6 | pending | not started | thread-safe/lifecycle/callback parity 대기 |
| M7 services | Phase 7 | pending | not started | registry/discovery/spot unified services parity 대기 |
| M8 hardening/release | Phase 8 | pending | not started | full gate / hardening / release-readiness 대기 |

### 14.3 phase 상태표

| Phase | 상태 | 비고 |
| --- | --- | --- |
| Phase 0 | completed | baseline, with_zmq baseline, planning frame 확보 |
| Phase 1 | completed | `socket_base` send/recv ownership, multipart/routing-id/thread-safety 경계가 현재 코드와 일치함을 재확인 |
| Phase 2 | in_progress | `zlink_msg_refcnt()`, socket/service monitor callback-only 전환, unified close/snapshot, raw `zlink_recv_handler()`, raw `SUB/XSUB zlink_subscribe_handler()`, raw socket `zlink_send_ready_handler()`, current split spot-family alias(`spot_pub`/`spot_node`) send-ready 최소 parity는 반영했다. 이번 iteration에서 split `spot_pub_t`/`spot_sub_t` 위에 borrowed-node facade `zlink_spot_new/destroy`, `zlink_spot_publish`, `zlink_spot_subscribe`, `zlink_spot_subscribe_pattern`, `zlink_spot_unsubscribe`, `zlink_spot_recv`, `zlink_spot_setsockopt`를 실제 코드/test에 올렸다. canonical generic pub/sub API, unified `zlink_subscribe_handler()`, unified poller/send-ready/monitor, ctx-owning `zlink_spot_new()`는 아직 미완료다 |
| Phase 3 | pending | `PAIR`의 단일-peer 교체, `inproc` bind-before-connect 실패 규칙은 test/code에 반영했지만 상위 public socket callback/data-plane parity가 먼저 정리되지 않아 pattern phase 완료로 닫지 않는다 |
| Phase 4 | pending | transport/TLS/option parity 미완료 |
| Phase 5 | pending | monitoring/events/snapshot parity 미완료 |
| Phase 6 | pending | thread-safe/lifecycle/callback parity 미완료 |
| Phase 7 | pending | services parity 미완료 |
| Phase 8 | pending | full conformance / hardening / release-readiness 미완료 |
| Phase 8 | pending | full hardening/release-readiness 미완료 |

### 14.4 rolling micro-slice queue

아래 13.4~13.6 블록은 과거 실행 흔적 보존용이다.
현재 iteration의 current/next 선택 근거로 직접 사용하지 않는다.
현재 작업 선택은 항상 아래 순서를 따른다.

1. main 문서의 현재 목표/우선순위
2. 이 실행 가이드 상단의 성능 선행 규칙과 판정 규칙
3. baseline 문서의 고정 기준
4. 그 다음에만 historical note 참고

- current macro-step: `M2 public/core parity`
- current phase: `Phase 2`
- current micro-slice:
  - unified `spot` 최소 data-plane parity 실제 반영 및 test surface 연결
- next micro-slice:
  1. unified `spot` service monitor / send-ready / poller gap inventory
  2. canonical `zlink_publish()` ABI inventory와 최소 계약 test 고정
  3. canonical `zlink_subscribe()` ABI inventory와 최소 계약 test 고정
- parked micro-slice:
  1. `zlink_send_rid()` ABI inventory
  2. `PAIR` remaining parity inventory
  3. perf debt 원인 분리 전 broad-impact alias cleanup

### 14.5 현재 우선순위 백로그

1. unified `spot` source-rid / poller / send-ready / monitor / option/routing-id parity inventory
2. unified `spot` service monitor / send-ready / poller parity
3. canonical pub/sub `zlink_publish()` ABI inventory
4. `zlink_send_rid()` ABI inventory
5. 상위 public socket parity가 정리된 뒤 `PAIR` 문서 계약도 항목별 micro-slice로 다시 inventory
6. focused with_zmq smoke에서 남는 `PAIR 64B inproc`, `DEALER_DEALER 64B inproc` debt는 기능 micro-slice와 분리 추적

### 14.6 최근 검증 증거

- baseline report:
  - `doc/plan/perf/feature-port-from-20260305-baseline.ko.md`
  - baseline raw report artifact는 retention으로 현재 워크트리에서 제거됨
- latest full single:
  - `core/bench/with_zmq/results/single/report/perf_linux_20260328_220114.txt`
- latest full multi:
  - `core/bench/with_zmq/results/multi/report/perf_linux_20260328_214440_header_policy_full_multi_20260328.txt`
- current iteration message API verification:
  - `ctest --test-dir core/build --output-on-failure -R "test_msg_refcnt|test_msg_flags|test_msg_ffn"` -> pass
- current iteration monitoring/message verification:
  - `ctest --test-dir core/build --output-on-failure -R "test_msg_refcnt|test_socket_null|test_monitor_enhanced|test_stream_routing_id_size|test_stream_socket"` -> pass
- current iteration service monitor verification:
  - `ctest --test-dir core/build --output-on-failure -R "test_service_monitor|test_socket_null|test_monitor_enhanced|test_msg_refcnt|test_stream_routing_id_size|test_stream_socket"` -> pass
- current iteration raw socket recv-handler verification:
  - `ctest --test-dir core/build --output-on-failure -R test_socket_recv_handler` -> pass
  - `ctest --test-dir core/build --output-on-failure -R "test_pair_inproc|test_router_multiple_dealers|test_stream_socket|test_socket_null"` -> pass
- current iteration quick perf rule:
  - every micro-slice closes with `PAIR/DEALER_DEALER 64B tcp/inproc` focused smoke first
  - broad-impact slice must add expanded or full perf verification in the same iteration
- current iteration canonical multipart ABI target:
  - `ctest --test-dir core/build --output-on-failure -R "test_public_inproc_multipart_send|test_socket_recv_handler|test_msg_refcnt"` -> pass
  - `ctest --test-dir core/build --output-on-failure -R "test_xpub_nodrop|test_hwm_pubsub|test_pair_inproc|test_router_multiple_dealers"` -> pass
  - `BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build` -> `PAIR 64B tcp -5.37%`, `PAIR 64B inproc -20.84%`, `DEALER_DEALER 64B tcp -2.61%`, `DEALER_DEALER 64B inproc -4.15%`
  - `BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 2 --build-dir core/build` -> 재측정. `PAIR 64B tcp +16.00%`, `PAIR 64B inproc -9.51%`, `DEALER_DEALER 64B tcp +7.05%`, `DEALER_DEALER 64B inproc -17.08%`
  - perf note: `PAIR 64B inproc`, `DEALER_DEALER 64B inproc` debt는 여전히 block/warning 후보이며, 현재 iteration에서는 기능 ABI 전환만 반영하고 원인 분리는 다음 slice와 분리 추적
- current iteration raw socket subscribe-handler verification:
  - `ctest --test-dir core/build --output-on-failure -R "test_socket_subscribe_handler|test_socket_recv_handler|test_pubsub_filter_xpub|test_socket_null"` -> pass
  - `ctest --test-dir core/build --output-on-failure -R "test_stream_socket|test_pair_inproc|test_router_multiple_dealers"` -> pass
- current iteration raw socket send-ready verification:
  - `cmake --build core/build -j$(nproc) --target test_socket_send_ready_handler` -> pass
  - `ctest --test-dir core/build --output-on-failure -R "test_socket_send_ready_handler|test_socket_recv_handler|test_socket_subscribe_handler|test_socket_null|test_pair_inproc"` -> pass
- current iteration service send-ready verification:
  - `cmake --build core/build -j$(nproc) --target test_spot_send_ready_handler` -> pass
  - `ctest --test-dir core/build --output-on-failure -R "test_spot_send_ready_handler|test_socket_send_ready_handler|test_service_monitor|test_spot_pubsub_scenario|test_spot_send_blocking_wakeup"` -> pass
- current iteration unified spot verification:
  - `cmake --build core/build -j$(nproc) --target test_spot_unified_basic` -> pass
  - `ctest --test-dir core/build --output-on-failure -R test_spot_unified_basic` -> pass
  - `ctest --test-dir core/build --output-on-failure -R "test_spot_unified_basic|test_spot_send_blocking_wakeup"` -> pass
  - `ctest --test-dir core/build --output-on-failure -R test_spot_pubsub_scenario` -> pass
- current public socket API inventory:
  - `core/include/zlink.h` / `core/src/api/zlink.cpp` 기준으로 public header/API는 아직 legacy buffer형 `zlink_send()/zlink_recv()`를 유지한다
  - 이번 iteration에서 현재 트리 기준 최소 unified `spot` wrapper `zlink_spot_new/destroy`, `zlink_spot_publish`, `zlink_spot_subscribe`, `zlink_spot_subscribe_pattern`, `zlink_spot_unsubscribe`, `zlink_spot_recv`, `zlink_spot_setsockopt`를 public header/implementation/test에 실제 반영했다
  - 위 wrapper는 기존 `spot_node_t` + split `spot_pub_t`/`spot_sub_t`를 재사용하는 borrowed-node facade다
  - canonical generic `zlink_publish()/zlink_subscribe()`, unified `zlink_subscribe_handler()`, unified poller/send-ready/service-monitor parity, ctx-owning `zlink_spot_new()`는 아직 미반영이다
- current iteration focused with_zmq smoke:
  - `core/bench/with_zmq/results/single/report/perf_linux_20260329_075432.txt`
  - `core/bench/with_zmq/results/single/report/perf_linux_20260329_075517.txt`
  - `core/bench/with_zmq/results/single/report/perf_linux_20260329_080827.txt`
  - `core/bench/with_zmq/results/single/report/perf_linux_20260329_081847.txt`
  - `core/bench/with_zmq/results/single/report/perf_linux_20260329_082219.txt`
  - `core/bench/with_zmq/results/single/report/perf_linux_20260329_082941.txt`
  - `core/bench/with_zmq/results/single/report/perf_linux_20260329_083025.txt`
  - `core/bench/with_zmq/results/single/report/perf_linux_20260329_083106.txt`
  - `core/bench/with_zmq/results/single/report/perf_linux_20260329_083820.txt`
  - `core/bench/with_zmq/results/single/report/perf_linux_20260329_084233.txt`
  - `core/bench/with_zmq/results/single/report/perf_linux_20260329_092926.txt`
  - `core/bench/with_zmq/results/single/report/perf_linux_20260329_095717.txt`

### 14.7 현재 iteration 시작점

- current macro-step: `M2 public/core parity`
- current phase: `Phase 2`
- current slice: unified `spot` 최소 data-plane slice. 현재 트리의 split `spot_pub_t`/`spot_sub_t` 위에 borrowed-node facade `zlink_spot_*`를 얹어 local pub/sub, pattern unsubscribe, setsockopt 계약을 test/code로 고정
- current micro-slice: unified `spot` 최소 data-plane parity
- next blocking checks:
  - `doc/api/spot.ko.md`, `doc/guide/02-core-api.ko.md` 기준 unified `spot` source-rid / poller / send-ready / monitor / option/routing-id gap을 먼저 inventory한다
  - canonical 멀티파트 `zlink_send()/zlink_recv()` ABI 교체 계약은 현재 legacy buffer ABI와 충돌하므로 별도 inventory로 남긴다
  - `doc/guide/03-1-pair.ko.md`의 `recv/poller-only` 서술은 상위 socket/core-api 문서보다 하위 예시 문맥으로 취급하고, canonical public surface는 상위 문서의 raw socket callback 지원으로 판정한다

### 14.8 성능 기록 / 코멘트

- baseline reference:
  - source:
    - `doc/plan/perf/feature-port-from-20260305-baseline.ko.md`
  - comment:
    - 성능의 기준은 `libzmq 절대 동률`이 아니라 `2026-03-05 baseline 유지`다.
- latest quick smoke:
  - result file:
    - `core/bench/with_zmq/results/single/report/perf_linux_20260329_204103.txt`
  - key cells:
    - `PAIR 64B tcp`: `zlink 3921.26 Kmsg/s`, `libzmq no_data_rc_127`
    - `PAIR 64B inproc`: `zlink 3554.04 Kmsg/s`, `libzmq no_data_rc_127`
    - `DEALER_DEALER 64B tcp`: `zlink 3863.11 Kmsg/s`, `libzmq no_data_rc_127`
    - `DEALER_DEALER 64B inproc`: `zlink 4414.59 Kmsg/s`, `libzmq no_data_rc_127`
  - comment:
    - 현재 환경에서는 compare용 `libzmq` 실행이 빠져 quick smoke가 diff 판정까지 닫히지 않았다.
    - 이번 slice는 `spot` public facade 국소 변경이며, compare 환경 복구 전에는 기존 inproc debt를 새 결과와 직접 비교 판정하지 않는다.
- logging rule:
  - 모든 micro-slice는 최소 1개의 quick perf 기록과 1줄 코멘트를 남긴다.
  - broad-impact micro-slice는 expanded/full perf 기록과 코멘트를 같은 iteration에 추가한다.

### 14.9 현재 iteration 진행 메모

- 관련 문서 항목
  - `doc/api/README.ko.md`
  - `doc/guide/02-core-api.ko.md`
  - `doc/api/message.ko.md`의 `zlink_msg_refcnt()`
  - `doc/api/monitoring.ko.md`
  - `doc/api/spot.ko.md`
  - `doc/api/discovery.ko.md`
  - `doc/guide/11-thread-safety.ko.md`
  - `doc/api/events.ko.md`
  - `doc/internals/multipart-atomicity.ko.md`
  - `doc/guide/03-1-pair.ko.md`
  - `doc/api/socket.ko.md`
- 변경 대상 파일
  - `doc/plan/rebuilding/rebuild-feature-parity-ralph-guide.ko.md`
  - `core/include/zlink.h`
  - `core/src/api/zlink.cpp`
  - `core/src/api/service_monitor_internal.hpp`
  - `core/src/core/msg.hpp`
  - `core/src/core/msg.cpp`
  - `core/src/services/discovery/discovery.hpp`
  - `core/src/services/discovery/discovery.cpp`
  - `core/src/services/spot/spot.hpp`
  - `core/src/services/spot/spot.cpp`
  - `core/src/services/spot/spot_node.hpp`
  - `core/src/services/spot/spot_node.cpp`
  - `core/src/services/spot/spot_pub.hpp`
  - `core/src/services/spot/spot_pub.cpp`
  - `core/src/services/spot/spot_sub.hpp`
  - `core/src/services/spot/spot_sub.cpp`
  - `core/src/sockets/socket_base.hpp`
  - `core/src/sockets/socket_base.cpp`
  - `core/src/sockets/pair.cpp`
  - `core/src/services/gateway/gateway.cpp`
  - `core/tests/CMakeLists.txt`
  - `core/tests/test_msg_refcnt.cpp`
  - `core/tests/test_socket_null.cpp`
  - `core/tests/monitoring/test_monitor_enhanced.cpp`
  - `core/tests/monitoring/test_service_monitor.cpp`
  - `core/tests/routing-id/test_stream_routing_id_size.cpp`
  - `core/tests/test_stream_socket.cpp`
  - `core/tests/test_pair_inproc.cpp`
  - `core/tests/test_socket_recv_handler.cpp`
  - `core/tests/test_socket_subscribe_handler.cpp`
  - `core/tests/test_socket_send_ready_handler.cpp`
  - `core/tests/spot/test_spot_unified_api.cpp`
- 추가/보강한 `core/tests`
  - `core/tests/spot/test_spot_unified_api.cpp`
    - unified `spot` `zlink_set_option()/zlink_get_option()` common option surface
    - unified `spot` `zlink_set_routing_id()/zlink_get_routing_id()` surface
    - unified `spot` local publish->subscribe `source_rid` 전달
  - `core/tests/test_msg_refcnt.cpp`
    - inline message refcount = 1
    - large message copy/close refcount transition
    - zero-copy message copy/close refcount transition
  - `core/tests/test_socket_null.cpp`
  - `core/tests/monitoring/test_service_monitor.cpp`
    - SPOT pub monitor subject payload 기반 `PUB_DELIVERY_READY_CHANGED`
    - SPOT pub monitor `PUB_FIRST_DELIVERY_READY_CHANGED`
    - service destroy 시 monitor `CLOSED` terminal event queue delivery
    - discovery invalid subscribe의 common `ERROR` event
    - SPOT sub invalid subscribe의 common `ERROR` event
    - SPOT pub async queue `EAGAIN` failure의 common `ERROR` event
  - `core/tests/test_pair_inproc.cpp`
    - 두 번째 `PAIR` peer connect 시 첫 번째 peer를 끊고 새 peer를 활성 peer로 교체
    - `inproc`는 bind 전에 connect하면 `ECONNREFUSED`
  - `core/tests/test_socket_recv_handler.cpp`
    - `PAIR` raw socket `zlink_recv_handler()` attach 후 direct recv / `zlink_msg_recv()` / `ZLINK_POLLIN`이 `EBUSY`
    - `PAIR` raw socket callback이 multipart payload 2개를 ownership 이전과 함께 전달
    - `ROUTER` raw socket callback이 source routing-id와 payload multipart를 분리 전달
    - unsupported raw socket(`PUB`)의 `zlink_recv_handler()`가 `ENOTSUP`
  - `core/tests/test_socket_subscribe_handler.cpp`
    - raw `SUB`에 `zlink_subscribe_handler()` attach 후 direct recv / `zlink_msg_recv()` / `ZLINK_POLLIN`이 `EBUSY`
    - raw `SUB` callback이 topic frame과 payload multipart를 분리 전달
    - unsupported raw socket(`PAIR`)의 `zlink_subscribe_handler()`가 `ENOTSUP`
  - `core/tests/test_socket_send_ready_handler.cpp`
    - raw `PAIR`에 `zlink_send_ready_handler()` attach/교체 후 data-plane `ZLINK_POLLOUT`이 `EBUSY`
    - raw `PAIR` send-ready callback이 writable transition에서 실제 dispatch된다
    - 동일 소켓 send-ready callback 내부 재등록은 `EDEADLK`
    - unsupported raw socket(`SUB`)의 `zlink_send_ready_handler()`가 `ENOTSUP`
  - `core/tests/spot/test_spot_unified_api.cpp`
    - unified `spot` exact/pattern subscription inventory와 local publish/recv
    - unified `spot` `zlink_subscribe_handler()` callback-only 전환 후 `zlink_subscribe()` `EBUSY`
- 실행한 검증 명령
  - `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
  - `cmake --build core/build -j$(nproc)`
  - `cmake --build core/build -j$(nproc) --target test_service_monitor`
  - `ctest --test-dir core/build --output-on-failure -R test_service_monitor`
  - `ctest --test-dir core/build --output-on-failure -R "test_service_monitor|test_socket_null|test_monitor_enhanced|test_msg_refcnt|test_stream_routing_id_size|test_stream_socket"`
  - `cmake --build core/build -j$(nproc) --target test_socket_recv_handler`
  - `ctest --test-dir core/build --output-on-failure -R test_socket_recv_handler`
  - `ctest --test-dir core/build --output-on-failure -R "test_pair_inproc|test_router_multiple_dealers|test_stream_socket|test_socket_null"`
  - `ctest --test-dir core/build --output-on-failure -R "test_socket_subscribe_handler|test_socket_recv_handler|test_pubsub_filter_xpub|test_socket_null"`
  - `ctest --test-dir core/build --output-on-failure -R "test_stream_socket|test_pair_inproc|test_router_multiple_dealers"`
  - `cmake --build core/build -j$(nproc) --target test_socket_send_ready_handler`
  - `ctest --test-dir core/build --output-on-failure -R "test_socket_send_ready_handler|test_socket_recv_handler|test_socket_subscribe_handler|test_socket_null|test_pair_inproc"`
  - `cmake --build core/build -j$(nproc) --target test_spot_unified_api`
  - `ctest --test-dir core/build --output-on-failure -V -R test_spot_unified_api`
  - `ctest --test-dir core/build --output-on-failure -R "test_spot_unified_api|test_spot_send_ready_handler|test_spot_pubsub_scenario|test_socket_subscribe_handler"`
  - `ctest --test-dir core/build --output-on-failure -R "test_spot_unified_api|test_spot_send_ready_handler|test_service_monitor|test_socket_subscribe_handler"`
  - `BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build`
  - `BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build` x7
  - `cmake --build core/build -j$(nproc)`
  - `BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build` -> `core/bench/with_zmq/results/single/report/perf_linux_20260329_090457.txt`
  - `BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build` -> `core/bench/with_zmq/results/single/report/perf_linux_20260329_090544.txt`
  - `BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build` -> `core/bench/with_zmq/results/single/report/perf_linux_20260329_090622.txt`
  - `BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build` -> `core/bench/with_zmq/results/single/report/perf_linux_20260329_100555.txt`
- 핵심 4셀 대비 판정
  - `PAIR 64B tcp`: 3회 median `+14.85%`
  - `PAIR 64B inproc`: 3회 median `-7.47%` warning
  - `DEALER_DEALER 64B tcp`: 3회 median `-10.84%` block
  - `DEALER_DEALER 64B inproc`: 3회 median `-6.34%` warning
- commit / push 여부
  - `perf block`으로 commit/push 보류
- 다음 iteration의 첫 작업
  - `PAIR 64B inproc`, `DEALER_DEALER 64B inproc` perf block이 monitoring slice와 무관한 inherited debt인지 분리 확인
    - options 기반 `zlink_socket_monitor_open()` null socket / null options 계약
  - `core/tests/monitoring/test_monitor_enhanced.cpp`
    - options 기반 socket monitor open 호출 경로
    - `zlink_monitor_snapshot()` source/detail/ready_count/state 계약
    - `zlink_monitor_close()` handle nulling 계약
    - `zlink_socket_monitor_handler()`의 callback-only 단방향 전환 계약
    - callback 전환 후 `zlink_socket_monitor_recv()` / 재등록 `EBUSY` 계약
    - callback 실행 중 외부 `zlink_monitor_close()`의 `EBUSY` 계약
    - callback 내부 self-close 성공 + deferred cleanup 계약
  - `core/tests/routing-id/test_stream_routing_id_size.cpp`
    - options 기반 monitor open + unified close 호출 경로
  - `core/tests/test_stream_socket.cpp`
    - stream monitor open 호출부를 options 기반으로 전환
  - `core/tests/monitoring/test_service_monitor.cpp`
    - discovery monitor snapshot 기본값과 callback-only 전환 후 `EBUSY` 계약
    - discovery provider change fanout에서 `PROVIDERS_CHANGED` / `SERVICE_UP` / `SERVICE_DOWN` payload 계약
    - SPOT pub monitor snapshot source/detail 계약
    - SPOT pub async queue full/drained event fanout 계약
    - SPOT sub monitor callback-only 전환 후 `EBUSY` 계약
    - `spot_node` target의 pub/sub source selection 계약
    - SPOT sub filter/subscription/delivery-ready event의 subject/subject_kind/value payload 계약
    - node target callback-only service monitor의 actual callback delivery 계약
- 실행한 검증 명령
  - `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R "test_msg_refcnt|test_msg_flags|test_msg_ffn"`
  - `ctest --test-dir core/build --output-on-failure -R "test_msg_refcnt|test_socket_null|test_monitor_enhanced|test_stream_routing_id_size|test_stream_socket"`
  - `ctest --test-dir core/build --output-on-failure -R "test_monitor_enhanced"`
  - `ctest --test-dir core/build --output-on-failure -R "test_socket_null|test_stream_routing_id_size|test_stream_socket"`
  - `ctest --test-dir core/build --output-on-failure -R "test_service_monitor"`
  - `ctest --test-dir core/build --output-on-failure -R "test_service_monitor|test_socket_null|test_monitor_enhanced|test_msg_refcnt|test_stream_routing_id_size|test_stream_socket"`
  - `ctest --test-dir core/build --output-on-failure -L unittest -j$(nproc)` -> 현재 CTest 등록상 `No tests were found`
  - `BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build` x6
  - `cmake --build core/build -j$(nproc) --target test_pair_inproc`
  - `ctest --test-dir core/build --output-on-failure -R test_pair_inproc`
  - `ctest --test-dir core/build --output-on-failure -R "test_pair_(inproc|tcp|ipc)"`
  - `BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build` -> `core/bench/with_zmq/results/single/report/perf_linux_20260329_084233.txt`
  - `BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build` -> `core/bench/with_zmq/results/single/report/perf_linux_20260329_084654.txt`
- 핵심 4셀 3회 median 판정
  - `PAIR 64B tcp`: `-0.20%`, `+10.77%`, `+4.64%` -> median `+4.64%` (`pass`)
  - `PAIR 64B inproc`: `-16.43%`, `-11.54%`, `-11.70%` -> median `-11.70%` (`block`)
  - `DEALER_DEALER 64B tcp`: `+9.11%`, `-2.03%`, `+13.43%` -> median `+9.11%` (`pass`)
  - `DEALER_DEALER 64B inproc`: `-15.28%`, `-14.38%`, `-16.19%` -> median `-15.28%` (`block`)
- 판정
  - 기능 계약(`zlink_msg_refcnt`)은 public header/implementation/test에 반영 완료
  - `socket_base::send()/recv()` ownership은 현재 코드가 Phase 1 계약과 일치함을 재확인했다.
  - socket monitor open은 options 구조체 기반 public API로 전환했고, raw monitor source를 가리키는 `zlink_monitor_snapshot()`과 unified `zlink_monitor_close()`를 최소 구현했다.
  - 관련 `core/`/`core/tests` 호출부는 options 기반 open과 unified close로 정리했고, 이번 iteration에서 socket monitor callback-only 전환과 close race 계약까지 추가 잠금했다.
  - service monitor는 discovery/spot_pub/spot_sub/spot_node target에 대해 최소 open/recv/handler/snapshot 경로를 public header/implementation/test에 반영했다.
  - service monitor recv는 queued actual event를 반환하고 queue 공백에서는 `EAGAIN`, handler 부착 후 `EBUSY`를 반환하는 계약으로 확장됐고, unified `zlink_monitor_close()` / `zlink_monitor_snapshot()`가 service handle에도 동작한다.
  - discovery monitor는 subscribed service 기준으로 `PROVIDERS_CHANGED`, `SERVICE_UP`, `SERVICE_DOWN` fanout과 `service_name`/`value` payload를 실제 runtime 변화에 맞춰 emit한다.
  - SPOT sub/node monitor는 local subscribe/unsubscribe 경로에서 `FILTER_APPLIED`, `SUBSCRIPTION_READY_CHANGED`, `SUB_DELIVERY_READY_CHANGED`를 actual fanout하며 `subject` / `subject_kind` / `value` payload와 callback delivery를 test로 잠갔다.
  - SPOT pub/node monitor는 async publish queue 경로에서 `SPOT_PUB_QUEUE_FULL` / `SPOT_PUB_QUEUE_DRAINED`를 actual fanout하며 queue depth/value 계약을 test로 잠갔다.
  - service monitor 공통 `ERROR` event는 discovery invalid subscribe, SPOT sub invalid subscribe, SPOT pub async queue `EAGAIN` failure에 대해 deterministic emit되도록 반영했고 `error_code`/recv 계약을 test로 잠갔다.
  - `zlink_service_event_t.subject_kind` 문서 계약에 맞춰 public 상수(`NONE/TOPIC/PATTERN`)를 header에 추가했다.
  - current smoke `perf_linux_20260329_083820.txt`에서는 `PAIR 64B tcp +5.19%`, `PAIR 64B inproc -14.40%`, `DEALER_DEALER 64B tcp +3.33%`, `DEALER_DEALER 64B inproc -18.46%`로 inproc block debt가 계속 남아 있다.
  - `PAIR` 문서 계약과 현재 구현을 다시 대조한 결과, “두 번째 peer connect 시 첫 번째 peer disconnect”가 첫 번째 실제 gap이었고 `pair_t::xattach_pipe()`와 `test_pair_inproc`에 반영했다.
  - 같은 `PAIR` 문서/transport 계약 기준으로 `inproc`는 bind-before-connect여야 하므로, bind 전 connect를 pending으로 허용하던 경로를 차단하고 `test_pair_inproc`에 `ECONNREFUSED` 계약을 추가했다.
  - 새 smoke `perf_linux_20260329_084233.txt`에서도 `PAIR 64B inproc -12.10%`, `DEALER_DEALER 64B inproc -12.57%`, `DEALER_DEALER 64B tcp -13.93%`가 남아 있어 이번 `PAIR` semantics 수정이 focused perf block을 해소하지는 않았다.
  - 후속 smoke `perf_linux_20260329_084654.txt`에서는 `PAIR 64B tcp +27.50%`, `PAIR 64B inproc -8.98%`, `DEALER_DEALER 64B tcp -0.35%`, `DEALER_DEALER 64B inproc -8.35%`로 block은 내려갔지만 inproc warning debt는 계속 남아 있다.
  - service monitor `ERROR` parity 자체는 구현 완료로 판단하고, commit/push는 perf block과 unrelated 변경 때문에 계속 보류한다.
  - 하지만 이번 재점검에서 `Phase 2 completed` 상태표가 현재 public header/API 현실과 어긋남을 확인했다.
  - `core/include/zlink.h`는 여전히 legacy buffer형 `zlink_send()/zlink_recv()`와 STREAM 전용 attach API만 export하고, `core/src/api/zlink.cpp`도 동일 surface만 구현한다.
  - 이번 iteration에서 `zlink_recv_handler()`는 raw `PAIR/DEALER/ROUTER/STREAM`에 대해 public header/API/test로 최소 반영했고 attach 후 direct recv / `zlink_msg_recv()` / data-plane `ZLINK_POLLIN`을 `EBUSY`로 막는 callback-only 계약을 추가로 잠갔다.
  - 이번 iteration에서 `zlink_subscribe_handler()`는 raw `SUB/XSUB`에 대해 public header/API/test로 최소 반영했고 attach 후 direct recv / `zlink_msg_recv()` / data-plane `ZLINK_POLLIN`을 `EBUSY`로 막는 callback-only 계약을 추가로 잠갔다.
  - 이번 iteration에서 `zlink_send_ready_handler()`는 raw `PAIR/PUB/XPUB/DEALER/ROUTER/STREAM`에 대해 public header/API/test로 최소 반영했고 attach 후 data-plane `ZLINK_POLLOUT`을 `EBUSY`로 막는 callback-only 계약, 교체 semantics, self-reentry `EDEADLK`, unsupported `SUB` `ENOTSUP`를 추가로 잠갔다.
  - 새 subscribe-handler smoke 3회에서는 `PAIR 64B tcp -3.03% / -18.59% / +11.92%` -> median `-3.03%` pass, `PAIR 64B inproc -17.59% / -11.38% / -8.82%` -> median `-11.38%` block, `DEALER_DEALER 64B tcp -16.26% / -18.61% / +10.16%` -> median `-16.26%` block, `DEALER_DEALER 64B inproc +1.06% / -18.98% / +1.91%` -> median `+1.06%` pass로 inherited focused perf debt가 여전히 남는다.
  - 이번 iteration에서 `zlink_send_ready_handler()`는 current split spot-family alias `spot_pub`/`spot_node`에 대해서도 service monitor 기반 callback dispatch와 `ZLINK_POLLOUT` conflict를 최소 구현으로 반영했다.
  - 새 focused smoke `perf_linux_20260329_092926.txt`에서는 `PAIR 64B tcp -20.01%`, `PAIR 64B inproc -9.97%`, `DEALER_DEALER 64B tcp +21.79%`, `DEALER_DEALER 64B inproc -19.63%`로 inproc block debt가 계속 남아 있다.
  - 반면 unified `spot`와 canonical 멀티파트 `zlink_send()/zlink_recv()` public API는 여전히 legacy buffer ABI와 충돌하는 미반영 항목으로 남아 있다.
  - `doc/guide/03-1-pair.ko.md`는 같은 문서군 안에서 다시 `PAIR`를 `recv/poller-only`로 서술해 상위 socket/core-api 문서와 충돌한다.
  - 따라서 실행 가이드 기준 현재 첫 미완료 작업은 `PAIR` 개별 semantics 추가가 아니라 public socket data-plane/callback parity 범위를 다시 고정하고 Phase 2를 재개하는 것이다.
  - 그 재개 범위에서 raw `zlink_recv_handler()`, raw `SUB/XSUB zlink_subscribe_handler()`, raw `zlink_send_ready_handler()`, current split spot-family alias send-ready 최소 parity를 반영했고, 다음 첫 미완료 항목은 unified `spot`/canonical 멀티파트 `zlink_send()/zlink_recv()` ABI inventory 재고정이다.
- commit / push 여부
  - 미실시. focused perf block과 unrelated 변경 존재로 green slice commit 조건 불충족
- 다음 iteration의 첫 작업
  - unified `spot` 문서 surface와 current split `spot_pub`/`spot_sub`/`spot_node` 구현 차이를 실행 가이드 기준으로 다시 inventory
  - canonical 멀티파트 `zlink_send()/zlink_recv()` ABI 교체 범위는 현재 legacy buffer ABI와 충돌하므로 별도 inventory로 분리 유지
  - focused with_zmq perf debt는 기능 parity와 분리해 계속 추적

## 14. 종료 출력 계약

루프 안의 Codex는 아래 세 경우 중 하나만 최종 한 줄로 출력해야 한다.

- 더 이상 미적용 사항이 없을 때:

```text
미적용 사항이 없습니다.
```

- 사용자 결정 없이는 더 진행할 수 없을 때:

```text
사용자 입력 필요: <한 줄 이유>
```

- 그 외 모든 경우:

```text
계속 진행 필요
```
