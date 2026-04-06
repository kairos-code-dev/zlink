# `bindings/*/perf` 정책 정렬 선행 리팩토링 계획

> 1차 범위: [`bindings/cpp/perf/`](/home/hep7/project/kairos/zlink/bindings/cpp/perf)
> 를 시작점으로 하되, 같은 원칙을 다른 binding perf에도 순차 적용한다.
> 이번 계획은 [`doc/perf/`](/home/hep7/project/kairos/zlink/doc/perf)
> 정책 준수, 각 binding public surface 일관성, POSD 기반 구조 단순화를 먼저
> 완료하는 실행 계획이다.

## 1. 목표

이번 작업의 목표는 perf 코드를 "돌아가게만" 정리하는 것이 아니다.
`bindings/*/perf`가 아래 성질을 구조적으로 만족하도록 만드는 것이다.

- 각 binding perf는 **해당 언어의 public binding surface만 사용**한다.
- `doc/perf` 정책과 실제 실행 surface가 충돌하지 않는다.
- `core/perf`와 **동일한 측정 의미**를 유지한다.
- 각 binding perf는 **그 언어 사용자 입장에서 자연스러운 스타일**로 구현된다.
  - C++: RAII, typed wrapper, value type, 명시적 ownership
  - Java/.NET: managed object, callback/async model, public API naming 일관성
  - Rust: ownership/borrowing, `Result`, safe wrapper 우선
  - Go: Go public API / goroutine / channel 스타일 우선
  - Node/Python: 해당 언어의 async/runtime 관례 우선
- `--recv`는 정책이 허용한 패턴에서만 같은 public pattern 안에서 선택된다.
- callback / recv / control / metric 집계 책임이 섞이지 않는다.
- hot path와 cold path의 경계를 명확히 하고, callback hot path에서는 정책이
  금지한 blocking lock과 raw handle ownership 전파를 제거한다.
- 새 패턴을 추가하거나 기존 정책을 수정할 때 변경 증폭이 작다.
- 성능 비교와 baseline ratio 판정은 후행 단계로 미루고, 우선
  "정책에 맞는 구현 완료" 상태를 모든 대상 binding에 확보한다.

## 1.1 이번 문서의 단계 구분

이번 문서는 의도적으로 두 단계를 분리한다.

- 1단계:
  정책 정렬 / 구현 완료 / public surface 정리 / direct native API 제거
- 2단계:
  baseline 비교 / before-after 성능 회귀 판정 / 목표 ratio 충족 작업

즉 이번 턴들의 완료 기준은 "성능이 충분히 좋다"가 아니라
"정책과 측정 의미에 맞는 구현이 끝났다"이다.

## 1.2 해석 우선순위

이 문서에서 우선순위는 아래 순서를 따른다.

1. `doc/perf/*.md` 와 execution guide의 명시 policy contract
2. `core/perf`와 동일한 측정 의미
3. POSD 기반 구조 단순화
4. 해당 언어 스타일 정렬
5. 후행 성능 비교와 최적화

즉 언어 스타일이나 구조 단순화를 이유로 측정 의미를 바꾸면 안 된다.
해당 언어에서 더 자연스러운 구현 방식이 있더라도, 그것이 `core/perf`의
phase, recv/send model, callback model, throughput/bandwidth/latency 정의를
바꾸면 채택하지 않는다.
또한 상위 policy authority가 이미 고정한 shared component, canonical surface,
pattern 역할 분해를 POSD나 언어 스타일 이유로 다시 해석하면 안 된다.

## 1.3 적용 순서

이 계획은 모든 binding에 동시에 적용하지 않는다.

1. 현재 활성 binding 하나를 선택해 1단계를 끝낸다.
2. 같은 원칙과 완료 조건을 다음 binding에 그대로 반복 적용한다.
3. 대상 binding 전체에 1단계가 끝난 뒤에만 2단계 성능 비교를 수행한다.

즉 full comparable run과 baseline ratio 판정은 모든 대상 binding에서
"정책 준수 + 구현 완료 + 측정 의미 정렬"이 끝난 뒤의 후행 단계다.

## 2. 관련 기준 문서

- 통합 정책:
  [`doc/perf/PERF_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_POLICY.md)
- single 정책:
  [`doc/perf/PERF_SINGLE_TEST_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_SINGLE_TEST_POLICY.md)
- multi 정책:
  [`doc/perf/PERF_MULTI_TEST_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_MULTI_TEST_POLICY.md)
- 저장소 공통 규칙:
  [`AGENTS.md`](/home/hep7/project/kairos/zlink/AGENTS.md)
- 현재 C++ binding perf 빌드 정의:
  [`bindings/cpp/perf/CMakeLists.txt`](/home/hep7/project/kairos/zlink/bindings/cpp/perf/CMakeLists.txt)
- 현재 binding perf 실행기:
  [`bindings/cpp/perf/run_policy_bench.py`](/home/hep7/project/kairos/zlink/bindings/cpp/perf/run_policy_bench.py)
  [`bindings/cpp/perf/run_binding_single.sh`](/home/hep7/project/kairos/zlink/bindings/cpp/perf/run_binding_single.sh)
  [`bindings/cpp/perf/run_binding_multi.sh`](/home/hep7/project/kairos/zlink/bindings/cpp/perf/run_binding_multi.sh)
- bindings perf 실행 authority:
  [`core/tools/bindings-perf/bindings-perf-execution-guide.ko.md`](/home/hep7/project/kairos/zlink/core/tools/bindings-perf/bindings-perf-execution-guide.ko.md)
- 의미 정렬 기준 구현:
  [`core/perf/README.md`](/home/hep7/project/kairos/zlink/core/perf/README.md)
  [`core/perf/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/run_comparison.py)
  [`core/perf/single/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/single/run_comparison.py)
- baseline 결과:
  [`core/perf/baseline/`](/home/hep7/project/kairos/zlink/core/perf/baseline)

참고:
- 현재 문서의 명령 예시와 파일 예시는 1차 범위인 `cpp`를 기준으로 적는다.
- 다른 binding으로 확장할 때는 같은 원칙을 유지하되, 명령/파일 경로만
  `bindings/<lang>/perf`에 맞게 치환한다.

## 3. 작업 시작 전제

- 저장소 루트:
  `/home/hep7/project/kairos/zlink`
- 공식 빌드 디렉터리:
  `/home/hep7/project/kairos/zlink/core/build`
- 이번 계획은 top-level `build/`를 사용하지 않는다.
- 현재 워크트리는 dirty 상태일 수 있으므로, 기존 수정과 충돌하지 않게
  단계별로 작은 패치로 나눈다.
- baseline 비교는 현재 저장소에 존재하는
  `/home/hep7/project/kairos/zlink/core/perf/baseline`
  산출물을 기준으로 수행한다.
- 이 문서는
  [`core/tools/bindings-perf/bindings-perf-execution-guide.ko.md`](/home/hep7/project/kairos/zlink/core/tools/bindings-perf/bindings-perf-execution-guide.ko.md)
  의 하위 실행 계획이며, 충돌 시 execution guide를 우선한다.
- 다만 이번 계획의 즉시 범위는 execution guide 전체를 한 번에 끝내는 것이 아니라,
  그 가이드의 선행 조건인 "정책 준수 + 정상 구현 surface 확보"까지를 먼저
  각 binding에 순차 적용하는 것이다.

## 4. 설계 원칙

### 4.1 POSD 원칙

- **깊은 모듈 우선**:
  helper는 "코드 재사용"보다 "정책 계약 캡슐화"를 책임져야 한다.
- **얕은 wrapper 금지**:
  native API 이름만 바꿔 다시 노출하는 perf helper는 삭제 대상이다.
- **언어 스타일 우선**:
  perf helper가 내부적으로는 공통 의미를 가져도, 외부 구조와 사용 방식은
  해당 언어의 관용적 스타일을 따라야 한다.
- **의미 보존 절대 우선**:
  언어 스타일 정렬은 허용되지만, `core/perf`와의 측정 의미 동일성을 깨는
  스타일 변경은 금지한다.
- **숨은 결합 제거**:
  파일명, 바이너리명, `--recv`, runtime wrapper, runner 해석이 서로 다르면 안 된다.
- **변경 증폭 축소**:
  정책 수정 시 pattern 파일 여러 개를 동시에 수정해야 하는 구조를 줄인다.
- **hot path 명확화**:
  callback/recv hot path에서 결과 출력, phase 판정, heap-heavy 컨테이너 조작,
  blocking lock을 분리한다.

### 4.1.1 언어 스타일 적용 규칙

- POSD 기반 공통화는 허용하지만, 공통화 결과가 해당 언어에서 부자연스러운
  얕은 bridge 나 raw native helper 증식으로 보이면 실패로 본다.
- 각 binding perf는 "그 언어 binding을 실제로 측정한다"는 인상이 코드에서
  직접 보여야 한다.
- native API 개념을 그대로 재노출하는 helper보다, 언어 API의 개념 단위로
  계약을 감싸는 깊은 helper를 우선한다.
- style 정렬은 formatting 문제가 아니라 API shape, ownership model,
  error model, callback model까지 포함한 구조 문제로 본다.

### 4.2 perf 정책 원칙

- perf 코드는 **해당 바인딩의 공식 public surface 하나**만 측정해야 한다.
- binding perf에서 direct native API 사용은 금지한다.
- binding perf에서 direct native handle / native message / native poll item을
  helper 뒤에 숨겨 우회하는 것도 금지한다.
- 상위 policy authority가 shared component 사용을 명시한 경우, 그 component는
  binding-local 구현으로 치환하는 리팩토링 대상이 아니다.
  특히 `STREAM` shared client처럼 비교 의미와 실행 surface에 포함된 shared path는
  POSD/언어 스타일 이유로 binding-local client로 바꾸면 안 된다.
- 이 계획의 완료 상태에서는 현재 활성 binding perf 트리 안의 direct native
  호출이 **0건**이어야 한다.
- single은 callback only다.
- multi는 recv 기본, `SPOT` / `STREAM`만 `recv|callback` dual-mode다.
- `recv`와 `callback`은 같은 pattern 안에서 `--recv` 값으로만 선택한다.
- callback mode를 이유로 별도 public pattern 이름이나 별도 public binary surface를
  만들지 않는다.
- perf 수정은 정책 정합성 수정, `core/perf` 의미 정렬, benchmark bug 수정일 때만
  허용한다.
- 숫자 부스팅용 perf shortcut, payload 축소, sample 왜곡, 조건 완화는 금지한다.
- `core/perf`의 recv/send/poller/callback semantics를 바꾸는 방향으로
  현재 활성 binding perf를 수정하면 안 된다.
- 특히 `core/perf`의 blocking recv / poller drain 의미를 피하려고 binding perf를
  nonblocking recv 또는 다른 I/O model로 바꾸면 안 된다.
- shared `STREAM` client path처럼 policy가 이미 정한 canonical dependency를
  "미완성 binding gap"으로 오판하면 안 된다.
- 패턴별 mode 구현은 `core/perf`의 실제 역할 분해와 같아야 한다.
  예를 들어 어느 패턴의 callback semantics가 client 쪽에만 존재한다면,
  binding perf도 server 쪽에 별도 callback 의미를 새로 만들면 안 된다.

### 4.3 `core/perf` 의미 동일성 원칙

`bindings/*/perf`는 `core/perf`와 숫자만 비슷하게 나오는 별도 benchmark가
아니라, **동일한 측정 의미를 binding surface로 재현하는 benchmark**여야 한다.

이번 계획에서 의미 동일성은 아래를 포함한다.

- 동일 pattern direction 분류
  - single 전체: one-way
  - multi echo: `DEALER_ROUTER`, `ROUTER_ROUTER`, `STREAM`
  - multi one-way: `DEALER_DEALER`, `PUBSUB`, `SPOT`
- 동일 throughput 정의
  - single / multi one-way:
    `recv_count / duration_seconds`
  - multi echo:
    active 구간 왕복 완료 수 / duration
- 동일 bandwidth 정의
  - single 전체 + multi one-way:
    `throughput × size / 1,000,000`
  - multi echo:
    `throughput × size × 2 / 1,000,000`
- 동일 latency sample set
  - active phase의 유효 header 메시지 집합으로 mean / p95 / p99 산출
- 동일 phase 의미
  - `ready -> warmup -> active`
  - extra settle/prime/drain phase를 새 benchmark phase로 승격하지 않음
- 동일 result surface
  - `recv_mode`
  - 결과 파일명
  - effective options
  - RESULT line contract

즉 binding perf 리팩토링은 "구현을 바꾸되 benchmark의 의미는 바꾸지 않는 작업"
이어야 한다.

## 5. 초기 진단에서 확인된 대표 문제

이 장은 "리팩토링 시작 시점에 어떤 종류의 문제가 있었는가"를 설명하는
초기 진단 기록이다. 이미 해결된 항목이 있더라도, 이후 다른 binding에 같은
문제가 재발하지 않게 하기 위한 기준으로 유지한다.

### 5.1 direct native API 사용

초기 `bindings/cpp/perf`에는 C++ binding perf임에도 아래 성질이 남아 있었다.

- `zlink_recv`, `zlink_send_rid`, `zlink_poll`, `zlink_msg_move`,
  `zlink_multipart_close` 같은 direct native API 호출이 남아 있다.
- raw `zlink_msg_t` ownership과 routing id native struct가 perf helper 밖으로
  새어 나온다.
- 결과적으로 "C++ binding perf"가 아니라 "native API를 C++에서 감싼 perf"가 된다.

이 문제는 단순 스타일 문제가 아니라 측정 대상이 흐려지는 구조 문제다.

### 5.2 `--recv` surface와 실제 구현 surface의 불일치

정책은 `MULTI_STREAM` / `MULTI_SPOT`에서 `--recv recv|callback`을 같은 public
pattern 안에서 선택하도록 요구한다. 초기 진단 당시에는:

- `STREAM` callback 전용 별도 server source / target이 존재한다.
- runtime wrapper는 그 별도 target을 public runtime surface로 노출하지 않는다.
- runner는 여전히 `comp_src_stream_server` 하나를 canonical server로 본다.

즉 구현이 정책 표면과 다르고, dead path 또는 shadow path가 생긴다.

### 5.3 callback hot path의 얽힘

정책은 callback hot path에서

- small POD metric event handoff,
- bounded queue,
- non-blocking / lock-free 성격의 state handoff

를 요구한다. 초기 진단 당시 callback stream helper에는:

- same-socket pending state를 `std::mutex`로 보호하는 경로,
- app thread와 callback thread가 같은 pending 구조를 공유하는 경로,
- raw multipart ownership을 callback helper가 직접 들고 처리하는 경로

가 남아 있다.

### 5.4 single 정책 이탈

초기 single `SPOT` sender는 자연 backpressure를 유지한 blocking send 모델이 아니라
`try_publish()` 기반으로 바뀐 흔적이 있다. 이는 single callback 정책과 충돌한다.

### 5.5 `core/perf` 대비 의미 드리프트 위험

초기 `bindings/cpp/perf`는 아래 이유로 `core/perf` baseline과의 비교 의미를
훼손할 위험이 있다.

- direct native API 우회 때문에 binding layer 비용/계약을 건너뛴다.
- `STREAM` callback이 canonical `--recv` mode가 아니라 별도 shadow path에
  가깝다.
- single `SPOT`의 send model이 core single 계약과 다르다.
- callback hot path의 lock/ownership 구조가 benchmark 인프라 오버헤드를 측정값에
  섞을 수 있다.

이 문제를 방치하면 baseline 비교는 "같은 의미의 성능 비교"가 아니라
"다른 benchmark 간 숫자 비교"가 된다.

## 6. 이번 리팩토링의 비목표

- core 라이브러리 동작 변경
- 다른 binding perf까지 한 번에 같이 수정
- 성능 개선 자체를 1차 목표로 삼는 대규모 알고리즘 변경
- perf policy 문서의 의미 변경

이번 작업의 1차 목표는 **정책 준수와 구조 정리**다.
성능 수치 비교와 ratio 판정은 2차 목표로 미룬다.

그리고 execution guide의 우선순위를 그대로 따른다.

1. 현재 활성 binding perf가 정책을 만족하면서 전체 패턴/전체 사이즈에서 정상
   동작하는지 확보
2. 같은 기준으로 다음 binding에 1단계를 반복 적용
3. 대상 binding 전체의 1단계가 끝난 뒤 성능 비교와 개선 수행

## 7. 목표 구조

## 7.1 public surface

- single public pattern:
  `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `SPOT`
- multi public pattern:
  `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `PUBSUB`, `SPOT`, `STREAM`
- mode 선택:
  `--recv recv|callback`

여기서 public surface는 다음 네 층이 모두 같아야 한다.

- 정책 문서
- shell runner usage
- Python runner 해석
- build/runtime binary mapping

## 7.2 계층 분리

리팩토링 후 책임은 아래처럼 나눈다.

### pattern file

- pattern-specific socket/service topology
- 정책이 허용한 recv mode 지원 여부 선언
- pattern-specific ready contract 연결

### mode helper

- recv mode event loop
- callback mode metric handoff
- pending/backpressure state machine

### policy helper

- ready gate
- `START` / `STOP` / control protocol
- canonical binary / runtime mapping

### cold path

- 결과 출력
- phase 종료 후 집계
- cleanup

### binding-style helper

- 해당 언어에서 자연스러운 객체/함수/모듈 경계 제공
- raw/native contract를 직접 노출하지 않고 언어 API 개념으로 의미를 감춤
- perf 정책 helper와 언어 API helper의 책임 분리

## 7.3 direct native API 제거 방향

1차 범위에서 현재 활성 binding perf에 허용되는 의존은 아래로 제한한다.

현재 활성 binding이 `cpp`인 경우 예시는 아래와 같다.

- `zlink::socket_t` 계열
- `zlink::service::*`
- `zlink::message_t`
- `zlink::routing_id_t`
- `zlink::poller_t`
- monitor / service_monitor public wrapper

즉 새 helper는 native `zlink_msg_t *`, raw `zlink_routing_id_t *`, raw
`zlink_pollitem_t`를 public helper 계약으로 사용하지 않는다.

## 8. 단계별 실행 계획

### 단계 0. binding별 현황 동결

목적:
- 지금 어떤 파일이 정책 위반인지 명확히 고정한다.

할 일:
- 각 binding perf의 direct native API 사용 위치 전수 목록화
- `--recv` / binary / runtime mapping 표 작성
- dead path, shadow path, canonical path 구분
- `core/perf`와의 semantic parity 체크리스트 작성
  - throughput
  - bandwidth
  - latency sample set
  - phase
  - direction
  - recv_mode
  - result naming
- 리팩토링 시작 전 snapshot 채집은 선택 사항으로 낮춘다.
  - 있으면 후속 회귀 비교에 쓴다.
  - 없으면 1단계 blocker로 보지 않는다.
- execution guide 기준 comparable run 조건 표 작성
  - recv/callback baseline file
  - transport
  - size coverage
  - client count
  - warmup/duration
  - recv mode
  - 결과 파일 경로

완료 기준:
- "무엇을 삭제/승격/이동할지"가 파일 단위로 정리된다.
- baseline 비교를 깨는 의미 차이가 체크리스트로 드러난다.
- 이후 성능 단계에서 사용할 비교 기준이 문서로만이라도 고정된다.
- comparable / non-comparable 판정 기준이 문서로 고정된다.

### 단계 1. public surface 정렬

목적:
- 정책과 실행 surface를 먼저 일치시킨다.

할 일:
- 패턴별 shadow path, dead path, callback 전용 별도 public surface 제거
- `run_binding_multi.sh`, runtime wrapper, build target mapping을
  `STREAM + --recv` 기준으로 정렬
- single/multi help 문구에서 정책과 어긋나는 표현 제거
- core runner와 naming / canonical binary mapping 차이 정리

완료 기준:
- 각 대상 pattern은 하나의 canonical public pattern만 가진다.
- `--recv callback`은 shadow path가 아니라 canonical path를 선택한다.
- binding perf 실행 surface가 `core/perf`와 같은 의미로 읽힌다.

### 단계 2. direct native API 제거

목적:
- binding perf가 binding surface만 측정하도록 만든다.

할 일:
- `perf_socket_compat.hpp`를 얕은 native bridge가 아니라 필요한 binding gap만
  좁히는 최소 shim으로 축소
- stream/spot/pubsub helpers에서 raw native API 호출 제거
- raw monitor/service monitor polling을 해당 언어의 public wrapper 기반으로 교체
- helper가 native API 개념을 다시 노출하지 않도록 해당 언어 스타일의 계약으로 재구성

완료 기준:
- 현재 활성 binding perf 트리 안의 direct native 호출이 0건이다.
- binding perf가 core perf와 다른 측정 contract를 우회로로 만들지 않는다.
- 코드 구조가 해당 언어 binding을 측정하는 코드처럼 읽힌다.

### 단계 3. callback hot path 재설계

목적:
- policy가 요구하는 hot/cold path 분리를 맞춘다.

할 일:
- callback path에서 same-owner 또는 core와 동일한 owner 분해를 유지하도록 정리
- callback hot path는 metric event 추출과 최소 state update만 수행
- app thread / worker thread는 POD event queue만 소비

완료 기준:
- callback hot path에 policy가 금지한 shared blocking state가 남지 않는다.
- raw message ownership이 callback 밖으로 넘어가지 않는다.
- callback role 분해가 `core/perf`와 충돌하지 않는다.

### 단계 4. single policy 정렬

목적:
- single perf의 callback-only / blocking-send 계약을 회복한다.

할 일:
- single `SPOT` sender 경로를 policy 문서에 맞게 재정렬
- single pattern 전반에서 sync recv API가 측정 경로에 끼지 않는지 재확인
- dead code 형태의 service monitor / raw polling 잔재 제거

완료 기준:
- single `PAIR`, `PUBSUB`, `DEALER_*`, `ROUTER_*`, `SPOT` 모두 callback-only 계약이
  코드에서 직접 드러난다.

### 단계 5. 공통화 재구성

목적:
- 파일 수를 줄이는 것이 아니라 변경 증폭을 줄이는 공통 구조를 만든다.

할 일:
- handshake / control / phase helper를 "정책 계약 단위"로 재배치
- stream / spot의 mode-specific helper와 pattern-specific helper를 분리
- 이름만 바꾼 중복 helper 제거

완료 기준:
- helper 이름만 봐도 "policy helper", "mode helper", "pattern helper"가 구분된다.
- helper shape만 봐도 해당 언어 스타일과 ownership/error model이 드러난다.

### 단계 6. 정책 구현 검증과 문서 정리

목적:
- 구조 정리 후 실행 contract를 고정한다.

할 일:
- build 확인
- 정책 runner smoke
- 결과 surface / help / 문서 정합성 점검
- 필요한 경우 `doc/perf`와 모순되는 binding-specific 실행 문구 수정
- `core/perf` baseline과 비교 가능한 semantic parity 확인
- full comparable run 준비 조건 확인
- 결과 파일이 `bindings/<lang>/perf/results/` 아래 남는지 확인

완료 기준:
- runner, runtime wrapper, build target, 결과 파일 naming이 정책과 맞다.
- `core/perf`와의 의미 차이가 문서상/구조상 설명 가능하게 정리된다.
- execution guide 기준 full comparable run을 다음 단계에서 바로 수행할 수 있는
  상태가 된다.

### 단계 7. 후행 성능 검증

목적:
- 정책과 구조 정렬이 끝난 뒤에만 성능 비교를 수행한다.

할 일:
- binding before/after snapshot 비교
- baseline comparable run
- 목표 ratio 확인
- 필요 시 binding library hot path 최적화

완료 기준:
- execution guide 기준 comparable run이 완료된다.
- baseline ratio 판정과 before/after 회귀 판정이 끝난다.

## 9. 패치 순서 권장안

이 작업은 한 번에 크게 합치지 않는다. 아래 순서로 나눈다.

1. `run_binding_multi.sh` / `prepare_cpp_runtime.py` / `CMakeLists.txt`
   surface 정렬
2. `perf_socket_compat.hpp` 축소와 binding gap 정리
3. single `SPOT` / `PUBSUB` direct native API, send model 정렬
4. multi `STREAM` recv/callback 구조 재정리
5. multi `SPOT` callback/recv helper 정리
6. dead helper / dead target 삭제
7. 각 binding별 정책 구현 검증과 문서 정리
8. 후행 comparable run / 성능 검증

각 배치 뒤에는 build 성공만 확인하지 않고 아래를 함께 점검한다.

- semantic parity 유지 여부
- 후행 baseline 비교 준비 상태인지 여부
- throughput / bandwidth / latency triplet 계약 유지 여부
- direct native API 0건 유지 여부
- execution guide comparable 조건을 다음 단계에서 바로 수행 가능한지 여부
- 결과 파일이 `bindings/<lang>/perf/results/` 아래 남는지 여부

이 순서를 지키는 이유는, public surface를 먼저 고정해야 내부 리팩토링이
"어느 경로가 진짜 경로인지" 혼동 없이 진행되기 때문이다.

## 10. 검증 명령

기본 build:

```bash
cmake --build core/build --target \
  cpp_perf_pair \
  cpp_perf_pubsub \
  cpp_perf_dealer_dealer \
  cpp_perf_dealer_router \
  cpp_perf_router_router \
  cpp_perf_spot \
  cpp_comp_src_dealer_dealer_server \
  cpp_comp_src_dealer_dealer_client \
  cpp_comp_src_dealer_router_server \
  cpp_comp_src_dealer_router_client \
  cpp_comp_src_router_router_server \
  cpp_comp_src_router_router_client \
  cpp_comp_src_pubsub_server \
  cpp_comp_src_pubsub_client \
  cpp_comp_src_spot_server \
  cpp_comp_src_spot_client \
  cpp_comp_src_stream_server
```

single 정책 smoke:

```bash
python3 bindings/cpp/perf/run_policy_bench.py \
  --binding cpp \
  --suite single \
  --pattern PUBSUB,SPOT \
  --transports tcp \
  --msg-sizes 64 \
  --duration 1 \
  --warmup 1
```

multi recv smoke:

```bash
python3 bindings/cpp/perf/run_policy_bench.py \
  --binding cpp \
  --suite multi \
  --pattern STREAM,SPOT \
  --recv recv \
  --transports tcp \
  --msg-sizes 64 \
  --duration 1 \
  --warmup 1 \
  --clients 4
```

multi callback smoke:

```bash
python3 bindings/cpp/perf/run_policy_bench.py \
  --binding cpp \
  --suite multi \
  --pattern STREAM,SPOT \
  --recv callback \
  --transports tcp \
  --msg-sizes 64 \
  --duration 1 \
  --warmup 1 \
  --clients 4
```

semantic parity / baseline 확인:

```bash
ls -1 core/perf/baseline
rg -n "\bzlink_[a-z0-9_]+\s*\(" bindings/cpp/perf
python3 bindings/cpp/perf/run_policy_bench.py \
  --binding cpp \
  --suite multi \
  --pattern STREAM \
  --recv recv \
  --transports tcp \
  --msg-sizes 64 \
  --duration 1 \
  --warmup 1 \
  --clients 4
```

후행 full comparable run 확인:

```bash
python3 bindings/cpp/perf/run_policy_bench.py \
  --binding cpp \
  --suite multi \
  --pattern ALL \
  --recv recv
python3 bindings/cpp/perf/run_policy_bench.py \
  --binding cpp \
  --suite multi \
  --pattern STREAM,SPOT \
  --recv callback
python3 bindings/cpp/perf/run_policy_bench.py \
  --binding cpp \
  --suite single \
  --pattern ALL
```

후행 before/after binding snapshot 확인:

```bash
python3 bindings/cpp/perf/run_policy_bench.py \
  --binding cpp \
  --suite single \
  --pattern PUBSUB,SPOT \
  --transports tcp \
  --msg-sizes 64 \
  --duration 1 \
  --warmup 1
python3 bindings/cpp/perf/run_policy_bench.py \
  --binding cpp \
  --suite multi \
  --pattern STREAM,SPOT \
  --recv recv \
  --transports tcp \
  --msg-sizes 64 \
  --duration 1 \
  --warmup 1 \
  --clients 4
python3 bindings/cpp/perf/run_policy_bench.py \
  --binding cpp \
  --suite multi \
  --pattern STREAM,SPOT \
  --recv callback \
  --transports tcp \
  --msg-sizes 64 \
  --duration 1 \
  --warmup 1 \
  --clients 4
```

비교 시 확인 항목:

- pattern direction이 core와 같은가
- throughput 단위가 같은가
- bandwidth 계산식이 같은가
- latency / p95 / p99가 같은 active sample set에서 나오는가
- `recv_mode`, 결과 파일명, effective options가 같은 규칙을 따르는가
- `bindings/<lang>/perf` direct native API grep 결과가 0건인가
- 결과 파일이 `bindings/<lang>/perf/results/` 아래 남는가
- comparable run 조건을 만족하는가

baseline gate 절차:

1. `core/perf/baseline/`에서 이번 실행과 같은 `recv_mode`의 최신 baseline 파일을
   선택하고, 그 파일 안에 같은 pattern / transport / size RESULT line이 있는지
   확인한다.
2. 같은 pattern / transport / size 조합으로 `core/perf` 공식 실행기와
   현재 활성 binding perf 실행기를 각각 돌린다.
3. 먼저 의미 gate를 본다.
   - pattern direction
   - throughput 단위
   - bandwidth 계산식
   - latency sample set
   - phase
   - result surface
4. 의미 gate를 통과한 경우에만 baseline 숫자 비교를 본다.
   - baseline 파일에 해당 조합 RESULT line이 없으면, 그 조합은 최신 baseline
     부재로 기록하고 이번 턴에는 `core/perf` fresh run 결과를 reference로 쓴다.
5. baseline 숫자 비교는 아래 기준으로 fail-fast 한다.
   - throughput 회귀가 5% 초과
   - latency mean / p95 / p99 중 하나라도 악화가 5% 초과
6. 수치가 다르지만 의미 gate는 통과한 경우, 원인이 binding overhead인지
   benchmark 구현 차이인지 구분해 기록한다. 구현 차이면 리팩토링 미완료로 본다.

후행 comparable run gate 절차:

1. 결과 파일은 반드시 `bindings/<lang>/perf/results/` 아래 report로 남긴다.
2. 아래가 하나라도 다르면 non-comparable 로 본다.
   - recv mode
   - transport
   - pattern coverage
   - size coverage
   - client count
   - warmup/duration
3. probe/smoke 결과는 방향 확인용으로만 쓰고, baseline ratio 또는 완료 판정에는
   사용하지 않는다.
4. 최종 완료 판정 전에는 반드시 full comparable run을 다시 수행한다.

후행 binding before/after gate 절차:

1. 단계 0에서 확보한 binding snapshot과 같은 조합으로 현재 결과를 비교한다.
2. 의미 gate를 통과한 상태에서만 숫자 비교를 한다.
3. 숫자 비교는 아래 기준으로 fail-fast 한다.
   - throughput 회귀가 5% 초과
   - latency mean / p95 / p99 중 하나라도 악화가 5% 초과
4. core baseline 대비 개선/유지가 보이더라도 binding before/after 회귀가 있으면
   해당 패치는 완료로 보지 않는다.

## 11. 리스크와 대응

### 리스크 1. binding surface가 아직 perf에 필요한 API를 충분히 감싸지 못함

대응:
- perf에서 raw native API로 우회하지 말고 binding layer에 필요한 최소 public
  wrapper를 추가한 뒤 perf는 그 wrapper를 사용한다.

### 리스크 2. 언어 스타일 정렬이 `core/perf` 의미와 충돌할 수 있음

대응:
- 스타일 변경보다 `core/perf` 의미 동일성을 먼저 본다.
- 패턴별 callback/recv role 분해가 core와 다르면 스타일 정렬안을 폐기한다.
- recv / callback 두 mode를 같은 public pattern에서 비교 가능하게 유지한다.

### 리스크 2-1. POSD/언어 스타일 해석이 상위 policy authority를 덮어쓰는 경우

대응:
- shared component, canonical dependency, explicit shared client 같은 항목은
  먼저 policy contract인지부터 확인한다.
- 상위 authority가 고정한 계약이면 구조 선호와 무관하게 그대로 유지한다.
- "더 언어답다", "더 자연스럽다", "더 깊은 모듈이다" 같은 이유만으로
  policy contract를 binding-local 구현으로 치환하지 않는다.

### 리스크 3. surface 정리 전에 내부 helper부터 건드리면 dead path가 늘어남

대응:
- 반드시 단계 1을 먼저 수행한다.

### 리스크 4. 의미 동일성 없이 숫자만 맞추려는 유혹

대응:
- "baseline 대비 수치"보다 "같은 측정 의미인지"를 먼저 gate로 둔다.
- semantic parity 체크리스트를 통과하지 못하면 baseline 비교를 무효로 본다.

### 리스크 5. direct native API를 helper 뒤에 숨겨서 우회하는 경우

대응:
- helper 내부 캡슐화도 허용하지 않는다.
- 현재 활성 binding perf 트리에서 direct native 호출 grep 0건을 완료 gate에 넣는다.
- 필요한 기능이 binding surface에 없으면 perf에서 우회하지 않고
  해당 binding public wrapper를 먼저 보강한다.

### 리스크 6. baseline 환경 차이 때문에 리팩토링 회귀 판정이 흔들리는 경우

대응:
- `core/perf/baseline` 비교와 별도로 binding before/after snapshot 비교를 같이 둔다.
- core baseline은 의미 정렬과 장기 reference, binding snapshot은 이번 리팩토링의
  직접 회귀 gate로 사용한다.

### 리스크 7. probe 결과를 full comparable run처럼 오해하는 경우

대응:
- execution guide의 comparable 조건을 그대로 gate로 둔다.
- smoke/probe/full comparable의 역할을 문서에서 분리해 둔다.
- 완료 판정은 full comparable run에서만 내린다.

## 12. 완료 정의

이번 문서의 1단계는 아래 조건을 모두 만족할 때 완료로 본다.

- 현재 활성 binding perf가 canonical public pattern / `--recv` surface를 갖는다.
- 현재 활성 binding perf에서 direct native API 의존이 제거되거나 private
  boundary 안으로 축소된다.
- single / multi 정책 위반 지점이 제거된다.
- callback hot path가 policy가 요구한 구조와 일치한다.
- `STREAM` / `SPOT` dual-mode가 shadow path 없이 동작한다.
- build, runner smoke, 결과 naming이 정책과 일치한다.
- `core/perf`와 같은 측정 의미를 유지한 상태에서만 baseline 비교가 수행된다.
- 현재 활성 binding perf 트리의 direct native API grep 결과가 0건이다.
- execution guide 기준 comparable run을 바로 시작할 수 있는 상태다.
- perf 코드가 해당 언어 binding 스타일로 읽히고, native 개념 재노출이 남지 않는다.

이번 문서의 2단계는 아래 조건까지 추가로 만족할 때 완료로 본다.

- binding before/after snapshot 대비 설명 없는 throughput/latency 회귀가 없다.
- execution guide 기준 full comparable run 결과가 각 binding의
  `bindings/<lang>/perf/results/` 아래 남아 있다.

## 13. 현재 구현 배치의 구체 작업 목록

현재 배치는 아래만 한다.

- 현재 활성 binding에서 public surface shadow path 제거
- 현재 활성 binding에서 direct native API 제거
- 현재 활성 binding에서 single/multi 정책 위반 수정
- 현재 활성 binding에서 callback hot path 구조 정렬
- 같은 원칙을 다음 binding으로 확장할 준비

이 범위를 넘는 baseline ratio 확인과 성능 미세조정은 후행 단계로 미룬다.
이유는, 지금은 "속도 확인"보다 "같은 의미의 benchmark 구현 완료"가 먼저이기
때문이다.
