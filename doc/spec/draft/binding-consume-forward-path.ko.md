# Binding consume-forward path 초안

이 문서는 **정식 spec 승격 전 설계 기록**이다.
2026-05-22 현재 core C API와 Go API의 1차 구현은 들어갔지만, 정식 공개 계약 문서로
나누어 반영하는 작업은 아직 끝나지 않았다. 실제 공개 surface는 `core/include/zlink.h`,
관련 core spec, 각 바인딩의 정식 문서를 기준으로 한다.

이 초안은 바인딩이 받은 메시지를 다시 보낼 때 생기는 복사와 경계 호출 비용을 줄이기 위한
공개 계약 후보를 정리한다. 첫 대상은 Go `MULTI_SPOT_SENDSEND` 보류 항목이다.
다만 계약은 perf 전용 이름이나 perf 전용 조건을 드러내지 않고, 일반적인 routed relay
용도로 쓸 수 있어야 한다.

## 배경

Go 바인딩에는 이미 아래 공개 경로가 있다.

- `RecvPart(...)`: caller가 제공한 `Message`에 수신 part를 채운다.
- `MoveMessage(...)`: caller가 message 소유권을 submit 시점에 넘긴다.
- `Bytes(...)`: caller-owned byte slice를 submit 중에만 읽는다.

이 경로들은 단일 수신, 명시적 소유권 이전, caller-owned byte slice 계약을 각각 해결한다.
하지만 `SPOT_SENDSEND`처럼 "받은 routed message를 같은 상대에게 바로 돌려보내는" relay
경로에서는 Go wrapper가 route metadata, message wrapper, builder submit 단계를 모두
통과한다. 현재 측정에서는 이 비용을 public API 내부 최적화만으로 충분히 줄이지 못했다.

## 목표

1. 받은 routed message를 다시 보내는 일반 relay 경로를 public 계약으로 정의한다.
2. caller가 native handle, `zlink_msg_t`, 내부 routing pointer를 직접 알 필요가 없게 한다.
3. 입력 message가 소비되는 시점을 명확히 하여 실패 시 원본 보존 계약과 충돌하지 않게 한다.
4. Go뿐 아니라 다른 바인딩도 같은 core 의미를 사용할 수 있게 C API를 먼저 정의한다.
5. perf runner는 새 계약을 통해서만 고성능 relay 경로를 사용한다.

## 비목표

- `MULTI_SPOT_SENDSEND`만을 위한 perf 전용 API를 추가하지 않는다.
- 기존 `Message(...)`, `MoveMessage(...)`, `Bytes(...)` builder 계약을 바꾸지 않는다.
- 실패 시 원본 message를 보존한다는 `Message(...)` 계약을 약화하지 않는다.
- 바인딩에서 reflection, internal helper, native pointer 우회로를 public API처럼 사용하지 않는다.
- HWM, sleep, backoff 값을 바꿔 통과시키는 튜닝 API를 추가하지 않는다.

## 설계 대안

### 대안 A: builder 변형 추가

`SendOp`에 `BorrowMessage(...)`, `UnsafeMessage(...)` 같은 builder 단계를 더 추가한다.

이 대안은 채택하지 않는다. `MoveMessage(...)`와 `Bytes(...)`가 이미 명시적 소유권 이전과
caller-owned slice 경로를 제공한다. builder 단계를 더 늘리면 caller가 선택해야 하는
payload 모드만 많아지고, 수신 route metadata와 submit 경계 호출 비용은 그대로 남는다.
인터페이스가 얕아진다.

### 대안 B: Received 전용 forward 메서드

`Received.ForwardFirstPart(...)` 또는 `Received.Forward(...)`를 추가해 받은 메시지를
바로 보낸다.

이 대안은 일부 문제를 줄이지만 첫 구현 대상으로 삼지 않는다. `Received`를 만들기 위해 이미
바인딩 wrapper와 part slice를 구성해야 하므로, core가 가진 native receive/send 연속성을
살리기 어렵다. 또한 `Received`가 reply, normal send, forward까지 모두 알게 되어 하나의
값 타입에 너무 많은 실행 경로가 붙는다.

### 대안 C: core consume-forward primitive

core에 "받은 routed message를 소비하면서 같은 route로 다시 보내는" primitive를 추가하고,
바인딩은 이를 얇게 노출한다.

이 대안을 선택한다. caller가 알아야 할 것은 "수신한 routed message가 소비되고, 같은
상대에게 forward된다"는 의미뿐이다. native message 소유권, multipart more flag, route
metadata 보존은 core가 내부에서 처리한다. 이 방식은 POSD의 깊은 모듈 원칙에 맞다.

## C API 초안

첫 구현은 SPOT routed message에만 둔다. ROUTER, DEALER 계열은 같은 문제가 재현되는지
별도 측정 뒤 확장한다.

```c
typedef struct zlink_spot_forward_result_t
{
    zlink_routing_id_t source_node_rid;
    zlink_routing_id_t source_spot_rid;
    uint64_t request_seq;
    uint32_t has_request_seq;
    size_t part_count;
    size_t payload_bytes;
} zlink_spot_forward_result_t;

zlink_submit_result_t zlink_spot_forward_routed(
  void *spot,
  zlink_recv_flags_t recv_flags,
  zlink_send_flags_t send_flags,
  zlink_spot_forward_result_t *result_out);
```

### 의미

- `spot`은 유효한 Spot handle이어야 한다.
- 함수는 routed receive queue에서 메시지 하나를 읽는다.
- 읽은 message part들은 같은 `source_node_rid`, `source_spot_rid`로 다시 전송된다.
- multipart 메시지는 원래 part 순서와 `MORE`/`FINAL` 경계를 유지한다.
- 수신된 native message는 성공 여부와 관계없이 이 함수가 소비한다.
- `result_out`이 `NULL`이 아니면 실제 수신한 source route, request sequence 정보,
  part 수, payload byte 수를 채운다.
- request sequence가 있는 메시지는 기본적으로 forward하지 않는다. 이 경우
  `ZLINK_SUBMIT_INVALID_ARGUMENT`를 반환하고 `result_out`에는 수신 metadata를 채운다.
  request/reply는 `Reply` 계약으로 처리해야 하기 때문이다.
- receive queue에 데이터가 없으면 `ZLINK_SUBMIT_NOT_FOUND`를 반환한다.
- send backpressure는 `ZLINK_SUBMIT_BACKPRESSURED`로 반환한다.
- invalid handle, invalid state, terminated, internal error는 기존 submit result domain을
  따른다.

`ZLINK_SUBMIT_NOT_FOUND`는 receive no-data를 submit result domain에서 표현하기 위해
사용한다. 정식 spec 승격 시 errno/result 문서에도 같은 의미를 반영해야 한다.

## Go API 초안

Go 바인딩은 native pointer나 C result를 노출하지 않는다.

```go
type SpotForwardResult struct {
    SourceNodeRID RoutingID
    SourceSpotRID RoutingID
    RequestSeq    uint64
    HasRequestSeq bool
    PartCount     int
    PayloadBytes  int
}

func (s *Spot) ForwardRouted(recvFlags RecvFlags, sendFlags SendFlags) (SpotForwardResult, bool, error)
```

### Go 의미

- 반환값 `ok=false, err=nil`은 `RecvFlagsDontWait`에서 수신 데이터가 없다는 뜻이다.
- `ok=true, err=nil`은 메시지를 하나 소비하고 forward submit까지 끝났다는 뜻이다.
- `ok=false, err=*SubmitError`는 메시지를 소비했지만 forward submit이 실패했거나,
  request message처럼 forward 대상이 아닌 메시지를 받은 경우다.
- 이 함수는 payload를 caller에게 노출하지 않는다. payload를 검사하거나 수정해야 하는
  caller는 기존 `RecvRouted(...)`, `RecvRoutedPart(...)`, `SendToSpot(...)` 조합을 쓴다.
- 이 함수는 `Received.Send().Message(...)`의 실패 시 원본 보존 계약을 바꾸지 않는다.
  consume-forward는 별도 API이며 수신 payload를 항상 소비한다.

## 구현 계획

1. `core/include/zlink.h` 또는 SPOT public header에 C result struct와 function을 추가한다.
2. core SPOT implementation에서 `zlink_spot_recv_part`와 `zlink_spot_send_spot_part`의
   native message 소유권을 한 함수 안에서 처리한다.
3. request sequence가 있는 routed request는 forward하지 않는 회귀 테스트를 추가한다.
4. multipart order와 part flag 보존 테스트를 추가한다.
5. Go `contracts` projection과 root implementation에 `SpotForwardResult`와
   `Spot.ForwardRouted(...)`를 추가한다.
6. Go public API 테스트에서 no-data, single-part forward, multipart forward, request
   message rejection을 확인한다.
7. Go `MULTI_SPOT_SENDSEND` 서버 echo 경로에만 새 API를 선택 적용하고, 기존
   `RecvRouted(...)`/`Received.Send(...)` 경로는 유지한다.
8. `bindings/c/perf`와 Go perf를 같은 transport/size 조건으로 재측정한다.
9. 기준을 통과한 size만 상태표에 반영한다. 다른 size가 악화되면 선택 적용하거나 원복한다.
10. Java, .NET, Node, Rust, Python 바인딩도 같은 C API를 노출할지 별도 항목으로 검토한다.

## 문서 반영 계획

구현 전에는 정식 spec에 계약을 추가하지 않는다. 구현이 끝난 뒤 아래 문서에 나누어 반영한다.

- `doc/spec/core/`의 SPOT routed message 계약 문서
- `doc/spec/bindings/go/README.md`
- Go public GoDoc
- `doc/perf`와 `doc/plan/perf`의 Go 보류 항목
- errno/result 문서

## 검증 기준

- core native test가 single-part, multipart, no-data, backpressure, request rejection을
  확인한다.
- Go `go test ./...`가 public API 계약을 확인한다.
- `MULTI_SPOT_SENDSEND`는 최소한 기존 통과 size를 깨지 않아야 한다.
- 새 API가 적용된 size는 C 기준과 같은 `MsgUnit(B)`, auto-HWM, active window, client 수로
  재측정한다.
- 통과 판정은 throughput 비율뿐 아니라 no-result, timeout, latency 이상치를 함께 본다.
