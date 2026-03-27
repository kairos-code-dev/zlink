# C++ Socket Surface 구현 Ralph Guide

> 상태: 완료
> 대상 범위: `bindings/cpp/include/zlink/**`, `bindings/cpp/samples/**`,
> `bindings/cpp/tests/contract/**`, `bindings/cpp/plan/socket/**`
> 단일 authority: 이 문서
> 설계 입력: `2026-03-26-cpp-socket-surface-detailed-design.ko.md`
> 최종 종료 판정: `미적용 사항이 없습니다.`

## 1. 목적

이 가이드는 `bindings/cpp` socket 계층을 concrete facade 기반 구조로 끝까지
전환하기 위한 단일 실행 문서다.

이번 루프의 목표는 아래를 모두 만족하는 것이다.

- `pair_socket_t`, `dealer_socket_t`, `router_socket_t`, `stream_socket_t`,
  `pub_socket_t`, `sub_socket_t`, `xpub_socket_t`, `xsub_socket_t`가
  실제 사용자-facing facade로 동작할 것
- `samples/` 와 `tests/contract/` 가 generic `socket_t(context_t&, socket_type)`
  대신 concrete facade를 기본값으로 사용할 것
- `socket.hpp` 는 즉시 삭제하지 않되 compat/umbrella 방향으로 축소될 것
- POSD 기준 리팩토링을 마지막 단계로 수행하고 더 이상 리팩토링 대상이 없을 때
  종료할 것
- `build.sh ON ON`, `ctest -L contract`, `ctest -L sample-smoke` 가 녹색일 것

## 2. 입력 문서와 사용 규칙

이 Ralph Guide가 유일한 실행 authority다.

설계 입력 문서:

- [`2026-03-26-cpp-socket-surface-detailed-design.ko.md`](./2026-03-26-cpp-socket-surface-detailed-design.ko.md)

규칙:

- 설계 세부가 바뀌면 먼저 이 guide와 설계 입력 문서를 같이 갱신한다.
- guide 갱신 없이 코드만 바꿔서 설계 불일치를 남기지 않는다.
- `bindings/cpp` 밖은 직접 범위가 아니다. 필요 시 사용자 확인 전 확장하지 않는다.

## 3. 중단 규칙

아래 경우가 아니면 멈추지 않는다.

- `bindings/cpp` 밖 변경이 없으면 풀 수 없는 blocker
- 최신 `core/include/zlink.h` 와 현재 C++ facade 설계가 직접 충돌하는 경우
- 사용자 작업과 직접 충돌하는 워크트리 변경 발견

그 외에는:

1. 첫 미완료 작업 묶음을 집는다.
2. 코드 수정과 샘플/contract test 이행을 같이 한다.
3. 빌드와 검증을 돌린다.
4. 작업 레지스터를 갱신한다.
5. 다음 미완료 항목으로 바로 이동한다.

## 4. 기본 검증 명령

항상 아래 경로만 사용한다.

```bash
./bindings/cpp/build.sh ON ON

ctest --test-dir core/build --output-on-failure -L contract
ctest --test-dir core/build --output-on-failure -L sample-smoke -j1
```

필요 시 개별 샘플/contract smoke:

```bash
./core/build/bindings/cpp/sample_cpp_pair_recv
./core/build/bindings/cpp/sample_cpp_pubsub_recv
./core/build/bindings/cpp/sample_cpp_dealer_router_recv
./core/build/bindings/cpp/sample_cpp_stream_recv
./core/build/bindings/cpp/test_cpp_contract_socket
./core/build/bindings/cpp/test_cpp_contract_options
./core/build/bindings/cpp/test_cpp_contract_monitor
```

## 5. 작업 레지스터

상태 값:

- `미착수`
- `진행중`
- `검증중`
- `완료`

### 5.1 facade 헤더 구조

상태: `완료`

대상:

- `include/zlink/socket_handle.hpp`
- `include/zlink/base_socket.hpp`
- `include/zlink/message_socket.hpp`
- `include/zlink/publisher_socket.hpp`
- `include/zlink/subscriber_socket.hpp`
- `include/zlink/socket_types.hpp`
- `include/zlink.hpp`

목표:

- concrete facade 헤더를 정식 public include로 노출
- 공통 lifecycle/option/monitor helper는 `base_socket_t`에 유지
- 타입별 option 재노출은 concrete facade에서 처리

완료 조건:

- 새 facade 헤더가 모두 빌드됨
- `zlink.hpp` 포함만으로 concrete facade를 사용할 수 있음

진행 메모:

- facade 헤더는 추가됐다.
- `zlink.hpp` 포함만으로 concrete facade 전체를 사용할 수 있다.
- 공통 lifecycle/option/monitor helper는 `base_socket_t`에 유지한다.
- `pub/xpub`, `sub/xsub`의 타입별 option 위임 중복은 공통 facade base로 접었다.

### 5.2 샘플 이행

상태: `완료`

대상:

- `samples/pair/**`
- `samples/pubsub/**`
- `samples/dealer_router/**`
- `samples/stream/**`
- `samples/spot/**`

목표:

- raw socket 샘플은 concrete facade만 사용
- `pubsub` 샘플은 `xpub_socket_t` / `sub_socket_t` 조합으로 고정
- `spot` 샘플은 service layer라 이번 facade 리팩터링의 회귀 없이 유지

완료 조건:

- 전체 `sample-smoke` 통과
- raw socket 샘플에서 generic `socket_t(context_t&, socket_type)` 직접 사용 없음

진행 메모:

- `pair/pubsub/dealer_router/stream` 샘플은 concrete facade 기준으로 이동했다.
- `spot` 샘플은 service layer 그대로 유지한다.
- `ctest --test-dir core/build --output-on-failure -L sample-smoke -j1` 녹색을 확인했다.

### 5.3 contract test 이행

상태: `완료`

대상:

- `tests/contract/test_cpp_contract_socket.cpp`
- `tests/contract/test_cpp_contract_options.cpp`
- `tests/contract/test_cpp_contract_monitor.cpp`
- 나머지 contract test 전체 build/green 유지

목표:

- facade 리팩터링 관련 contract test는 concrete facade 사용
- option/monitor/socket contract가 generic constructor 없이도 유지됨을 증명

완료 조건:

- `ctest -L contract` 전체 통과
- contract test에서 generic `socket_t(context_t&, socket_type)` 직접 사용 없음

진행 메모:

- `socket/options/monitor` contract는 concrete facade 기준으로 전환했다.
- 나머지 contract test는 이번 변경과 직접 무관하지만 회귀 없이 통과해야 한다.
- `ctest --test-dir core/build --output-on-failure -L contract`를 단독 실행으로
  녹색 확인했다.

### 5.4 compat 축소

상태: `완료`

대상:

- `include/zlink/socket.hpp`
- `include/zlink/compat.hpp`
- 필요 시 `doc/bindings/cpp*.md`

목표:

- `socket.hpp` 를 compat/umbrella 방향으로 축소
- 새 코드의 기본 진입점은 concrete facade가 되도록 정리

완료 조건:

- `socket.hpp` 가 더 이상 새 public 설계의 중심 구현체가 아님
- 새 샘플과 contract test는 `socket.hpp`의 generic constructor에 의존하지 않음

진행 메모:

- 샘플과 contract test에서 generic `socket_t(context_t&, socket_type)` 직접 사용은
  제거됐다.
- `socket.hpp`의 public generic constructor는 deprecated compat 경로로 명시했다.
- `socket_t` 자체는 `socket_handle_t` 내부 구현과 기존 사용자 호환을 위해 유지한다.

### 5.5 종료 정리

상태: `완료`

목표:

- 남은 generic `socket_t(context_t&, socket_type)` 직접 사용 위치 재점검
- 설계 입력 문서와 실제 구현 상태 불일치 제거
- 최종 검증 로그가 녹색인 상태에서 종료 판정

완료 조건:

- 이 guide의 모든 항목이 `완료`
- 최종 메시지를 정확히 `미적용 사항이 없습니다.` 로 낼 수 있음

진행 메모:

- `bindings/cpp/tests/test_cpp_core_spec_router.cpp` 와 `API_DRAFT.md`에는 generic
  `socket_t(context_t&, socket_type)` 언급이 남아 있다.
- 현재 실행 범위의 필수 이행 대상인 `samples/**`, `tests/contract/**`, public facade
  헤더는 concrete facade 기준으로 전환됐다.
- 설계 입력 문서와 현재 public facade 구조 사이에 이번 루프 범위의 불일치는 없다.
- 최종 검증은 `./bindings/cpp/build.sh ON ON`, `ctest -L contract`,
  `ctest -L sample-smoke -j1` 순서로 녹색 확인했다.

### 5.6 POSD 기반 최종 리팩토링

상태: `완료`

대상:

- `include/zlink/socket.hpp`
- `include/zlink/base_socket.hpp`
- `include/zlink/socket_types.hpp`
- `include/zlink/message_socket.hpp`
- `include/zlink/publisher_socket.hpp`
- `include/zlink/subscriber_socket.hpp`
- 관련 샘플/contract test

목표:

- John Ousterhout POSD 기준으로 마지막 구조 리뷰를 수행
- change amplification, shallow wrapper, hidden coupling, 중복 option 위임,
  의미가 약한 compat 경계가 남아 있으면 정리
- 리팩토링 후에도 public 의미가 더 단순해지고 설명이 더 짧아져야 함

반복 규칙:

1. 현재 구조에서 리팩토링 후보를 1개 이상 찾는다.
2. 후보가 있으면 문서에 기록하고 실제 리팩토링을 수행한다.
3. `build.sh ON ON`, `ctest -L contract`, `ctest -L sample-smoke`로 검증한다.
4. 다시 POSD 기준으로 리뷰한다.
5. 더 이상 의미 있는 리팩토링 후보가 없을 때만 `완료`로 바꾼다.

완료 조건:

- facade 계층 설명이 몇 문장 안에 끝난다.
- 새 facade가 단순 전달자 집합이 아니라 의미 있는 깊은 모듈 경계를 이룬다.
- 중복 위임/호환 레이어/불필요한 API 중첩이 더 이상 남아 있지 않다.
- 더 진행해도 복잡도 감소보다 churn이 커지는 상태라고 문서화할 수 있다.
- 검증 명령이 모두 녹색이다.

진행 메모:

- 리팩토링 후보로 `pub/xpub`, `sub/xsub`의 중복 option 위임을 식별했다.
- 공통 facade base로 접어 change amplification과 얕은 중복 wrapper를 줄였다.
- 그 외 구조는 `socket_t`를 compat 경로로 남기면서 `socket_handle_t` 내부 구현을
  유지하는 편이 churn 대비 복잡도 감소가 작다고 판단했다.
- 현재 facade 설명은 `base_socket_t`가 공통 lifecycle/option/monitor를 맡고,
  `message/publisher/subscriber` 계층이 data-plane 의미를 나누며, concrete facade가
  타입별 option과 추가 기능만 재노출한다고 요약할 수 있다.

## 6. 금지 규칙

- `bindings/cpp/perf/**` 를 이번 루프 범위에 끌어오지 않는다.
- 테스트를 느슨하게 만들어서 빌드를 통과시키지 않는다.
- `socket.hpp` 를 아직 쓰는 사용자 코드를 고려하지 않고 바로 삭제하지 않는다.
- native에 없는 socket type facade를 C++에서 추가하지 않는다.
- `std::function` 기반 callback facade를 이번 루프에 도입하지 않는다.

## 7. 로그 / commit / push 규칙

- 로그는 wrapper 기본 `logs/` 아래에 쌓는다.
- commit / push는 사용자 지시가 있을 때만 한다.
- 루프 중간에 설계가 바뀌면 먼저 문서를 수정한 뒤 코드를 수정한다.

## 8. 종료 조건

아래가 모두 만족되면 종료한다.

- facade 헤더 구조: `완료`
- 샘플 이행: `완료`
- contract test 이행: `완료`
- compat 축소: `완료`
- 종료 정리: `완료`
- POSD 기반 최종 리팩토링: `완료`

그 시점의 최종 응답은 정확히 아래 한 줄이다.

```text
미적용 사항이 없습니다.
```
