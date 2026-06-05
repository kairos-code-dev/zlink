# TLS multipart recv view 리팩토링 상세 설계

> 범위:
> [`core/include/zlink.h`](/home/hep7/project/kairos/zlink/core/include/zlink.h),
> [`core/src/api/`](/home/hep7/project/kairos/zlink/core/src/api),
> [`core/src/services/spot/`](../../../core/src/services/spot),
> [`core/tests/`](/home/hep7/project/kairos/zlink/core/tests),
> [`core/perf/`](../../../core/perf),
> [`core/bench/`](../../../core/bench),
> [`bindings/cpp/`](/home/hep7/project/kairos/zlink/bindings/cpp)

## 1. 목적

현재 public multipart recv surface는 `zlink_recv()` / `zlink_subscribe()`가
`zlink_msg_t *parts[]` 배열을 heap에 할당해 caller에게 넘기고, caller가
`zlink_multipart_close(parts, count)` 뒤에 `free(parts)`까지 수행하는 구조다.

이 구조는 다음 문제를 만든다.

- single-part 메시지에서도 메시지마다 `malloc/free(parts)`가 발생한다.
- `zlink_msg_t` 개별 ownership과 배열 container ownership이 한 API에 섞여 있다.
- callback payload shape와 direct recv payload shape를 맞추려는 일반화가 hot path에
  그대로 전파된다.
- small-message perf에서 공통 고정비가 커진다.

이번 리팩토링의 목표는 다음이다.

- multipart recv 결과의 **message ownership**만 caller에게 넘긴다.
- `parts` 배열 그릇은 internal thread-local storage(TLS) view로 제공한다.
- caller는 **개별 message만 close**하고 `free(parts)`는 더 이상 하지 않는다.
- `spot`을 포함한 direct recv/subscribe surface를 같은 lifetime 규칙으로 통일한다.
- send 경로는 `single-part`와 `multipart`가 가능한 한 같은 처리 구조를 공유하고,
  차이는 `part` 반복과 `more`/rollback 필요성만 남도록 정리한다.

## 2. 최종 public 계약

### 2.1 `zlink_recv()` / `zlink_subscribe()` 반환 규약

`zlink_recv()`와 `zlink_subscribe()`는 다음 새 계약을 가진다.

- `parts_out`는 caller-owned heap 배열이 아니다.
- `parts_out`는 같은 스레드에서 다음 recv 계열 호출 전까지만 유효한 TLS view다.
- caller는 `free(parts_out)`를 호출하면 안 된다.
- caller는 각 part에 대해 `zlink_msg_close(&parts_out[i])`만 호출한다.
- `part_count_out`는 해당 TLS view에 채워진 part 개수다.

recv 계열 호출은 아래를 모두 포함한다.

- `zlink_recv()`
- `zlink_subscribe()`
- 내부적으로 같은 TLS recv view를 소비하는 socket/spot direct recv helper

### 2.2 `zlink_multipart_close()` 의미

`zlink_multipart_close(parts, count)`는 더 이상 배열 storage를 해제하지 않는다.

새 의미:

- `parts[0..count-1]` 각각에 대해 `zlink_msg_close()`를 호출하는 close-only helper
- `parts` 포인터가 TLS view여도 호출 가능
- `free(parts)`를 포함하지 않음

즉 `zlink_multipart_close()`는 convenience helper로만 남고, ownership release는
개별 message close에만 귀속된다.

### 2.3 lifetime 규약

TLS view lifetime은 다음으로 고정한다.

- 같은 스레드에서 다음 recv 계열 호출 전까지 유효
- 다른 스레드로 넘겨서 사용하면 안 됨
- caller가 message를 close하지 않았더라도 다음 recv 시 view는 덮일 수 있음

이 규약은 libzmq의 frame-by-frame recv와 똑같지는 않지만, aggregate payload shape를
유지하면서 heap container 제거를 가능하게 하는 최소 규칙이다.

## 3. 비목표

이번 단계에서는 다음을 하지 않는다.

- Python/Node/.NET/Java binding까지 동시 변경
- multipart cap 초과 시 heap fallback 추가
- callback handler payload shape 변경
- public `zlink_send()` multipart ownership 규약 변경

언어 바인딩은 native 계약이 안정화된 뒤 별도 후속 작업으로 분리한다.

다만 send 경로의 내부 clone 제거와 fast path 정리는 이번 리팩토링과 같은 흐름으로
설계하고 구현한다.

## 4. 설정 정책

### 4.1 TLS multipart recv cap

TLS view는 고정 최대 part 수를 가진다. 기본값은 `2`로 둔다.

이 cap은 **사용자에게 반환되는 payload part 개수 기준**이다.

- callback handler의 `parts_ + part_count_`
- direct recv/subscribe의 `parts_out + part_count_out`

즉 사용자는 이 payload part 개수만 신경 쓰면 되고, 내부 framing은 cap 정의에 들어가지
않는다.

내부 framing 예:

- routing id frame
- topic frame
- send prefix frame

이들은 구현이 내부 조립/전송에서 별도로 처리해야 하는 frame이며, public payload cap
계산에는 넣지 않는다.

이 값은 실제 **external payload shape** 대부분이 아래로 수렴한다는 전제를 둔다.

- single payload recv: `1`
- 사용자에게 노출되는 multipart payload: 많아야 `2`

설정 방식:

- 새 context option 또는 전역 socket-api option으로 노출
- 이름은 `ZLINK_CTX_OPT_RECV_TLS_PART_CAP` 또는 동등한 새 option으로 추가
- 값은 `1` 이상 정수만 허용
- 기본값 `2`

### 4.2 cap 초과 정책

이번 단계는 dynamic fallback을 두지 않는다.

- actual part count가 TLS cap을 초과하면 `EMSGSIZE`
- `parts_out = NULL`, `part_count_out = 0`
- 이미 받은 message들은 내부에서 모두 close하고 에러 반환

이 정책을 택한 이유:

- 계약을 단순하게 유지
- heap fallback을 다시 넣어 성능/수명 모델을 섞지 않으려고

사전 확인 요구:

- native/core 내부에서 실제로 4개 이상 part를 반환하는 direct recv/subscribe 경로가
  없는지 같은 작업 안에서 확인한다.
- 만약 존재하면 같은 변경 안에서 그 경로를 함께 줄이거나, option 기본값을 높여
  전체 recv surface가 한 번에 일관된 cap 정책으로 닫히도록 수정한다.
- “나중에 heap fallback으로 우회”는 이번 단계에서 허용하지 않는다.

확인 기준은 “내부 raw frame 수”가 아니라 “최종 callback/direct recv로 사용자에게
반환되는 payload part 수”다.

## 5. core 내부 설계

### 5.1 TLS recv storage 구조

스레드별 recv storage 구조체를 둔다.

개념 구조:

```cpp
struct recv_tls_view_t {
    size_t cap;
    size_t count;
    zlink_msg_t parts[MAX_CAP_OR_DYNAMIC];
    bool initialized[MAX_CAP_OR_DYNAMIC];
};
```

실제 구현 요구:

- storage는 thread-local
- slot별 `zlink_msg_t` init 상태를 추적
- 새 recv 시작 전 이전 slot 상태를 정리(reset)
- close되지 않은 message가 남아 있어도 다음 recv 전에 내부가 회수할 수 있어야 함

reset 규칙:

- 이전 `count` 범위의 slot을 순회
- `initialized`인 slot은 `zlink_msg_close()`
- close가 끝난 slot은 즉시 `zlink_msg_init()`로 empty-valid 상태로 재초기화
- `count = 0`

중요한 점:

- caller가 이미 `zlink_msg_close()`를 호출한 slot이 있어도 다음 recv reset이 깨지면 안 됨
- 따라서 TLS storage는 slot별 `initialized` bit를 별도로 유지하고,
  caller close 여부와 무관하게 “next recv start 시 close-if-initialized -> init-empty”
  규약으로 복구하는 helper를 가진다.
- caller가 `zlink_msg_close()`를 호출한 뒤 그 slot을 다시 만지지 않는 것이 API 규약이며,
  다음 recv 전 reset은 TLS storage가 자기 bookkeeping 기준으로만 수행한다.

### 5.2 recv export 경로

현재 [socket_message_recv_api.cpp](../../../core/src/api/socket/socket_message_recv_api.cpp)
의 `move_single_part_to_output()` / `export_recv_frames()`는 `malloc(parts)`를 사용한다.

이 둘은 아래 방식으로 바꾼다.

- single-part: TLS slot `0`으로 move 후 `parts_out = tls.parts`
- multi-part: 각 frame을 TLS slot으로 move 후 `parts_out = tls.parts`
- `part_count_out = payload_count`

즉 export 단계가 `heap array materialization`이 아니라
`TLS slot population`으로 바뀐다.

### 5.3 multipart 후속 frame 수집 규칙

multipart recv는 `more` flag를 기준으로 frame 경계를 판단한다.

새 규칙:

- 첫 frame recv는 caller가 넘긴 `flags_`를 그대로 사용한다.
- 첫 frame에서 `more == false`면 즉시 single-part 완료로 처리한다.
- 첫 frame에서 `more == true`면 후속 frame은 multipart assembly 전용 내부 수집
  규약으로 읽는다.

이 규칙을 택하는 이유:

- multipart send는 이미 하나의 메시지 묶음으로 보장되어 전송된다.
- 첫 frame을 성공적으로 받았고 `more`가 보였다는 것은 같은 메시지의 후속
  frame들이 이미 이어지는 조립 대상이라는 뜻이다.
- 따라서 후속 frame 수집은 direct recv timeout과 분리된
  “현재 열린 multipart를 끝까지 조립하는 내부 단계”로 다뤄야 한다.

후속 frame 수집 실패 규칙:

- 후속 frame 수집 중 일반 `EAGAIN`/timeout semantics를 caller에게 그대로 노출하지
  않는다.
- multipart가 시작된 뒤에는 필요한 후속 frame을 내부에서 끝까지 조립한다.
- 조립 중 비정상 오류가 나면 내부에서 이미 받은 frame들을 close하고 에러를 반환한다.

즉 multipart recv의 blocking point는 첫 frame에만 있고, 그 이후는
internal assembly 단계로 고정한다.

### 5.4 single-part fast path와의 관계

이미 추가된 direct fast path는 유지한다.

- `zlink_msg_recv()`
- `zlink_msg_recv_rid()`
- `zlink_msg_send()`

역할 분리:

- truly single-frame hot path가 필요하면 `zlink_msg_recv()` 사용
- aggregate payload shape가 필요하면 `zlink_recv()` TLS view 사용

이렇게 하면:

- single-part direct path는 최저 비용을 유지하고
- 기존 aggregate 사용처는 heap container만 제거한다.

### 5.5 routed / stream / router payload

`ROUTER`/`STREAM` 계열은 routing id stripping 규칙을 그대로 유지한다.

현재처럼:

- `ROUTER + source_rid_out != NULL`
- `STREAM + source_rid_out != NULL`
- `payload_offset` 계산 후 payload part만 caller view로 노출

다만 결과 export는 heap 배열이 아니라 TLS view로 바꾼다.

### 5.6 pubsub / subscribe 계열

`zlink_subscribe()`는 topic frame + payload frame을 수신한 뒤 payload part만 TLS view에
노출한다.

topic의 처리 규칙은 그대로 유지한다.

- `topic_id_out_ == NULL`이면 길이만 채움
- topic buffer 부족 시 `EMSGSIZE`
- payload가 없는 subscribe면 `part_count_out = 0`

topic-only subscribe 정책:

- `SUB`, `XSUB`, `SPOT` direct subscribe 경로 모두 topic-only 메시지를 정상으로 허용한다.
- 즉 topic frame을 성공적으로 읽었고 `more == false`면 `part_count_out = 0`,
  `parts_out = NULL`로 성공 반환한다.
- topic-only를 `EPROTO`로 취급하는 예외 경로는 두지 않는다.

subscribe frame assembly 규칙은 multipart recv 규칙과 동일하게 고정한다.

- topic frame은 caller가 넘긴 `flags_`로 수신한다.
- topic frame에서 `more == false`면 payload 없는 subscribe로 즉시 종료한다.
- topic frame에서 `more == true`면 payload 및 나머지 후속 frame은 internal
  assembly 단계로 수집한다.

즉 `zlink_subscribe()`도 첫 frame(topic)에만 caller flags 기반 blocking point가 있고,
그 이후 payload 조립은 internal assembly 단계로 처리한다.

## 6. spot 포함 서비스 계층 정렬

### 6.1 spot recv surface

`spot_subject_recv` 및 그 위의 public `zlink_subscribe()` 경로도 동일 계약으로 바꾼다.

즉 `spot`에서도:

- caller는 `free(parts)`를 하지 않음
- `zlink_msg_close()` 또는 `zlink_multipart_close()`만 사용
- TLS view lifetime은 동일

### 6.2 callback과 direct recv의 shape 정렬

callback handler는 이미 `parts_ + part_count_` shape를 받는다.
이번 변경 뒤 direct recv도 같은 aggregate shape를 유지하되, ownership 규칙만
달라진다.

정렬 결과:

- callback: callee delivers `parts_`, callback returns, framework closes
- direct recv: TLS view delivers `parts_`, caller closes message only

즉 shape는 유지하고, direct recv의 heap container ownership만 제거한다.

## 7. 테스트/도구 영향 범위

### 7.1 core tests

다음 류의 테스트를 전부 새 계약으로 바꾼다.

- `zlink_recv(..., &parts, &part_count, ...)` 뒤 `free(parts)` 하던 테스트
- `zlink_subscribe(..., &parts, &part_count, ...)` 뒤 `free(parts)` 하던 테스트
- `spot` e2e/introspection tests
- monitor / socket contract tests
- multi socket contract regression tests

수정 원칙:

- `free(parts)` 제거
- 필요하면 `zlink_multipart_close(parts, part_count)`만 호출
- 또는 명시적 `for (i) zlink_msg_close(&parts[i])`

추가 검증:

- 같은 스레드에서 두 번 연속 recv 시 첫 view가 덮이는 규약 테스트
- caller가 close 안 한 상태에서 다음 recv 시 내부가 누수 없이 reset되는지 테스트
- cap 초과 시 `EMSGSIZE` 테스트

### 7.2 perf / bench

다음 경로의 `free(parts)`를 제거한다.

- `core/perf/multi`
- `core/perf/single`
- `core/bench/with_zmq`
- `core/bench/with_routing`

특히 다음 케이스를 다시 측정한다.

- single: `PAIR`, `DEALER_DEALER`, `DEALER_ROUTER`, `PUBSUB`, `ROUTER_ROUTER`
- multi: `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `PUBSUB`, `STREAM`
- transport:
  - multi: `tcp`, `ipc`
  - multi stream: `tcp`
  - single: 기존 정책 유지

성공 기준:

- single small-message에서 최근 회귀 패턴이 사라질 것
- heap-based aggregate export가 제거되어 `PAIR/DEALER_*`의 throughput ceiling이
  회복될 것

### 7.3 C++ wrapper

이번 단계는 C++ wrapper까지 같이 맞춘다.

현재 wrapper는 native `parts`를 받아 `zlink_multipart_close + free(parts)`를 전제로
움직인다. 이를 아래로 바꾼다.

- native view를 복사/이동해 wrapper-owning `std::vector<message_t>`로 변환
- native side에는 `free(parts)` 호출 금지
- 필요 시 native part close만 수행
- wrapper 승격 중 중간 실패가 나면 아직 wrapper로 move되지 않은 native view part는
  `zlink_multipart_close(parts, count)`로 정리하고 종료한다
- socket wrapper와 `service::spot` wrapper가 같은 helper/규약을 공유하도록 맞춘다

즉 wrapper boundary에서 native TLS lifetime을 끊고, C++ 객체 lifetime으로 다시
승격시킨다.

## 8. 문서/API 주의사항

### 8.1 breaking change 공지

이번 변경은 명백한 breaking change다. 문서와 changelog에 최소한 아래를 명시해야 한다.

- `zlink_recv()` / `zlink_subscribe()`는 더 이상 caller-owned `parts` 배열을 반환하지 않음
- `free(parts)`는 금지
- `parts`는 TLS recv view이며 다음 recv 전까지만 유효
- 각 part는 caller가 `zlink_msg_close()` 해야 함

### 8.2 helper 사용 권장

사용자 문서 예시는 모두 아래 형태로 바꾼다.

```c
zlink_msg_t *parts = NULL;
size_t count = 0;
if (zlink_recv(sock, NULL, &parts, &count, 0) == 0) {
    for (size_t i = 0; i < count; ++i)
        zlink_msg_close(&parts[i]);
}
```

또는:

```c
zlink_multipart_close(parts, count);
```

`free(parts)` 예시는 문서/샘플에서 제거한다.

## 9. send 경로 정렬 원칙

recv 리팩토링과 함께 send도 다음 원칙으로 정렬한다.

### 9.1 목표 구조

`single-part`와 `multipart`는 가능한 한 같은 send 구조를 공유해야 한다.

수정 뒤에 남아야 하는 차이는 아래뿐이다.

- `part_count == 1`이면 send loop 1회
- `part_count > 1`이면 part 수만큼 send loop 반복
- multipart일 때만 마지막 전 part에 `more` 설정
- multipart일 때만 rollback 상태가 의미를 가짐

즉 send 경로의 차이는 frame 개수와 `more`/rollback semantics만 남고,
정상 경로의 별도 clone/temporary container 비용은 제거한다.

### 9.2 제거 대상

현재 [multipart_send_txn.cpp](../../../core/src/runtime/core/multipart_send_txn.cpp)
의 다음 구조는 제거 대상이다.

- 매 attempt마다 `std::vector<msg_t>` clone
- blocking retry 전제 때문에 정상 경로도 clone을 선행
- `part_count == 1`이어도 multipart txn 진입

즉 “retry 가능성 때문에 모든 정상 send를 clone한다”는 현재 정책은 폐기한다.

### 9.3 최종 send 정책

- `zlink_send(parts, part_count)`
  - `part_count == 1`이면 direct msg send fast path
  - `part_count > 1`이면 원본 parts를 그대로 순서대로 send
- `zlink_send_rid(parts, part_count)`
  - `part_count == 1`이면 direct routed send fast path
  - `part_count > 1`이면 원본 parts direct routed multipart send
- `zlink_publish(topic, parts, part_count)`
  - topic prefix frame은 local frame으로 생성
  - payload parts는 clone 없이 원본 direct send

### 9.4 실패/성공 규약

- 성공 시에만 caller 원본 parts를 consume/reset
- 실패 시 caller 원본 parts는 payload/shape/재사용 가능 상태를 유지해야 함
- multipart 중간 실패 시 `socket->rollback()`으로 소켓 상태를 되감는다
- nonblocking send는 1회 시도 뒤 즉시 반환
- blocking send는 retry가 필요할 때만 wait/retry를 수행하되, retry를 위해 payload를
  clone하지 않는다

원본 parts 보존 계약을 더 구체적으로 고정한다.

- send 실패 뒤 caller는 같은 `parts[]`를 그대로 다시 `zlink_send()` /
  `zlink_send_rid()` / `zlink_publish()`에 넘길 수 있어야 한다.
- send 실패 경로는 caller 원본 `zlink_msg_t`의 payload ownership, part count, part 순서,
  routing/publish용 payload shape를 훼손하면 안 된다.
- 원본 part consume/reset은 오직 성공 경로에서만 일어난다.

### 9.5 구현상 확인 항목

send clone 제거는 아래 항목을 같은 변경 안에서 모두 만족시키는 것을 목표로 한다.

- socket family별 `rollback()`이 partial multipart를 충분히 되감는지
- `PAIR`, `DEALER`, `ROUTER`, `PUB/XPUB`, `STREAM`, `SPOT` publish 경로가
  같은 규칙을 만족하는지
- single-part direct send와 multipart direct send가 같은 ownership 규약을 유지하는지

구현 시작 순서는 아래로 고정한다.

1. `PAIR`/`DEALER` 계열 single-part direct send fast path
2. `ROUTER` routed single-part direct send fast path
3. multipart direct send + rollback 검증
4. publish/topic prefix 경로 clone 제거
5. stream/spot publish 경로 정렬

이 순서는 작업 분할을 뜻하지 않는다. 한 변경 안에서 위 순서대로 구현/검증을
진행하고, 최종 머지 시점에는 모든 socket family와 spot publish 경로가 모두 새
send 규약으로 닫혀 있어야 한다.

## 10. 구현 순서

1. `zlink.h` 문서와 계약 주석 갱신
2. `socket_message_recv_api.cpp`에 TLS recv storage 도입
3. `malloc(parts)` 기반 export 제거
4. `zlink_multipart_close()`를 close-only helper로 정리
5. `spot` recv/subscribe 경로 정렬
6. send clone 경로를 제거하고 single/multipart 공통 send 구조로 정렬
7. `core/tests`의 `free(parts)` 제거 및 새 lifetime/send 규약 테스트 추가
8. `core/perf`, `core/bench`, `bindings/cpp` 정렬
9. single/multi bench 재검증

위 순서는 하나의 완결된 변경을 위한 내부 작업 순서다.

- 중간 단계에서 recv만 머지하거나 send만 남겨 두지 않는다.
- native API, tests, perf/bench, C++ wrapper, spot 경로가 모두 새 계약으로 닫힌
  상태를 최종 완료 상태로 본다.
- “후속으로 같은 문서를 이어서 구현”하는 잔여 단계는 남기지 않는다.

## 11. 리스크와 대응

### 10.1 lifetime 오해

가장 큰 리스크는 caller가 TLS view를 오래 들고 있는 경우다.

대응:

- 헤더/문서에 강하게 명시
- 연속 recv 시 덮임을 검증하는 회귀 테스트 추가

### 10.2 cap 초과

일부 실제 multipart shape가 기본 cap `3`을 넘길 수 있다.

대응:

- 기본 옵션 노출
- cap 초과 시 명시적 `EMSGSIZE`
- 테스트에서 초과 동작을 고정

### 10.3 wrapper/샘플 잔존 `free(parts)`

native 계약이 바뀐 뒤 예전 관용구가 남아 있으면 즉시 문제를 만든다.

대응:

- `rg "free\\(parts"` 전체 정리
- `zlink_multipart_close + free(parts)` 패턴을 전수 제거

### 11.4 send rollback 보장

send clone 제거 뒤 가장 큰 리스크는 multipart 중간 실패 시 원본과 socket state가
엇갈리는 경우다.

대응:

- socket family별 rollback contract를 먼저 점검
- partial send failure regression test를 추가
- single-part / multipart / routed / publish를 각각 별도 검증

## 12. 완료 기준

아래가 모두 만족되면 이번 리팩토링이 완료된 것으로 본다.

1. `zlink_recv()` / `zlink_subscribe()` 문서가 TLS recv view 계약으로 갱신되어 있다.
2. core recv 구현에서 `malloc(parts)` 기반 export가 제거되어 있다.
3. `zlink_multipart_close()`는 close-only helper가 되어 있다.
4. native/core/tests/perf/bench/C++ wrapper에 `free(parts)`가 남아 있지 않다.
5. `spot` 포함 direct recv/subscribe 경로가 같은 lifetime 규약을 따른다.
6. send 경로에서 정상 hot path의 frame clone이 제거되어 있다.
7. cap 초과, 연속 recv view overwrite, message close-only cleanup, multipart send
   rollback에 대한 테스트가 있다.
8. single small-message perf 회귀가 재측정 기준으로 유의미하게 완화된다.
