# 수신 모델 개편 3차 구현 / 감독 계획

> 성격: 이 문서는 구현 로그가 아니라, 현재 main Codex가 **감독(manager)** 으로서
> 하위 Codex 에이전트에게 작업을 순차 분배하고, 결과를 직접 리뷰한 뒤,
> 미구현 항목과 잘못된 구현이 남아 있으면 다시 작업을 지시하는
> **감독용 실행 기준 문서**다.
>
> 상태: active
> 대상 범위: `core/`, `core/tests/`, `core/perf/`, `doc/spec/draft/`,
> `doc/plan/direct-callback-recv/`
> 최종 목표: 아래 3개 draft 기능을 순서대로 구현하고, 각 기능마다 미구현 항목이
> 없다고 감독 리뷰로 판정된 뒤 POSD 기반 리팩토링까지 끝낸 다음 다음 기능으로
> 넘어간다. 3차까지 모두 끝난 뒤에는 `doc/perf/` 정책 문서와 실제 perf 구현이
> 완전히 맞을 때까지 perf를 다시 정렬한다.
> 이 문서는 위 최종 완료 정의를 만족할 때까지 작업이 **중간에 멈추지 않도록**
> 실행 순서와 재작업 규칙을 고정한다.

## 1. 문서 목적

이 문서는 아래 3개 draft를 실제 코드에 반영하는 **실행 순서**와
**완료 판정 방식**을 고정한다.

설계 authority는 아래 draft 문서들이다.
이 문서는 새 설계를 제안하지 않는다.

- [`doc/spec/draft/socket-receive-model-revision.ko.md`](/home/hep7/project/kairos/zlink/doc/spec/draft/socket-receive-model-revision.ko.md)
- [`doc/spec/draft/stream-packet-handler.ko.md`](/home/hep7/project/kairos/zlink/doc/spec/draft/stream-packet-handler.ko.md)
- [`doc/spec/draft/pollout-recovery-semantics.ko.md`](/home/hep7/project/kairos/zlink/doc/spec/draft/pollout-recovery-semantics.ko.md)

성능 authority는 아래 문서들이다.

- [`doc/perf/PERF_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_POLICY.md)
- [`doc/perf/PERF_SINGLE_TEST_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_SINGLE_TEST_POLICY.md)
- [`doc/perf/PERF_MULTI_TEST_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_MULTI_TEST_POLICY.md)

설계 판단이 흔들리면 먼저 draft를 고치고 그 다음 코드를 수정한다.
코드만 먼저 바꾸고 authority와 불일치를 남기지 않는다.

## 2. 감독 원칙

- main Codex는 감독 역할만 수행한다.
- 실제 코드 수정은 하위 Codex 에이전트가 담당한다.
- 하위 에이전트가 "완료"라고 보고해도 감독은 직접 파일, 테스트, perf 결과를 읽고
  다시 리뷰한다.
- 감독 리뷰에서 미구현, 잘못된 동작, 불필요한 우회, 문서 불일치, 성능 저하 징후가
  발견되면 완료 처리하지 않고 다시 하위 에이전트에게 작업을 지시한다.
- 기능 하나가 끝났다고 보는 기준은 "빌드가 된다"가 아니라,
  **감독 리뷰 기준으로 미구현 항목이 0건**이고 **추가 POSD 리팩토링도 더 이상
  의미 있게 남아 있지 않다**는 판정이 나왔을 때다.
- 성능이 떨어지면 안 된다. correctness와 contract 정렬을 우선하되, perf 회귀가
  보이면 원인을 추적하고 제거한다.

## 3. 중단 금지 규칙

아래 경우가 아니면 작업을 멈추지 않는다.

- authority 문서만으로는 해결할 수 없는 C API / ABI 계약 충돌
- 사용자 변경과 직접 충돌하는 워크트리 변경 발견
- 현재 저장소 범위만으로 해결할 수 없는 외부 blocker

위 경우가 아니면 아래 규칙을 강제로 따른다.

1. 현재 차수의 첫 미완료 항목을 잡는다.
2. 하위 Codex 에이전트에게 구현 또는 재작업을 지시한다.
3. 코드 수정, 테스트, 필요한 perf 검증을 수행한다.
4. 감독이 직접 리뷰한다.
5. 미구현 항목, 잘못된 구현, POSD 잔여, perf 정책 불일치가 하나라도 남아 있으면
   같은 차수 안에서 즉시 다시 작업을 지시한다.
6. 현재 차수의 완료 조건을 만족하기 전에는 다음 차수로 넘어가지 않는다.
7. 3차까지 모두 끝난 뒤에도 `doc/perf/` 정책 불일치가 남아 있으면 최종 완료로
   보지 않고 perf 정렬 단계를 계속 진행한다.

즉 이 문서는 "부분 완료", "중간 보류", "다음에 이어서"를 기본 상태로 두지 않는다.
최종 완료 정의를 만족하기 전에는 항상 현재 미완료 항목을 기준으로 같은 루프를
계속 돈다.

## 4. 고정 작업 순서

작업 순서는 아래 3차로 고정한다.
앞 차수가 감독 리뷰 기준으로 완전히 종료되기 전에는 다음 차수로 넘어가지 않는다.

1. `socket-receive-model-revision`
2. `stream-packet-handler`
3. `pollout-recovery-semantics`

이 순서를 고정하는 이유는 아래와 같다.

- 1차는 public receive surface의 support matrix를 먼저 정리하는 작업이다.
- 2차는 `STREAM` 전용 상위 수신 모드를 추가하는 작업이라 1차의 mode gate 정리가
  끝난 뒤 들어가는 것이 자연스럽다.
- 3차는 `POLLOUT` 의미 재정의로 영향 범위가 가장 넓고 perf와 readiness 계약에
  직접 닿으므로 마지막에 처리하는 것이 안전하다.

## 5. 1개 기능당 공통 완료 루프

모든 기능은 아래 **6단계 완료 루프**를 공통으로 사용한다.

1. 하위 Codex 에이전트에게 현재 기능의 authority 문서와 범위를 주고 구현을 지시한다.
2. 하위 에이전트가 코드 수정, 테스트 추가/수정, 필요한 회귀 검증을 수행한다.
3. 감독이 직접 리뷰한다.
4. 미구현 항목이나 잘못된 구현이 있으면 다시 하위 에이전트에게 작업을 지시한다.
   감독 리뷰 기준으로 미구현 항목이 0건이 될 때까지 반복한다.
5. 미구현 항목이 0건이 되면 POSD 기반 리팩토링을 지시한다.
6. 감독 리뷰에서 더 이상 진행할 POSD 기반 리팩토링이 없다고 판정될 때까지
   5단계를 반복한다.

이 루프는 현재 기능의 완료 조건을 만족할 때까지 끊지 않는다.
즉 아래 상태에서는 라운드를 종료하지 않는다.

- "대체로 맞지만 몇 개 남음"
- "테스트는 되지만 문서 contract와 조금 다름"
- "POSD는 다음 차수에서 보자"
- "perf 정책 정렬은 마지막에 한 번에 하자"라고 하면서 현재 차수의 구조 문제를
  남기는 경우

현재 차수에서 잡힌 문제는 현재 차수 안에서 닫는다.

즉 각 기능은 아래 순서를 반드시 지킨다.

- 기능 구현
- 감독 리뷰
- 미구현 0건 확인
- POSD 리팩토링
- 반복 리뷰
- POSD 잔여 0건 확인

이 절차를 통과한 뒤에만 다음 기능으로 넘어간다.

## 6. 감독 진행표

상태 값은 아래 다섯 개만 쓴다.

- `미착수`
- `진행중`
- `재작업`
- `검증중`
- `완료`

| 단계 | 기능 | 1차 구현 | 감독 리뷰 | POSD 리팩토링 | POSD 반복 리뷰 | 최종 상태 |
| --- | --- | --- | --- | --- | --- | --- |
| 1차 | receive model revision | 미착수 | 미착수 | 미착수 | 미착수 | 미착수 |
| 2차 | stream packet handler | 미착수 | 미착수 | 미착수 | 미착수 | 미착수 |
| 3차 | pollout recovery semantics | 미착수 | 미착수 | 미착수 | 미착수 | 미착수 |
| 최종 | perf 정책 정렬 | 미착수 | 미착수 | 해당 없음 | 해당 없음 | 미착수 |

## 7. 단계별 authority와 완료 조건

### 7.1 1차: receive model revision

authority:

- [`socket-receive-model-revision.ko.md`](/home/hep7/project/kairos/zlink/doc/spec/draft/socket-receive-model-revision.ko.md)

핵심 범위:

- raw `PAIR`, `DEALER`, `SUB`, `XSUB`를 recv-only로 정리
- raw `ROUTER` direct receive callback 제거
- `ROUTER request` completion callback 유지
- `STREAM`은 recv + raw callback 유지
- `SPOT`은 direct message callback 제거, dispatch-event + recv drain 방향 정렬
- `zlink_subscribe_handler()` 제거
- `zlink_router_handler()` 제거
- `zlink_recv_handler()`를 `STREAM` 전용으로 축소

1차 구현 완료 기준:

- public header와 구현에서 지원 matrix가 draft와 일치한다.
- 미지원 subject는 숨은 compatibility 없이 명시된 오류로 닫힌다.
- 관련 unit/integration 회귀가 새 support matrix 기준으로 정리된다.
- 감독 리뷰 기준으로 미구현 항목이 없다.

1차 POSD 리뷰 기준:

- callback 제거 후 남은 dead branch, dead state, dead helper가 정리되어 있다.
- recv-only family에 callback 전환 흔적이 남아 있지 않다.
- `ROUTER`에서 data-plane receive와 request completion 의미가 코드 구조로도
  분리되어 있다.
- `SPOT` 쪽 direct message callback 우회 경로가 남아 있지 않다.

### 7.2 2차: stream packet handler

authority:

- [`stream-packet-handler.ko.md`](/home/hep7/project/kairos/zlink/doc/spec/draft/stream-packet-handler.ko.md)

핵심 범위:

- `zlink_stream_packet_handler()` 추가
- `STREAM`의 3모드 gate 정리
  - raw recv
  - raw callback
  - packet callback
- `2 bytes header size + 4 bytes body size` framing parser 구현
- `header/body zlink_msg_t` 직접 조립
- callback delivery 직전 추가 복사 금지
- `source_rid_` 포함
- 같은 client `source_rid_` 기준 직렬화
- malformed packet 처리와 monitor 관찰 경로 정리

2차 구현 완료 기준:

- packet callback 경로가 draft의 framing, ownership, mode gate, malformed 정책과
  일치한다.
- 같은 `source_rid_`에서는 callback이 겹치지 않고 순서가 유지된다.
- 다른 `source_rid_`는 socket 전체 직렬화를 강제하지 않는다.
- callback delivery 직전의 추가 복사가 없다.
- 감독 리뷰 기준으로 미구현 항목이 없다.

2차 POSD 리뷰 기준:

- parser state, connection state, callback dispatch 책임이 과하게 섞여 있지 않다.
- `STREAM` raw callback과 packet callback이 불필요한 얕은 wrapper를 공유하지 않는다.
- 연결별 state 정리와 close 경로가 설명 가능한 깊이로 정리되어 있다.

### 7.3 3차: pollout recovery semantics

authority:

- [`pollout-recovery-semantics.ko.md`](/home/hep7/project/kairos/zlink/doc/spec/draft/pollout-recovery-semantics.ko.md)

핵심 범위:

- `zlink_send_ready_handler()` 유지
- `ZLINK_POLLOUT`을 recovery readiness 의미로 재정렬
- `BACKPRESSURED` 이후 poller 경로가 callback 경로보다 더 불리하지 않게 정리
- `send_ready_handler`와 `POLLOUT`의 의미를 같은 축으로 맞춤
- registration 시점 경쟁 상태에서 wakeup을 놓치지 않는 방향으로 정리

3차 구현 완료 기준:

- `POLLOUT`과 `send_ready_handler`가 같은 recovery 의미를 공유한다.
- `BACKPRESSURED` 이후 재시도 시점 관찰이 poller에서도 자연스럽다.
- readiness 알림 뒤 재시도 실패가 허용된다는 점이 코드와 테스트에 반영되어 있다.
- 감독 리뷰 기준으로 미구현 항목이 없다.

3차 POSD 리뷰 기준:

- readiness 상태 계산이 타입별 우회 분기로 흩어져 있지 않다.
- poller와 callback이 서로 다른 state machine을 노출하지 않는다.
- service mode / socket base / poller 경로의 중복 규칙이 줄어들어 있다.

## 8. 감독 리뷰 체크리스트

감독은 각 라운드에서 아래 항목을 직접 확인한다.

### 8.1 구현 정확성

- authority draft의 핵심 계약이 실제 코드에 반영되었는가
- 누락된 API gate, 오류 코드, lifetime 규칙, close 규칙이 없는가
- 테스트가 변경 범위를 실제로 커버하는가
- 문서에 없는 hidden compatibility나 silent fallback이 들어가지 않았는가

### 8.2 구조 / POSD

- 얕은 wrapper가 불필요하게 늘지 않았는가
- dead branch, dead state, dead helper, dead file이 남아 있지 않은가
- 같은 정책을 여러 모듈이 중복해서 갖지 않는가
- lifecycle, ownership, thread 문맥이 더 깊고 짧게 설명 가능한 구조가 되었는가

### 8.3 성능 / 회귀

- hot path에 불필요한 heap alloc, 문자열 생성, 추가 복사, sleep, retry loop가
  들어가지 않았는가
- 새로운 mode gate나 readiness 계산이 데이터 평면 hot path를 불필요하게 느리게
  만들지 않았는가
- 기존 perf 민감 경로에 lock, polling, extra branch가 의미 있게 늘지 않았는가

감독은 위 세 축 중 하나라도 미달이면 완료 처리하지 않는다.

## 9. 기능 완료 후 perf 정렬 규칙

3차까지 모두 완료되면 마지막 단계로 `doc/perf/` 정책과 실제 perf 구현을 다시
대조한다.

대상 authority:

- [`doc/perf/PERF_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_POLICY.md)
- [`doc/perf/PERF_SINGLE_TEST_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_SINGLE_TEST_POLICY.md)
- [`doc/perf/PERF_MULTI_TEST_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_MULTI_TEST_POLICY.md)

이 단계는 단순 perf 실행이 아니라 **정책 동기화 단계**로 본다.

고정 순서:

1. 현재 `core/perf` 구현이 위 정책 문서의 모든 항목을 만족하는지 감독이 직접 리뷰한다.
2. 불일치가 발견되면 하위 Codex 에이전트에게 수정 지시를 내린다.
3. 수정 후 감독이 다시 리뷰한다.
4. 정책 위반이 더 이상 없을 때까지 반복한다.

이 단계도 중간에 멈추지 않는다.
즉 perf 스크립트가 한 번 통과했다는 이유만으로 종료하지 않는다.
정책 문서와 코드의 의미가 어긋나 있으면 같은 단계 안에서 계속 수정과 리뷰를
반복한다.

이 단계에서 특히 확인할 항목:

- recv-only 정책과 실제 perf 수신 경로가 일치하는가
- callback 경로를 perf에서 측정하지 않는다는 정책이 실제 코드에서 지켜지는가
- single / multi의 poller `POLLIN` / `POLLOUT` 사용 규칙이 정책과 일치하는가
- ready gate, active 집계, RESULT 의미, fail/partial/complete 의미가 정책과
  정확히 일치하는가
- `STREAM`과 `SPOT` 관련 perf 코드가 이번 3개 기능 변경 후에도 정책을 깨지 않는가

perf 단계 완료 기준:

- `doc/perf/`의 모든 관련 정책 항목에 대해 감독 리뷰 기준 불일치가 0건이다.
- 정책 위반을 감추는 우회 코드가 없다.
- 성능 저하가 발견되면 원인 수정까지 끝났다.

## 10. 기본 검증 명령

공통 빌드 / 테스트:

```bash
cmake --build core/build -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -L integration -j1
```

perf 확인 기준 명령:

```bash
source /home/hep7/project/kairos/zlink/.venv-bindings/bin/activate
/home/hep7/project/kairos/zlink/core/perf/run_benchmarks.sh --msg-sizes 64
/home/hep7/project/kairos/zlink/core/perf/run_benchmarks_multi.sh --msg-sizes 64
```

주의:

- single과 multi perf 스크립트는 같은 `core/build`를 공유하므로 병렬 실행하지 않는다.
- perf 실패가 발생하면 단순 재시도로 넘기지 않고 원인을 추적해 수정한다.
- perf 성공만으로 완료 처리하지 않는다. `doc/perf/` 정책 일치까지 확인해야 한다.

## 11. 하위 Codex 에이전트 작업 지시 원칙

감독은 하위 Codex 에이전트에게 아래 형식으로 지시한다.

- 현재 단계 authority 문서
- 이번 라운드 범위
- 반드시 수정해야 할 미구현 / 불일치 항목 목록
- 필요한 테스트 / perf 검증 항목
- 성능 저하 금지 규칙

하위 에이전트는 아래 규칙을 따라야 한다.

- 먼저 미구현 / 불일치 항목 목록을 명시적으로 식별한다.
- 코드 수정과 회귀 검증을 함께 수행한다.
- "대체로 맞다"가 아니라 지적된 항목을 0건으로 만드는 방향으로 수정한다.
- POSD 리팩토링 단계에서는 구조 단순화와 hot path 비용을 함께 고려한다.

## 12. 라운드 기록 규칙

감독과 하위 에이전트의 각 왕복은 라운드 단위로 남긴다.
이 기록은 같은 이슈를 반복 탐색하지 않고, 중간 중단 없이 다음 재작업으로
이어가기 위한 최소 작업 로그다.

각 라운드마다 아래 항목을 반드시 남긴다.

- 단계:
  `1차 receive model revision` | `2차 stream packet handler` |
  `3차 pollout recovery semantics` | `최종 perf 정책 정렬`
- 라운드 번호
- 이번 라운드 지시 범위
- 발견한 미구현 / 불일치 / POSD / perf 정책 위반 항목 목록
- 하위 Codex 에이전트가 실제로 수정한 파일 목록
- 실행한 테스트 / perf 검증 명령
- 감독 리뷰 결과
- 다음 라운드 추가 지시사항

라운드 기록 규칙:

- 감독 리뷰에서 "추가 지시 없음"이 나오기 전까지 라운드를 종료로 보지 않는다.
- 미구현 항목이 남아 있으면 다음 라운드 기록의 첫 줄에 그대로 이어서 적는다.
- POSD 리팩토링 단계도 별도 라운드로 기록한다.
- perf 정책 정렬 단계도 별도 라운드로 기록한다.

권장 템플릿:

```text
[라운드 기록]
- 단계:
- 라운드:
- 지시 범위:
- 발견한 항목:
- 수정 파일:
- 검증:
- 감독 리뷰 결과:
- 다음 지시:
```

## 13. 라운드 종료 금지 규칙

감독과 하위 에이전트는 아래 상태에서 라운드를 종료하거나 다음 단계로 넘기지
않는다.

- 감독 리뷰에서 미구현 항목이 1건 이상 남아 있는 상태
- 감독 리뷰에서 POSD 리팩토링 후보가 1건 이상 남아 있는 상태
- 테스트는 통과했지만 authority 문서와 코드 의미가 어긋나는 상태
- perf가 통과했지만 `doc/perf/` 정책과 의미가 어긋나는 상태
- 성능 회귀 원인이 확인되었지만 수정하지 않은 상태

즉 종료 가능한 상태는 아래 둘뿐이다.

- 현재 차수 완료
- 전체 계획 완료

그 외 상태는 모두 `재작업` 또는 `검증중`으로 되돌린다.

## 14. 최종 완료 정의

이번 3차 묶음 작업은 아래를 모두 만족해야 끝난다.

- 1차 `socket-receive-model-revision`이 감독 리뷰 기준 완료다.
- 2차 `stream-packet-handler`가 감독 리뷰 기준 완료다.
- 3차 `pollout-recovery-semantics`가 감독 리뷰 기준 완료다.
- 각 차수마다 미구현 항목이 0건인 상태를 만든 뒤 POSD 기반 리팩토링과 반복
  리뷰까지 끝냈다.
- 각 차수마다 성능 저하를 허용하지 않았다.
- 마지막 perf 정렬 단계에서 `doc/perf/` 정책 문서의 모든 관련 항목이 실제 구현과
  일치한다.
- 감독 리뷰 기준으로 더 이상 의미 있는 POSD 리팩토링 항목도, perf 정책 위반도
  남아 있지 않다.

완료 판정은 하위 에이전트의 자기신고가 아니라 감독 리뷰 기준으로만 내린다.
