# Request/reply multipart rollback 이슈 review

> 상태: Claude Fable review 요청
>
> 이 문서는
> [`inbound-dispatch-lane-design.ko.md`](../inbound-dispatch-lane-design.ko.md)의
> Core 구현 중 발견한 multipart rollback 이슈를 검토하기 위한 작업 log다.
> 새로운 설계나 승인된 공개 계약을 정의하지 않는다.
>
> Reviewer는 이 문서의 **8. Review 답변**을 직접 작성한다. 코드는 수정하지 않는다.

## 1. Review 결과로 결정할 내용

Byte HWM이 multipart 전송의 마지막 frame을 거부했을 때, 앞서 기록한
control frame이 다음 request와 합쳐지는 현상을 확인했다.

이번 review는 다음 두 가지를 결정해야 한다.

1. `lb_t`의 one-pipe fast path가 실패한 multipart를 rollback하지 않는 것이
   재현 결과의 직접 원인인지 확인한다.
2. 해당 분기만 수정하면 충분한지, `lb_t`와 `dealer_t`가 명시적인 rollback
   동작도 제공해야 하는지 판단한다.

## 2. 변경 배경

Core candidate `784e504384`는 DEALER/ROUTER transport를 Application connection과
Completion connection으로 분리하고, socket HWM을 message 개수가 아닌 byte
단위로 적용한다. Framework 내부에 payload를 복제하는 queue를 두지 않고,
Application connection의 network pipe에 backlog를 유지하는 것이 목표다.

전체 설계와 승인 조건은
[`inbound-dispatch-lane-design.ko.md`](../inbound-dispatch-lane-design.ko.md)를
기준으로 판단한다.

## 3. 재현 조건

다음 benchmark를 `core/build/lib/libzlink.so`와 연결해 실행했다.

```bash
LD_LIBRARY_PATH=/home/hep7/project/kairos/zlink/core/build/lib \
PERF_DEBUG=1 \
PERF_SINGLE_DURATION_SECONDS=1 \
bindings/c/build/perf/perf_dealer_router_reqrep current tcp 64
```

Socket에는 `4,096,000` byte HWM을 명시했다. 첫 request가 연결 준비 시점의
짧은 전환 구간에서 한 번 backpressured된 뒤 request 전송이 진행됐다.

## 4. 확인한 frame 증거

Request 1부터 41까지는 각각 다음 다섯 frame으로 수신됐다.

```text
1, 1, 1, 8, 64
```

앞의 네 frame은 request/reply protocol, version, message type, request sequence다.
마지막 `64` byte frame이 application payload다.

Request 42는 네 control frame을 기록한 뒤 마지막 payload가 HWM admission에서
거부됐다. 다음 수신 결과는 아홉 frame이었다.

```text
1, 1, 1, 8, 1, 1, 1, 8, 64
```

첫 네 frame은 request 42로 해석됐다. 뒤의 네 control frame과 `64` byte
payload는 request 43에 해당했다. 따라서 request 42의 미완성 control frame이
제거되지 않았고, request 43이 같은 multipart 뒤에 기록됐다.

이 결과 때문에 Router는 request 42의 payload가 다섯 부분이라고 판단했다.
첫 부분을 반환하면서 `ZLINK_PART_MORE`를 설정했고, 한 부분만 보낸 benchmark는
이를 비정상 metadata로 처리했다. 이 오류는 `zlink_recv_result_t`를 잘못
해석해서 발생한 현상이 아니다.

## 5. 현재 코드에서 확인할 원인

다음 파일과 직접 연결된 정의만 검토한다.

- `core/src/runtime/core/multipart_send_txn.cpp`
- `core/src/runtime/sockets/internal/lb.cpp`
- `core/src/runtime/sockets/internal/lb.hpp`
- `core/src/runtime/sockets/dealer/dealer.cpp`
- `core/src/runtime/sockets/dealer/dealer.hpp`
- `core/src/runtime/sockets/common/socket_base_msg.cpp`
- `core/src/runtime/core/pipe.cpp`
- `core/src/runtime/sockets/router/router.cpp`
- `core/src/runtime/sockets/pubsub/xpub.cpp`
- `core/src/runtime/sockets/internal/dist.cpp`

현재 확인한 호출 관계는 다음과 같다.

1. `logical_multipart_send()`는 frame을 순서대로 전송한다.
2. 앞선 frame을 기록한 뒤 다음 frame 전송이 실패하면 socket의
   `rollback_scoped()`를 호출한다.
3. 이 호출은 socket별 `xrollback()`으로 이어진다.
4. ROUTER와 XPUB은 `xrollback()`을 구현하지만 DEALER는 구현하지 않는다.
   따라서 DEALER는 기본 `socket_base_t::xrollback()`을 사용하며, 기본 동작은
   아무 frame도 제거하지 않는다.
5. `lb_t::sendpipe()`의 일반 unweighted 경로와 weighted multipart 경로는
   multipart 중간 실패 시 `pipe->rollback()`을 호출한다.
6. `lb_t::sendpipe()`의 one-pipe fast path는 후속 frame 기록이 실패해도
   `_active`를 `0`으로 바꾸고 `EAGAIN`을 반환할 뿐, `pipe->rollback()`을
   호출하거나 multipart 상태를 끝내지 않는다.

## 6. Reviewer가 답할 질문

다음 질문에 각각 `확인`, `반박`, `추가 확인 필요` 중 하나로 답하고 근거가
되는 `file:line`을 제시한다.

1. `lb_t` one-pipe fast path의 rollback 누락이 9-frame 병합의 직접 원인인가?
2. one-pipe 실패 분기에서 일반 경로와 같은 rollback 및 상태 전환을 수행하면
   재현된 corruption을 제거할 수 있는가?
3. 예외 경로를 `lb_t` 내부에서 완결하기 위해 `lb_t::rollback()`과
   `dealer_t::xrollback()`도 추가해야 하는가?
4. 두 수정안을 함께 적용할 경우 중복 책임이나 불필요한 interface 증가가
   발생하는가?
5. 같은 문제가 ROUTER, PUB/XPUB 또는 weighted DEALER 경로에도 남아 있는가?
6. HWM 실패 뒤 pipe가 write credit을 회복했을 때 `_active`, `_more`,
   `_dropping`, `_weighted_multipart_pipe`가 어떤 상태여야 하는가?

## 7. 필수 regression test

최소 수정안에는 다음 조건을 자동으로 확인하는 test가 필요하다.

1. DEALER의 one-pipe 경로에서 첫 frame을 `ZLINK_SNDMORE`로 기록한다.
2. Byte HWM 때문에 마지막 frame의 기록을 실패시킨다.
3. 실패한 multipart의 앞선 frame이 pipe에서 제거됐는지 확인한다.
4. Write credit을 회복한 뒤 단일-part message를 다시 전송한다.
5. 수신 측은 새 message 한 부분만 받고 `ZLINK_PART_FINAL`을 반환해야 한다.
6. 수신 payload에 실패한 multipart의 control frame이나 payload가 포함되면
   test가 실패해야 한다.
7. 실패 뒤 load balancer 상태가 다음 정상 전송을 막지 않는지 확인한다.

Reviewer는 이 test만으로 부족하다면 추가 scenario와 필요한 이유를 제시한다.

## 8. Review 답변

> Reviewer: Claude (Fable). 검토 시점 주의 — review 도중(09:41) 작업 트리에
> 질문 2·3의 수정이 이미 반영되었다(`lb.cpp` one-pipe rollback 분기,
> `lb_t::rollback()`, `dealer_t::xrollback()`). 아래 판정은 **수정 전 상태**
> (HEAD와 이 문서 §5의 기술)를 원인 분석 대상으로, **현재 트리의 수정**을
> 충분성 평가 대상으로 삼는다. 코드는 수정하지 않았다.

### 8.1 결론

재현된 9-frame 병합의 원인 사슬을 확인했다. 두 방어선이 함께 뚫려야 발생하는
문제였고, 실제로 둘 다 뚫려 있었다.

1. **byte HWM은 마지막 frame에서 message 전체를 admission한다.**
   `write_message_unlocked`는 more frame을 `_out_incomplete_bytes`에 누적만 하고,
   최종 frame에서 `can_commit_bytes_unlocked(_out_incomplete_bytes)`로 한 번에
   검사한다(`pipe.cpp:1286-1296`). 그래서 앞 4개 control frame이 성공한 뒤 마지막
   payload만 거부되는 상황이 **byte HWM에서 처음으로 도달 가능**해졌다. count HWM
   시절에는 모든 frame이 같은 message 수를 보므로 one-pipe 경로의 중간 실패가
   사실상 불가능했다 — 즉 이 분기는 원래부터 rollback이 없었지만(HEAD
   `lb.cpp:264-272`) byte HWM 변경이 잠복 결함을 활성화했다.
2. **one-pipe fast path가 실패를 정리하지 않았다.** `_active=0`과 `EAGAIN`만
   반환하고 rollback도 `_dropping` 진입도 하지 않아, more flag가 달린 4개 frame이
   **flush되지 않은 채** ypipe에 남았다.
3. **txn 안전망도 DEALER에서 no-op이었다.** `send_frames_once`는 실패 시
   `rollback_scoped`를 호출하지만(`multipart_send_txn.cpp:74-77` →
   `socket_base_msg.cpp:337-341` → `xrollback`), DEALER는 `xrollback`을 구현하지
   않아 기본 구현(`socket_base.cpp:529-532`)이 아무것도 제거하지 않았다.
4. 다음 request(43)의 최종 frame이 flush될 때 잔류 frame 4개가 함께 흘러나가
   receiver가 9-frame을 하나의 multipart로 해석했다 — §4의 증거와 정확히 일치한다.

질문별 판정:

| 질문 | 판정 |
|---|---|
| 1. one-pipe rollback 누락이 직접 원인인가 | **확인** — 단, DEALER `xrollback` 부재와의 결합 결함. 둘 중 하나만 있었어도 corruption은 없었다 |
| 2. one-pipe 분기 수정으로 corruption 제거 가능한가 | **확인** — 현재 트리의 수정(`lb.cpp:268-276`)이 일반 경로와 동일한 정리를 수행한다 |
| 3. `lb_t::rollback()`·`dealer_t::xrollback()`도 필요한가 | **확인** — 트리거가 다르다. lb 내부 수정은 write 실패를, `xrollback`은 caller 주도 abort(뒤 frame 준비 실패 등)를 덮는다 |
| 4. 두 수정의 중복 책임 여부 | **반박(중복 없음)** — 이중 호출이 idempotent하다. 아래 finding 참조 |
| 5. 같은 문제가 다른 경로에 남아 있는가 | **확인 — `dist_t::write_at`에 동일 결함 잔존.** ROUTER·weighted DEALER는 정리됨 |
| 6. 회복 후 상태 | 8.2 마지막 finding에 명시 |

### 8.2 Finding

```text
[High] one-pipe fast path의 multipart 실패 미정리 (재현의 1차 원인)
- 판정: 확인 (현재 트리에서 수정 반영됨)
- 근거: HEAD lb.cpp:264-272(수정 전), 현재 lb.cpp:268-276(수정),
        pipe.cpp:1286-1296(최종 frame commit 검사), §4 재현 증거
- 영향: 잔류 more-frame이 다음 message의 flush에 실려 나가 receiver가
        서로 다른 request를 하나의 multipart로 병합한다. request/reply
        protocol frame 경계가 깨져 이후 모든 해석이 오염될 수 있다.
- 수정 제안: 반영된 수정 유지. 일반 경로(lb.cpp:336-346)와 동일하게
        rollback → _dropping=(msg.more) → _more=false → -2/EAGAIN.
        _more 분기에서는 _active를 건드리지 않는 것도 일반 경로와 일치한다.
```

```text
[High] DEALER의 xrollback 부재 (안전망 붕괴, 2차 원인)
- 판정: 확인 (현재 트리에서 수정 반영됨)
- 근거: HEAD dealer.hpp에 xrollback 없음, socket_base.cpp:529-532(기본 no-op),
        현재 dealer.cpp:156-160 + lb.cpp:376-386 + lb.hpp:42(수정)
- 영향: txn이 rollback했다고 믿지만 아무것도 제거되지 않는다. lb 수정과
        무관하게 caller 주도 abort(뒤 frame의 init 실패, routed prefix 뒤
        준비 실패 등) 경로에서 같은 병합이 재발할 수 있었다.
- 수정 제안: 반영된 수정 유지. lb_t::rollback()은 _weighted_multipart_pipe
        우선, 아니면 _more일 때 _pipes[_current]를 rollback하고 상태를
        초기화한다 — ROUTER(router.cpp:408-415)·XPUB(xpub.cpp:381-384)과
        대칭이 맞다.
```

```text
[High] dist_t::write_at의 동일 결함 — XPUB·classic fanout·Logical Multicast local 전달
- 판정: 확인 (미수정)
- 근거: dist.cpp:230-241 — 뒤 frame 실패 시 deactivate_matching_pipe()만
        호출하고 pipe->rollback()이 없다. send_to_matching(dist.cpp:196-205)은
        per-pipe 실패를 상위로 알리지 않으므로 xpub_t::xrollback도 발동하지
        않는다.
- 영향: multi-frame publish에서 구독자 하나가 byte HWM 근처일 때, 그
        구독자 pipe에 flush되지 않은 more-frame이 잔류하고 다음 publish와
        병합된다. lb와 같은 부류이며 byte HWM으로 도달 가능해졌다.
- 수정 제안: write_at 실패 분기에서 more 상태(해당 pipe에 앞 frame을 쓴
        경우)면 deactivate 전에 pipe->rollback()을 호출한다. dist는 pipe별
        진행 상태를 이미 index로 알고 있으므로 상태 추가 없이 가능하다.
```

```text
[Medium] blocking 모드의 -2 처리 — message가 조용히 성공으로 끝난다
- 판정: 추가 확인 필요 (기존 계약, 이번 변경으로 노출 빈도 증가)
- 근거: socket_base_msg.cpp:169-178 — rc==-2이고 DONTWAIT·sndtimeo==0이
        아니면 msg를 닫고 0(성공)을 반환한다.
- 영향: blocking send에서 mid-multipart HWM 실패가 caller에게 성공으로
        보인다. request/reply에서는 request 유실이 caller timeout으로만
        드러난다. byte HWM에서는 이 경로의 발생 빈도가 count HWM보다 높다.
- 수정 제안: 이 review 범위 밖. request/reply helper가 -2 계열을 구분해
        즉시 재시도 또는 오류로 승격할지 별도 판단이 필요하다.
```

```text
[Medium] multipart 총량이 최종 frame까지 무제한 누적된다
- 판정: 추가 확인 필요
- 근거: append_outbound_frame_bytes_unlocked(pipe.cpp:1259-1269)는 overflow만
        검사하고 총량 상한을 검사하지 않는다. 총량 admission은 최종 frame의
        can_commit에서만 일어난다.
- 영향: frame 수가 많은 multipart는 거부가 확정되기 전까지
        _out_incomplete_bytes와 ypipe 보관량이 HWM·MaxMessageSize와 무관하게
        커질 수 있다. 설계 문서 §5.6의 "commit 전에 complete message 전체를
        한 번 admission" 의도와 어긋날 수 있다.
- 수정 제안: 누적 시점에 effectiveMaxMessageBytes 초과를 조기 거부할지
        확인하고, 설계 문서의 multipart admission 문장과 구현을 일치시킨다.
```

```text
[Low] rollback_unlocked의 more-flag assert는 flush 경계 가정에 의존한다
- 판정: 확인 (현재 안전)
- 근거: pipe.cpp:1359-1371 — unwrite한 frame마다 more flag를 assert한다.
        현재는 flush가 write_and_flush(최종 frame)에서만 일어나 성립한다.
- 영향: 미래에 message 중간에서 flush를 호출하는 코드가 추가되면 assert로
        중단된다. 결함이 아니라 지켜야 할 불변식이다.
- 수정 제안: 코드 변경 불필요. "flush는 message 경계에서만"을 pipe 주석
        또는 내부 문서에 명시한다.
```

byte 회계 관점의 확인 — rollback 시 반환할 credit은 없다. incomplete byte는
최종 frame commit 전까지 `_bytes_written`에 반영되지 않으므로
(`pipe.cpp:1300-1326`), rollback은 `_out_incomplete_bytes=0`으로 충분하며
누수도 이중 반환도 없다. 질문 6의 회복 후 상태는 다음이 정답이다:
`_more=false`, `_dropping`은 실패 frame에 more가 있었을 때만 true(같은 logical
message의 남은 frame을 소비하기 위해서만), `_weighted_multipart_pipe=NULL`,
`_active`는 -2 분기에서 변경하지 않는다. pipe의 `_out_active=false`는 peer가
LWM까지 읽으면 `process_activate_write` → `write_activated` →
`lb_t::activated`(lb.cpp:72-86)로 복원되고, 이미 active 목록에 있으면 이중
등록되지 않는다.

### 8.3 권장 수정 범위

1. **반영된 lb·dealer 수정을 유지한다.** 형태가 일반 경로·ROUTER·XPUB과 대칭이고
   이중 rollback이 idempotent함을 확인했다 — lb의 -2 분기가 `_more=false`로
   만든 뒤 `xrollback`이 와도 `lb_t::rollback()`의 guard(lb.cpp:378-380)가
   no-op으로 끝나고, pipe 수준 이중 rollback은 unwrite할 것이 없다.
2. **`dist_t::write_at`에 같은 정리를 추가한다.** 이번 회귀와 같은 부류이며
   byte HWM 후보를 완료 판정하기 전에 함께 고쳐야 한다.
3. [Medium] 두 건은 별도 이슈로 분리해 각각 판단한다 — 이 review의 회귀
   범위를 넘는 계약 문제다.

### 8.4 필수 test

§7의 7개 조건을 승인한다. 단 §7-3(“앞선 frame이 pipe에서 제거됐는지”)은 frame이
flush 전이라 receiver 관찰만으로는 부족하므로, **monitoring의 in-flight byte가
message 이전 값으로 돌아왔는지**로 함께 검증한다. 추가로 다음이 필요하다.

1. **dist/XPUB 재현 test** — 구독자 하나를 byte HWM 근처로 만들고 2-frame
   publish의 최종 frame을 거부시킨 뒤, 그 구독자의 다음 publish 수신이
   병합되지 않는지 확인한다. 현재 코드에서는 실패해야 정상이다(수정 전).
2. **blocking -2 의미 고정** — sndtimeo 기본에서 mid-multipart 실패 시 send가
   0을 반환하고 receiver가 partial을 받지 않는지, DONTWAIT에서는 EAGAIN과 함께
   txn rollback 경로(이중 rollback 포함)가 안전한지.
3. **byte credit 무누수** — rollback 후 `bytesInFlight`가 이전 값과 같고,
   credit 회복 뒤 단일-part 전송이 성공한다(§7-4와 결합).
4. **one-pipe → 복수 pipe 전이** — one-pipe에서 _more 실패 직후 두 번째 pipe가
   attach되어도 _dropping 소비가 새 pipe로 흘러가지 않는지.
5. **weighted 경로 회귀 고정** — 기존 rollback 분기(lb.cpp:245-251)를 같은
   시나리오로 고정한다.

### 8.5 남은 위험

- **dist 결함이 수정될 때까지 XPUB·Logical Multicast의 multi-frame 발행은 같은
  병합 위험을 가진다.** 이번 후보의 검증 범위에 반드시 포함해야 한다.
- blocking -2의 "조용한 성공"은 byte HWM에서 발생 빈도가 올라간다. request
  유실이 timeout으로만 보이는 운영 증상을 만들 수 있다.
- multipart 총량의 조기 admission 부재는 설계 문서의 admission 문장과 구현
  사이의 잠재 drift다. 문서와 구현 중 어느 쪽을 맞출지 결정이 필요하다.
- 이 review는 DEALER/ROUTER reqrep 재현 경로와 위 8개 파일 범위만 검토했다.
  PAIR·STREAM 등 다른 socket type의 write 실패 경로는 같은 기준으로 별도
  확인이 필요하다.

## 9. Finding 처리 결과

> §8의 review 답변은 그대로 유지한다. 이 절은 각 finding을 어떻게 처리했고 어떤
> test가 그것을 고정하는지 기록한다. 확인 commit은 `563e11d614`
> (`core: recover completion credit on reply submit`)다.

### 9.1 Finding별 처리

| Finding | 처리 | 근거 |
| --- | --- | --- |
| [High] one-pipe fast path의 multipart 실패 미정리 | 수정 유지 | `lb.cpp` one-pipe 분기가 일반 경로와 같은 rollback·`_dropping` 전환·`-2` 반환을 수행한다. `test_single_pipe_lb_rolls_back_byte_hwm_rejected_multipart` |
| [High] DEALER의 `xrollback` 부재 | 수정 유지 | `dealer_t::xrollback()`이 `lb_t::rollback()`을 호출한다. ROUTER·XPUB과 대칭 |
| [High] `dist_t::write_at`의 동일 결함 | **수정 완료** | `dist.cpp`가 multipart 중간 실패 시 해당 pipe를 rollback한다. `test_single_pipe_dist_rolls_back_byte_hwm_rejected_multipart` |
| [Medium] blocking 모드 `-2`가 성공으로 둔갑 | **부분 수정** | logical multipart helper는 mid-multipart abort를 `EAGAIN`으로 보고한다. 기존 raw multipart send의 호환 동작은 바꾸지 않았다 — 9.3 참고 |
| [Medium] multipart 총량 무제한 누적 | **수정 완료** | `pipe.cpp`가 각 frame 누적 시 HWM 초과를 조기 거부하고 거부한 frame의 누적값을 복구한다. `test_pipe_rejects_multipart_before_partial_bytes_exceed_hwm` |
| [Low] `rollback_unlocked`의 more-flag assert | 조치 없음 | 현재 flush 경계에서 성립한다. 코드 변경 없음 |

### 9.2 §8.4 필수 test 반영

| 요구 | 반영 |
| --- | --- |
| §7 7개 조건 | `test_single_pipe_lb_rolls_back_byte_hwm_rejected_multipart`가 rollback 뒤 수신 결과가 정확히 새 single-part message인지 확인한다 |
| monitoring byte 보완 | §8.4의 "monitoring byte로 함께 검증"은 채택하지 않았다. commit 전 multipart frame은 `bytesInFlight`에 포함되지 않으므로 monitoring byte만으로는 rollback을 증명할 수 없다. 대신 수신 결과 자체를 단정한다 |
| dist/XPUB 재현 test | `test_single_pipe_dist_rolls_back_byte_hwm_rejected_multipart` |
| byte credit 무누수 | rollback 뒤 후속 single-part 전송 성공으로 확인한다 |
| weighted 경로 회귀 고정 | `test_weighted_dealer_preserves_peer_weight_after_backpressure`, `test_weighted_lb_reactivation_keeps_configured_weight` |

### 9.3 review 범위 밖에서 확정한 근본 원인 — completion credit 회복

§8은 multipart rollback만 다뤘다. 같은 후보에서 이어진 perf 실패
(`router reply failed rc=1 errno=11`, 약 4 MiB 이후 회복 불가)는 별개 결함이었고
원인을 다음으로 확정했다.

Reply는 `send()`/`recv()`를 거치지 않고 completion pipe에 직접 write한다. 그래서
reply submit 경로가 socket command를 전혀 배수하지 않았다. Completion pipe가 byte
HWM에 도달하면 peer가 보낸 activate-write command가 mailbox에 남고 pipe의
`_out_active`가 false로 고정되어, 모든 retry가 영구적으로 `EAGAIN`으로 끝난다.

수정은 send 경로가 이미 하는 것과 같은 일을 reply submit 경로에서도 하는 것이다 —
기존 throttled mailbox pump(`process_commands(0, true)`)를 한 번 호출한다. 새 queue,
message별 atomic, 새 wire state를 추가하지 않았다.

| 항목 | 내용 |
| --- | --- |
| 회귀 test | `test_router_reply_completion_backpressure_recovers_over_tcp` |
| test가 고정하는 것 | request 전 `ZLINK_POLLCOMPLETION` 등록, 수신 payload를 그대로 reply로 재사용, completion callback의 payload 일치, HWM/LWM을 여러 cycle 반복 통과, 소비 시 credit 회복, timeout·재연결 없이 진행 |
| 음성 확인 | 수정을 비활성화하면 같은 test가 backpressure에서 회복하지 못해 실패한다(30초 deadline 초과) |
| connection ID 경로 | 같은 test가 함께 고정한다. reply가 application connection ID를 그대로 들고 있으면 completion session이 stale로 폐기하므로 completion callback이 도착하지 않는다 |

### 9.4 perf fixture 판정

`bindings/c/perf/single/common/perf_single_reqrep.hpp`의 미완료 변경은 그대로
승인하지 않고 정리했다.

- Reply API가 backpressure에서도 message를 소비하므로 retry에는 새 payload가
  필요하다. 그러나 이전 변경은 **모든 reply마다** `zlink_msg_copy()`로 사본을
  유지해 정상 경로에 복사를 넣었다.
- 정리한 구조는 첫 시도를 zero-copy로 유지하고(수신 message를 그대로 reply로
  넘긴다) backpressure가 실제로 발생한 드문 경로에서만 같은 크기의 payload를
  새로 만든다.
- request timeout을 늘리거나 HWM을 키워 증상을 숨기지 않았다.

정식 비교는 baseline worktree(`8bc2aa6786`)에 같은 reply 경로를 이식한 뒤 같은
runner 호출로 측정했다. 측정 경로의 code는 양쪽이 동일하다.

```bash
bindings/c/perf/run_benchmarks.sh --pattern DEALER_ROUTER_REQREP \
  --msg-sizes 64 --runs 3
```

| transport | count HWM (msg/s) | byte HWM (msg/s) | 변화 | count HWM p99 | byte HWM p99 |
| --- | --- | --- | --- | --- | --- |
| tcp | 261,030 | 330,500 | +26.6 % | 0.376 ms | 0.282 ms |
| tls | 227,870 | 357,470 | +56.9 % | 0.430 ms | 0.265 ms |
| ws | 234,520 | 360,570 | +53.8 % | 0.431 ms | 0.274 ms |
| wss | 215,340 | 272,690 | +26.6 % | 0.455 ms | 0.321 ms |
| inproc | 347,740 | 578,370 | +66.3 % | 0.262 ms | 0.113 ms |
| ipc | 252,450 | 362,210 | +43.5 % | 0.390 ms | 0.228 ms |

세 실행의 median이고 두 report 모두 `status=complete`다. Byte HWM이 모든
transport에서 throughput이 높고 tail latency가 낮다. inproc 값은 9.6 수정
이후의 것이다. 수정 전에는 byte HWM 쪽 inproc이 readiness timeout으로
실행되지 않았다.

### 9.7 memory amplification 측정

설계 문서 §4.4의 정의대로 측정했다.

```text
coreMemoryAmplification = processMemoryIncrease / accountedApplicationMessageBytes
```

하네스는 `core/study/hwm-bytes/`다. inproc pipe 하나를 reader가 받지 않는 상태로
채우므로 backlog가 kernel buffer로 새지 않고 process 안에 남는다. 분모는 payload
추정이 아니라 monitor snapshot의 `snd_bytes_in_flight`를 읽어 routing frame,
metadata와 frame별 최소 과금을 포함한다. RSS는 채우기가 멈춘 뒤 읽고, 기준선은
process 시작이 아니라 connection 성립 직후 값이다.

```bash
cmake -S core/study/hwm-bytes -B core/study/hwm-bytes/build -DCMAKE_BUILD_TYPE=Release
cmake --build core/study/hwm-bytes/build -j 4
core/study/hwm-bytes/build/hwm_memory_amplification <payload_bytes> 67108864
```

| payload | 적재 message 수 | accounted byte | RSS 증가 | accounted 기준 비율 | payload 기준 비율 |
| --- | --- | --- | --- | --- | --- |
| 64 B | 1,048,576 | 134,217,728 | 184,811,520 | **1.377** | 2.754 |
| 1 KiB | 123,361 | 134,216,768 | 140,083,200 | 1.044 | 1.109 |
| 64 KiB | 2,046 | 134,217,600 | 134,348,800 | 1.001 | 1.002 |

Accounted byte가 설정한 HWM(64 MiB)의 두 배인 것은 inproc이 sender와 receiver의
HWM을 합산하기 때문이다. 최소 frame 과금은 64 B이므로 64 B payload는 message마다
128 B로 계산된다.

기록하는 값은 작은 message의 최악값 **1.38**이다. Message가 커지면 payload가
지배해 1.0에 수렴한다. Framework가 payload byte만 센다면 같은 조건에서 2.75를
사용해야 한다.

### 9.6 review 범위 밖에서 확정한 근본 원인 — inproc pair readiness

C perf runner를 pattern 전체로 돌리자 `DEALER_ROUTER_REQREP`,
`DEALER_DEALER`, `ROUTER_ROUTER`가 inproc에서만 실패했다. `PAIR`와 `PUBSUB`은
같은 transport에서 통과한다. 최소 재현 program으로 확인한 증상은 데이터 경로가
아니라 monitor event다. inproc DEALER→ROUTER에서 send·recv는 정상인데
`ZLINK_EVENT_CONNECTION_READY`가 양쪽 socket 모두에 오지 않았다. 같은 program을
tcp로 돌리면 양쪽 모두 1건을 받는다. perf fixture는 request 전에 readiness를
기다리므로 그 대기에서 timeout이 났다.

원인은 두 가지가 겹쳤다.

| 지점 | 원인 |
| --- | --- |
| `socket_base_endpoint.cpp` inproc connect | paired transport일 때 ready 발행을 건너뛰고 `attach_pipe`에 맡겼다 |
| `socket_base_api.cpp` `attach_pipe` | pipe endpoint identifier가 `inproc://`로 시작할 때만 발행했다. inproc pipe는 endpoint pair를 갖지 않으므로 이 조건은 성립하지 않는다 |

여기에 pair readiness key 자체의 결함이 하나 더 있었다. key가 peer routing id를
포함하고 있어서, 두 lane이 peer identity를 서로 다른 시점에 알게 되는 경우 같은
pair가 서로 다른 key로 갈라졌다. bind가 나중에 오는 pending connect에서 실제로
그렇다. 이 경우 두 lane이 각각 절반만 ready로 표시되어 0x03에 도달하지 못한다.

수정은 inproc 경로가 lane마다 pair-aware 발행 entry를 호출하게 하고, pair
readiness key에서 routing id를 제거해 `(endpoint, pair id, generation)`으로
식별하게 했다. pair id는 connect마다 새로 만드는 64-bit random 값이고 handshake로
peer에 전달되므로 pair를 단독으로 식별한다. routing id는 식별에 아무 것도 더하지
않으면서 위의 분열만 만들었다.

| 항목 | 내용 |
| --- | --- |
| 회귀 test | `test_monitor_socket_contract`의 `test_inproc_dealer_router_ready_after_bind`, `test_inproc_dealer_router_ready_after_pending_connect`, `test_inproc_two_dealers_ready_once_each` |
| test가 고정하는 것 | bind 선행·connect 선행 두 순서, socket마다 ready event 정확히 1건, 한 inproc endpoint의 두 peer가 각각 1건, ready 후 실제 data 전달 |
| 음성 확인 | inproc 발행을 되돌리면 `..._after_bind`가 실패한다 |
| commit | `0830b29317` |

이 결함은 byte HWM 회귀가 아니라 Application·Completion pair(C-05)의 inproc
누락이다. Core test 80/80이 통과하던 이유는 paired transport의 inproc readiness
coverage가 없었기 때문이다. 그래서 1단계 transport matrix가 통과했다는 기존
기록은 inproc paired readiness를 포함하지 않는다. 위 test 3건이 그 공백을 메운다.

### 9.5 남은 위험 갱신

§8.5의 항목 중 다음이 해소되었다.

- dist 결함: 수정 완료(9.1).
- multipart 총량 조기 admission: 수정 완료. 설계 문서 문장과 구현이 일치한다.

다음은 그대로 남는다.

- 기존 raw multipart send의 blocking `-2` 호환 동작. logical helper만 `EAGAIN`으로
  보고하도록 바꿨다.
- PAIR·STREAM 등 다른 socket type의 write 실패 경로는 이 범위에서 검토하지 않았다.
