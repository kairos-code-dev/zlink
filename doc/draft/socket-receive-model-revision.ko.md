[스펙 목차](../README.ko.md)

# Draft -- Socket Receive Model Revision

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API나 정책을
> 보장하지 않는다.
> 구현과 공개 헤더가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 초안은 raw socket family의 수신 표면을 다시 정리하는 방향을 정의한다.

핵심 목표는 아래와 같다.

- 소켓 타입마다 "어떤 방식으로 수신하는가"를 더 분명하게 만든다.
- recv와 direct callback을 모든 소켓에 대칭으로 두지 않고, 실제 사용 패턴에
  맞게 줄인다.
- 서버 간 통신의 주 사용 경로를 `recv + poller` 중심으로 고정한다.
- `request` 계열의 completion callback과 direct receive callback을 분리해서
  의미를 더 선명하게 만든다.

## 2. 배경

현재 core socket 계층은 여러 타입에서 recv 방식과 direct callback 방식을 함께
지원한다.

- raw `PAIR`, `DEALER`, `STREAM`은 `zlink_recv()`와
  `zlink_recv_handler()`를 함께 지원한다.
- raw `SUB`, `XSUB`은 `zlink_subscribe()`와
  `zlink_subscribe_handler()`를 함께 지원한다.
- raw `ROUTER`는 `zlink_router_recv()`와 `zlink_router_handler()`를 함께
  지원한다.

이 구조는 기능적으로는 넓지만, 사용 모델이 소켓마다 명확하게 보이지 않는 문제가
있다.

- 사용자는 같은 소켓에서 recv와 callback 중 어느 쪽이 기본인지 다시 판단해야
  한다.
- direct callback attach 이후 recv와 poller가 `EBUSY`로 막히는 모드 전환 규칙을
  이해해야 한다.
- `ROUTER`는 direct receive callback과 request completion callback을 동시에
  가져 의미가 섞인다.
- 실전 서버 루프에서는 결국 여러 소켓, monitor, timer를 같은 poller로 다루는
  경우가 많아서 recv 기반 경로가 주 모델이 되기 쉽다.

이 문서는 이 문제를 "타입별 기본 수신 모델을 다시 고정한다"는 방식으로 풀려고
한다.

## 3. 설계 원칙

이번 개정은 아래 원칙을 기준으로 한다.

### 3.1 데이터 수신과 작업 완료를 구분한다

외부에서 들어오는 데이터를 소비하는 일과, 비동기 작업의 완료를 통지하는 일은
같은 종류의 callback으로 취급하지 않는다.

- direct receive callback은 data-plane receive 모델이다.
- `request(..., handler, ...)`의 callback은 async operation completion
  모델이다.

즉 어떤 소켓에서 direct receive callback을 없애더라도, `request` 함수의 reply
 completion callback까지 함께 없애는 것을 뜻하지 않는다.

### 3.2 서버 간 통신의 기본 모델은 recv + poller다

raw socket family의 주 사용 경로는 아래 조합으로 본다.

- poller가 readable/writable 이벤트를 감시한다.
- readable가 오면 recv 계열 함수로 데이터를 직접 가져온다.
- monitor와 timer도 같은 루프에서 함께 처리한다.

이 방식은 아래 이유로 core의 기본 모델에 더 잘 맞는다.

- 여러 소켓을 하나의 event loop에서 함께 다루기 쉽다.
- 스레드 소유권과 실행 순서를 호출자가 직접 통제할 수 있다.
- callback 실행 thread, 재진입, 장시간 handler가 I/O 진행을 막는 문제를
  줄일 수 있다.

### 3.3 예외는 실제 사용 패턴이 분명할 때만 둔다

모든 소켓에 recv와 callback을 기계적으로 대칭 지원하지 않는다.
예외는 실제 사용 패턴이 분명한 경우에만 둔다.

이 초안에서는 `STREAM`, `monitor`, `timer`, `SPOT`이 그 예외에 해당한다.

## 4. 타입별 목표 수신 모델

이 절은 이번 개정에서 각 타입을 어떻게 정리할지 한 번에 보여준다.

| 타입 | direct recv | direct callback | 비고 |
|------|-------------|-----------------|------|
| `PAIR` | 유지 | 제거 | raw message socket |
| `DEALER` | 유지 | 제거 | raw message socket |
| `SUB` | 유지 | 제거 | topic receive는 recv 중심 |
| `XSUB` | 유지 | 제거 | topic receive는 recv 중심 |
| `STREAM` | 유지 | 유지 | stream은 예외로 두 모델 모두 유지 |
| `ROUTER` | 유지 | 제거 | direct routed receive는 recv-only |
| `ROUTER request` | 해당 없음 | 유지 | reply completion callback은 유지 |
| `monitor` | 유지 | 유지 | 관찰 계층 |
| `timer` | 유지 | 유지 | 유틸리티 계층 |
| `SPOT` | 유지 | dispatch-event만 유지 | direct message callback 제거 |

## 5. 타입별 판단 근거

### 5.1 PAIR / DEALER

raw `PAIR`, `DEALER`는 recv-only로 정리한다.

이유는 아래와 같다.

- 일반 메시지 소켓은 poller와 함께 recv loop로 다루는 방식이 가장 예측 가능하다.
- direct callback을 유지하면 모드 전환과 `EBUSY` 규칙만 늘어나고, 주 사용 경로와는
  거리가 있다.
- 본격적인 서버 통신에서는 여러 소켓을 같은 루프에서 다루게 되므로 callback의
  장점이 작아진다.

### 5.2 SUB / XSUB

raw `SUB`, `XSUB`도 recv-only로 정리한다.

이유는 아래와 같다.

- topic 기반 수신도 결국 서버 루프 안에서 poller와 함께 다루는 경우가 많다.
- direct topic callback을 유지하면 filter 관리, recv 금지, `POLLIN` 금지 같은
  규칙이 늘어난다.
- 수신 모델을 recv로 고정하면 subscription은 control plane, subscribe recv는
  data plane으로 더 분명하게 나눌 수 있다.

### 5.3 STREAM

`STREAM`은 recv와 callback을 둘 다 유지한다.

이 초안은 `STREAM`을 예외로 본다.

- `STREAM`은 connection-oriented raw transport라서 event-driven callback
  스타일이 실제로 자연스럽다.
- 동시에 기존 raw receive 모델과의 정합성을 위해 recv도 유지할 수 있다.
- 따라서 `STREAM`만은 "두 모델 모두 지원"을 의도된 예외로 둔다.

즉 이번 개정의 목적은 모든 타입을 같은 방식으로 자르는 것이 아니라,
`STREAM`을 제외한 일반 메시지 소켓에서 direct callback을 줄이는 데 있다.

### 5.4 ROUTER

`ROUTER`는 direct routed receive callback을 제거하고, direct routed receive는
`zlink_router_recv()` 하나로 고정한다.

이 판단의 핵심은 아래와 같다.

- `ROUTER` inbound traffic은 data-plane receive다.
- `ROUTER request`의 reply callback은 async completion이다.
- 이 둘은 역할이 다르므로 같은 계층의 callback으로 두지 않는 편이 더 명확하다.

즉 `ROUTER`는 아래처럼 정리한다.

- 서버/수신 측:
  `zlink_router_recv()`로 routed delivery를 읽는다.
- 요청/클라이언트 측:
  `zlink_router_request(..., handler_, ...)`의 reply completion callback으로
  완료를 받는다.

### 5.5 Monitor / Timer

monitor와 timer는 recv와 callback을 둘 다 유지한다.

이유는 아래와 같다.

- monitor와 timer는 일반 message socket과 성격이 다르다.
- 관찰/스케줄링 계층에서는 recv loop와 callback style이 둘 다 실제 수요가 있다.
- 이 계층은 data-plane socket처럼 고성능 멀티소켓 receive loop만을 기준으로
  판단하기 어렵다.

### 5.6 SPOT

`SPOT`은 이 문서에서 직접 재설계하지 않는다.

`SPOT`의 수신 모델은 아래 draft 문서를 기준으로 별도 유지한다.

- [spot-multi-service-topology.ko.md](spot-multi-service-topology.ko.md)

다만 이 문서에서 고정하는 방향은 아래와 같다.

- `SPOT`의 direct message receive callback은 제거한다.
- `SPOT`의 callback 표면은 `dispatch_event` 계열만 유지한다.
- 실제 payload와 service metadata는 callback 안에서 recv 계열 함수로 drain하는
  방식으로 사용한다.

즉 raw socket family receive model revision과 SPOT service-aware 수신 모델은
같은 규칙으로 강제로 합치지 않지만, `SPOT`도 "data payload는 recv로 꺼낸다"는
큰 방향에는 맞춘다.

## 6. 공개 API 변경 방향

### 6.1 제거 대상

이 초안에서 제거 대상으로 보는 direct receive callback 표면은 아래와 같다.

```c
zlink_handler_result_t zlink_recv_handler (
  void *s_,
  zlink_socket_msg_handler_fn handler_,
  void *userdata_);

zlink_handler_result_t zlink_subscribe_handler (
  void *s_,
  zlink_subscribe_handler_fn handler_,
  void *userdata_);

zlink_handler_result_t zlink_router_handler (
  void *router_,
  zlink_router_handler_fn handler_,
  void *userdata_);
```

다만 이 제거 방향에는 타입별 예외가 있다.

- `zlink_recv_handler()`는 raw `STREAM` 지원을 위해 유지한다.
- `zlink_recv_handler()`의 raw `PAIR`, `DEALER` 지원은 제거한다.
- `zlink_subscribe_handler()`의 raw `SUB`, `XSUB` 지원은 제거한다.
- `zlink_router_handler()`는 전체 제거 대상으로 본다.

즉 함수 단위로 보면 아래처럼 정리된다.

- `zlink_recv_handler()`:
  raw `STREAM` 전용 함수로 축소
- `zlink_subscribe_handler()`:
  raw `SUB`, raw `XSUB` 지원 제거
- `zlink_router_handler()`:
  제거

### 6.2 유지 대상

이번 개정에서도 유지하는 callback 표면은 아래와 같다.

- `zlink_recv_handler()`의 raw `STREAM` 지원
- `zlink_send_ready_handler()`
- `zlink_reply_handler_fn`을 쓰는 request 계열 completion callback
- `zlink_socket_monitor_handler()`
- `zlink_service_monitor_handler()`
- `zlink_timer_handler()`
- `SPOT`의 dispatch-event callback 표면

### 6.3 recv 표면 고정

raw socket family에서 direct receive는 아래 함수군을 기준으로 고정한다.

- raw `PAIR`, `DEALER`, `STREAM`:
  `zlink_recv()`
- raw `SUB`, `XSUB`:
  `zlink_subscribe()`
- raw `ROUTER`:
  `zlink_router_recv()`

이 초안에서는 direct inbound delivery를 받기 위한 callback-only 대체 표면을
추가하지 않는다.

## 7. 함수별 계약 변경 초안

### 7.1 zlink_recv_handler

`zlink_recv_handler()`는 raw `STREAM` 전용 direct receive callback 등록 함수로
재정의한다.

```c
ZLINK_EXPORT zlink_handler_result_t zlink_recv_handler (
  void *s_,
  zlink_socket_msg_handler_fn handler_,
  void *userdata_);
```

새 계약은 아래와 같다.

- 지원 대상:
  raw `STREAM`
- 미지원 대상:
  raw `PAIR`, raw `DEALER`, 그 외 모든 handle
- unsupported subject는 `ZLINK_HANDLER_NOT_SUPPORTED`,
  내부 errno `ENOTSUP`로 실패한다.
- `STREAM`에서는 기존처럼 direct recv와 callback의 양쪽 모델을 계속 지원한다.

### 7.2 zlink_subscribe_handler

`zlink_subscribe_handler()`는 제거한다.

이 결정의 이유는 아래와 같다.

- raw `SUB`, raw `XSUB`는 recv-only로 정리한다.
- `SPOT`도 direct message callback을 제거하고 dispatch-event callback만
  유지한다.
- 따라서 이 함수가 맡을 direct subscribe callback subject가 남지 않는다.

즉 이 함수는 "raw 계층 지원 제거" 수준이 아니라 공개 헤더와 정식 spec에서
삭제 대상으로 본다.

### 7.3 zlink_router_handler

`zlink_router_handler()`는 제거한다.

헤더와 문서는 아래 의미를 가져야 한다.

- direct routed inbound receive는 `zlink_router_recv()`만 지원한다.
- `zlink_router_request()`의 `handler_`는 receive callback이 아니라 request
  completion callback이다.

즉 `ROUTER`는 "수신 모델로서의 callback"을 버리고, "비동기 요청 완료 통지"만
callback으로 유지한다.

## 8. ROUTER request callback 의미 고정

이번 개정에서 가장 중요하게 설명해야 하는 부분은 이 절이다.

`zlink_router_request()`의 `handler_`는 direct receive callback이 아니다.

- 이 callback은 `router_`로 들어오는 임의의 inbound traffic을 전달하지 않는다.
- 이 callback은 호출자가 제출한 request의 completion만 전달한다.
- completion은 성공 reply, timeout, protocol error, 내부 실패 같은
  `zlink_request_result_t` 결과로 정규화된다.

즉 `ROUTER`의 callback은 앞으로 아래 한 종류만 남는다.

- operation completion callback

반면 아래 종류는 제거한다.

- direct inbound routed delivery callback

이 구분을 문서 전체에서 계속 유지해야 한다.

## 9. poller와의 관계

이 개정의 목적 중 하나는 poller 기반 서버 루프를 주 모델로 더 분명하게 만드는
것이다.

target 사용 흐름은 아래와 같다.

```text
+------------------------------------------------------------------+
|                         Server Event Loop                        |
|------------------------------------------------------------------|
| poller wait                                                      |
| readable socket -> recv                                          |
| readable router -> router_recv                                   |
| readable sub/xsub -> subscribe                                   |
| readable monitor -> monitor_recv                                 |
| readable timer -> timer_recv                                     |
+------------------------------------------------------------------+
```

이 모델의 의미는 아래와 같다.

- data-plane receive는 poller readable + recv 조합으로 처리한다.
- monitor와 timer도 같은 루프에서 함께 처리할 수 있다.
- callback은 예외적 타입 또는 completion 통지에만 남긴다.

## 10. 구현 순서 기준

### 10.1 공통 헤더 정리

먼저 `core/include/zlink.h`와 socket 공통 spec에서 수신 모델 표를 정리한다.

- raw `PAIR`, `DEALER`에서 `zlink_recv_handler()` 지원 제거
- raw `SUB`, `XSUB`에서 `zlink_subscribe_handler()` 지원 제거
- raw `STREAM`은 recv/callback 둘 다 유지
- raw `ROUTER`에서 `zlink_router_handler()` 제거

### 10.2 ROUTER 문서와 errno-map 정리

그 다음 `ROUTER` 문서에서 아래 내용을 반영한다.

- `zlink_router_recv()`가 유일한 direct inbound receive 표면임을 고정
- `zlink_router_handler()` 절 삭제
- request completion callback과 direct receive callback의 차이를 문장으로 명확히
  설명

동시에 `errno-map`에서도 제거된 함수 항목을 정리한다.

### 10.3 SUB / XSUB / PAIR / DEALER 문서 정리

타입별 문서에서 direct receive callback 설명을 제거한다.

- recv 모드만 설명한다.
- callback 부착 후 `EBUSY`가 된다는 receive mode 전환 설명도 함께 제거한다.
- poller와 recv 조합을 권장 경로로 고정한다.

### 10.4 STREAM 예외 유지 정리

`STREAM` 문서에서는 예외 규칙을 분명히 쓴다.

- `STREAM`은 recv와 callback을 모두 지원한다.
- 이번 개정의 callback 유지 예외 타입이다.
- callback 사용 시 close 제약, thread 문맥, recv와 poller 충돌 규칙은 계속
  유지한다.

### 10.5 테스트 정리

테스트는 아래 방향으로 다시 정리한다.

- raw `PAIR`, `DEALER`, `SUB`, `XSUB`의 direct receive callback 테스트 제거
- raw `ROUTER` direct receive callback 테스트 제거
- `STREAM` recv/callback 양쪽 경로 테스트 유지
- `ROUTER request` completion callback 테스트 유지
- monitor/timer recv/callback 양쪽 경로 테스트 유지

## 11. 헤더 초안

이 절은 현재 방향을 공개 헤더 수준에서 요약한다.

### 11.1 유지 함수

```c
ZLINK_EXPORT zlink_recv_result_t zlink_recv (
  void *s_,
  zlink_routing_id_t *source_rid_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_);

ZLINK_EXPORT zlink_recv_result_t zlink_subscribe (
  void *subject_,
  zlink_routing_id_t *source_rid_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);

ZLINK_EXPORT zlink_recv_result_t zlink_router_recv (
  void *router_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_);

ZLINK_EXPORT zlink_handler_result_t zlink_recv_handler (
  void *s_,
  zlink_socket_msg_handler_fn handler_,
  void *userdata_);
```

여기서 `zlink_recv_handler()`는 raw `STREAM` 전용 지원으로 본다.

### 11.2 제거 또는 축소 대상

```c
ZLINK_EXPORT zlink_handler_result_t zlink_router_handler (
  void *router_,
  zlink_router_handler_fn handler_,
  void *userdata_);

ZLINK_EXPORT zlink_handler_result_t zlink_subscribe_handler (
  void *s_,
  zlink_subscribe_handler_fn handler_,
  void *userdata_);
```

이 초안의 방향은 아래와 같다.

- `zlink_router_handler()`:
  제거
- `zlink_subscribe_handler()`:
  제거

## 12. 기대 효과

이 개정으로 기대하는 효과는 아래와 같다.

- raw socket family의 주 수신 모델이 더 분명해진다.
- recv/callback 이중 지원 때문에 생기던 상태 전이와 `EBUSY` 규칙이 줄어든다.
- 서버 간 통신의 기본 구조를 `poller + recv` 중심으로 설명할 수 있다.
- `ROUTER`에서 direct receive callback과 request completion callback의 의미가
  분리된다.
- `SPOT`에서도 direct message callback 대신 dispatch-event callback + recv
  drain 모델로 역할이 분리된다.
- 문서, 테스트, 바인딩 설계에서 고려해야 하는 조합이 줄어든다.

## 13. 남은 확인 사항

구현 전 마지막으로 확인해야 할 항목은 아래와 같다.

- socket 공통 문서의 callback 타입 표를 어떻게 다시 쓸지
- `STREAM` 예외 규칙을 socket 공통 문서에서 얼마나 강하게 드러낼지
- 바인딩 문서에서 direct receive callback 제거를 어떻게 반영할지

## 14. 정식 spec 분해 계획

구현과 공개 헤더가 정리되면 이 초안 내용은 아래 문서들로 나누어 반영한다.

- `doc/spec/core/socket/README*.md`
  socket 공통 수신 모델과 callback 타입 표
- `doc/spec/core/socket/pair*.md`
  PAIR recv-only 표면
- `doc/spec/core/socket/dealer*.md`
  DEALER recv-only 표면
- `doc/spec/core/socket/sub*.md`
  SUB recv-only 표면
- `doc/spec/core/socket/xsub*.md`
  XSUB recv-only 표면
- `doc/spec/core/socket/router*.md`
  ROUTER recv-only direct receive와 request completion callback
- `doc/spec/core/socket/stream*.md`
  STREAM 예외 규칙
- `doc/spec/core/errno-map*.md`
  제거/축소된 handler API와 결과 매핑
