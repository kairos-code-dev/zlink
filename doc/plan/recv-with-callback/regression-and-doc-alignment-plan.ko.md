# `recv-with-callback` 회귀 및 문서 정렬 계획

> 범위:
> [`doc/`](/home/hep7/project/kairos/zlink/doc),
> [`core/tests/`](/home/hep7/project/kairos/zlink/core/tests),
> [`doc/plan/callback-to-recv/`](/home/hep7/project/kairos/zlink/doc/plan/callback-to-recv)

## 1. 목적

이번 작업은 코드 변경만으로 끝나면 안 된다. 현재 저장소에는 callback 축소를
전제로 한 테스트, plan 문서, perf 정책 문서가 함께 남아 있다. 이 계획의 목적은
그 흔적을 새 방향과 충돌하지 않게 정리하는 것이다.

핵심 원칙:

- 예전 축소 방향을 숨기지 않는다.
- 하지만 현재 의사결정의 source of truth는 `recv-with-callback` 디렉터리로
  옮긴다.
- 회귀는 "옛 정책을 계속 강제하는 실패 테스트"가 아니라 "새 규칙이 깨졌는지"
  를 확인하는 테스트로 바꾼다.

## 2. 기존 문서 처리 원칙

### 2.1 `callback-to-recv` 문서

기존 [`doc/plan/callback-to-recv/`](/home/hep7/project/kairos/zlink/doc/plan/callback-to-recv)
문서는 historical context로 남겨 둘 수 있다. 다만 아래 정리가 필요하다.

- `README.ko.md` 상단에 superseded 표기를 넣는다.
- 새 canonical 방향이 [`doc/plan/recv-with-callback/`](/home/hep7/project/kairos/zlink/doc/plan/recv-with-callback)
  라는 점을 명시한다.
- 구현자가 옛 문서를 보고 축소 방향으로 다시 코드를 밀지 않도록 주석을 추가한다.

### 2.2 새 문서 디렉터리 역할

새 디렉터리는 아래 역할을 가진다.

- implementation baseline
- test rewrite baseline
- API/doc/perf alignment baseline

즉 구현 전 참조 문서는 `callback-to-recv`가 아니라 `recv-with-callback`이 된다.

## 3. 테스트 재작성 순서

### 3.1 1단계: 정책 실패 회귀 제거

우선 아래 테스트에서 "attach should fail" 전제를 뒤집는다.

- [`core/tests/integration/test_socket_with_handler.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_socket_with_handler.cpp)
  - raw socket support matrix
- [`core/tests/integration/discovery/test_gateway_with_handler.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/discovery/test_gateway_with_handler.cpp)
  - gateway callback receive/send-ready contract
- [`core/tests/unittest/unittest_service_mode_policy.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_service_mode_policy.cpp)
  - recv-only / callback prerequisite 전제

전환 규칙:

- `ENOTSUP` 기대는 success 회귀로 바꾼다.
- 단순히 기대값만 바꾸지 말고, 실제 attach 후 `EBUSY` 규칙과 parity를 같이
  검증한다.
- 예전 fail-fast 회귀 이름도 새 의미에 맞게 바꾼다.

### 3.2 2단계: 공통 규칙 회귀 추가

family별 성공 회귀를 추가한 뒤 아래 공통 회귀를 묶어서 넣는다.

- receive callback attach 후 recv 계열 `EBUSY`
- receive callback attach 후 `POLLIN` `EBUSY`
- send-ready attach 후 `POLLOUT` `EBUSY`
- receive callback 없이 send-ready만 attach했을 때 recv 계열은 계속 동작
- send-ready 없이 receive callback만 attach했을 때 send path와 non-`POLLOUT`
  surface는 계속 동작
- receive callback과 send-ready를 함께 attach했을 때 각 축의 차단 규칙이
  독립적으로 유지
- callback 미사용 시 recv/poller 기존 동작 유지
- send-ready 미사용 시 `POLLOUT` 유지
- callback payload shape와 recv payload shape parity

### 3.3 3단계: monitor 및 mixed-mode 회귀

monitor는 기존 dual-mode 유지 대상이므로 아래를 별도로 확인한다.

- socket monitor handler/recv parity 유지
- service monitor handler/recv parity 유지
- callback receive family에서도 monitor contract는 변하지 않음
- single callback lane과 multi recv lane에서 monitor start gate가 계속 동작

## 4. 문서 정렬 순서

### 4.1 API 문서

우선순위가 높은 문서:

- `doc/spec/core/gateway.*`
- `doc/spec/core/monitoring.*`
- `doc/spec/core/spot.*`
- `doc/guide/03-5-stream.*`
- `doc/guide/07-2-gateway.*`
- `doc/guide/07-3-spot.*`

반영할 내용:

- receive callback attach 후 recv/pollin 차단
- send-ready attach 후 pollout 차단
- family별 예외 나열보다 규칙 중심 설명
- `gateway`도 callback receive/send-ready 공통 규칙에 들어온다는 점

### 4.2 perf 문서

우선순위가 높은 문서:

- `core/perf/README.md`
- `core/perf/README_KO.md`
- `doc/perf/*`

반영할 내용:

- single callback only
- multi recv only
- single=`SPOT`, multi=`SPOT`/`STREAM`만 dual-mode 예외
- monitor는 pattern이 아니라 gate이며 callback 기준
- callback 복원과 canonical perf lane은 별개

### 4.3 plan 문서

plan 문서는 아래 순서로 정리한다.

1. `recv-with-callback` 문서를 먼저 작성해 baseline을 만든다.
2. `callback-to-recv`에는 superseded/historical 성격을 표시한다.
3. 관련 하위 plan에서 callback 축소를 전제로 단정한 문구가 있으면 링크 또는
   한 줄 주석으로 현재 baseline을 안내한다.

## 5. 구현 순서 권장안

문서/테스트/코드를 동시에 건드릴 때는 아래 순서를 권장한다.

1. 새 plan 문서 작성
2. public header 주석 및 API gate 변경
3. socket/service 내부 gate 일반화
4. unit/integration 회귀 뒤집기
5. perf runner/policy 정렬
6. API/guide/perf 문서 정리

이 순서를 쓰는 이유:

- 문서 없는 구현 변경을 막는다.
- old policy 테스트가 새 코드 변경을 계속 깨는 구간을 최소화한다.
- perf를 나중에 정리해도 core contract는 먼저 고정할 수 있다.

## 6. 완료 기준

아래가 모두 만족되면 저장소 정렬 완료로 본다.

1. `recv-with-callback`이 새 source of truth로 읽힌다.
2. `callback-to-recv`가 현재 구현 기준 문서처럼 오해되지 않는다.
3. `core/tests/`에 callback 축소를 강제하는 성공/실패 회귀가 남아 있지 않다.
4. API/guide/perf 문서가 새 공통 규칙과 canonical lane을 반영한다.
5. 구현자가 문서와 테스트만 읽어도 attach 후 `EBUSY` 규칙을 유추할 수 있다.
