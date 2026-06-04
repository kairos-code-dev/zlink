# Msg-Only Data Plane Realignment Plan

> 상태: 현재 `core` public data-plane 정렬 작업의 기준 문서.
> 후속 pub/sub naming 재편 초안은
> `pubsub-public-surface-renaming-plan.ko.md`에서 별도로 다룬다.
> 후속 초안이 승인되기 전까지는 이 문서가 구현/회귀 수정의 source of truth다.

이 문서는 `core` public data-plane API를 `zlink_msg_t` 중심으로 재정렬한
결과와 최종 계약을 정리한다.

목적:

- public send/recv 계약을 callback 모델과 동일한 정보 모델로 통일한다.
- bytes convenience API를 제거하고 `msg_t` ownership 기반 surface만 남긴다.
- direct recv와 callback recv의 shape 차이를 없앤다.
- compatibility보다 단순한 canonical surface를 우선한다.
- 일반 multipart data-plane과 topic/subscription event plane을 분리한다.

우선순위:

- 이 문서는 `core` data-plane surface 정리의 작업 메모다.
- 구현 전 상세 ABI 이름과 단계별 제거 정책은 이 문서를 기준으로 정리한다.
- 기존 direct-callback-recv 문서와 충돌하면 "recv는 callback과 동일한 multipart
  + source_rid shape" 원칙을 우선한다.
- 기존 bytes API와의 source compatibility / ABI compatibility는 유지 목표가 아니다.

## 1. 결정 사항

### 1.1 Public recv 표준형

- public recv 기본형은 callback과 동일하게 `source_rid + multipart payload`를
  반환해야 한다.
- direct recv는 callback의 동기식 버전으로 해석한다.
- direct recv와 callback recv는 payload shape가 같아야 한다.
- routing envelope은 public payload에서 제거하고 `source_rid`로 분리한다.
- `source_rid`는 "이 메시지를 실제로 보낸 remote source identity"다.

### 1.2 Bytes API 제거

- `void *data + size` 기반 public data-plane API는 제거되었다.
- 기존 bytes 버전 `zlink_recv()`는 유지하지 않는다.
- 기존 bytes 버전 `zlink_send()`도 유지하지 않는다.
- raw bytes convenience helper를 기준 surface로 두지 않는다.
- deprecated 단계 없이 기존 bytes API를 제거하고 canonical 이름을 새 계약으로 재사용한다.

### 1.3 Msg API 중심 정렬

- public data-plane은 `zlink_msg_t` ownership 모델만 기준으로 삼는다.
- recv 기본형은 single-part가 아니라 multipart를 기본으로 삼는다.
- single message convenience helper는 유지하지 않는다.
- recv shape를 single message 기준으로 설계하지 않는다.
- multipart 경계는 `parts + part_count`로만 표현한다.
- public send flags에서 `ZLINK_SNDMORE`는 제거되었다.

### 1.4 일반 data-plane과 예외 plane 분리

- 일반 multipart data-plane은 `zlink_send`, `zlink_send_rid`, `zlink_recv`로
  정리한다.
- `spot`은 topic metadata가 본질이므로 별도 recv/send family를 유지한다.
- `xpub`은 subscribe/unsubscribe event plane이므로 일반 multipart recv/send와
  합치지 않는다.
- "모든 public socket이 완전히 같은 send/recv 이름을 공유해야 한다"를 목표로
  두지 않는다.

## 2. Canonical API 방향

아래는 개념 방향이며, 최종 이름은 구현 직전 확정한다.

```c
int zlink_send (void *s,
                zlink_msg_t *parts,
                size_t part_count,
                zlink_send_flags_t flags);

int zlink_send_rid (void *s,
                    const zlink_routing_id_t *target_rid,
                    zlink_msg_t *parts,
                    size_t part_count,
                    zlink_send_flags_t flags);

int zlink_recv (void *s,
                zlink_routing_id_t *source_rid_out,
                zlink_msg_t **parts_out,
                size_t *part_count_out,
                zlink_send_flags_t flags);
```

의미:

- `zlink_send`는 peer 지정이 필요 없는 일반 multipart send다.
- `zlink_send_rid`는 target peer 지정이 필요한 routing send다.
- recv는 항상 multipart payload를 돌려준다.
- recv의 `parts_out` ownership은 caller에게 이전된다.
- caller는 성공 후 각 part에 대해 `zlink_msg_close()`를 호출하고, parts array
  자체는 `free()`로 해제한다.
- callback의 `source_rid + parts + part_count` shape와 direct recv의 shape가
  동일해야 한다.
- routing send가 필요한 socket/service만 `target_rid`를 explicit하게 다룬다.
- non-routing type도 "메시지를 보낸 remote source identity"를 `source_rid`로
  설명 가능한 방향을 기본으로 한다.

## 3. 제거/치환 결과

### 3.1 제거 완료 항목

- `zlink_recv(void *s, void *buf, size_t len, ...)`
- `zlink_send(void *s, const void *buf, size_t len, ...)`
- 그 외 public raw bytes send/recv convenience API
- 이 API들은 compatibility wrapper 없이 삭제한다.
- `zlink_stream_send`, `zlink_stream_send_msg` 같은 전용 data-plane send API는
  public `zlink_send_rid`로 통합되었다.
- `zlink_gateway_send`, `zlink_gateway_send_rid` 같은 전용 data-plane send API는
  public canonical send family로 통합되었다.
- `zlink_gateway_recv` 같은 전용 recv API는 public `zlink_recv`로 통합되었다.
- 위 통합은 public C API 이름/시그니처 기준이며, 내부 구현 분기까지 제거한다는
  뜻은 아니다.
- 기존 bytes/single-part send surface와 함께 public `ZLINK_SNDMORE`도 제거되었다.

### 3.2 재정의 또는 대체 대상

- 기존 `zlink_msg_recv(zlink_msg_t *msg, ...)`는 canonical recv가 아니다.
- recv 기본형이 multipart가 되면 기존 single-msg recv는 삭제한다.
- 기존 `zlink_msg_send(zlink_msg_t *msg, ...)`도 삭제한다.
- canonical 이름 `zlink_send` / `zlink_recv`는 새 multipart msg 계약이 승계한다.
- canonical send family는 `zlink_send` + `zlink_send_rid` 두 개로 정리한다.
- `spot` / `xpub`는 일반 canonical send/recv family에 흡수하지 않는다.
- `stream` / `gateway`는 public C API 기준으로 canonical family에 통합한다.

## 4. 현재/목표 API 비교

### 4.1 현재 public data-plane surface

| API | 반환값 | 파라미터 | 현재 의미 | 상태 |
| --- | --- | --- | --- | --- |
| `int zlink_send(void *s, const void *buf, size_t len, zlink_send_flags_t flags)` | 성공 시 전송 바이트 수, 실패 시 `-1` | `s`, `buf`, `len`, `flags` | raw bytes 단일 frame 전송 | 제거 완료 |
| `int zlink_recv(void *s, void *buf, size_t len, zlink_send_flags_t flags)` | 성공 시 수신 바이트 수, 실패 시 `-1` | `s`, `buf`, `len`, `flags` | raw bytes 단일 recv | 제거 완료 |
| `int zlink_msg_send(zlink_msg_t *msg, void *s, zlink_send_flags_t flags)` | 성공 시 전송 바이트 수, 실패 시 `-1` | `msg`, `s`, `flags` | single message 전송, ownership 소비 | 제거 완료 |
| `int zlink_msg_recv(zlink_msg_t *msg, void *s, zlink_send_flags_t flags)` | 성공 시 수신 바이트 수, 실패 시 `-1` | `msg`, `s`, `flags` | single message recv | 제거 완료 |
| `int zlink_stream_send(void *s, const zlink_routing_id_t *rid, const void *data, size_t size, zlink_send_flags_t flags)` | 성공 시 payload 바이트 수, 실패 시 `-1` | `s`, `rid`, `data`, `size`, `flags` | STREAM 전용 bytes send | 제거 완료 |
| `int zlink_stream_send_msg(void *s, const zlink_routing_id_t *rid, zlink_msg_t *msg, zlink_send_flags_t flags)` | 성공 시 payload 바이트 수, 실패 시 `-1` | `s`, `rid`, `msg`, `flags` | STREAM 전용 single-msg send | 제거 완료 |
| `int zlink_gateway_send(void *gateway, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags)` | 성공 시 `0`, 실패 시 `-1` | `gateway`, `parts`, `part_count`, `flags` | service-bound multipart send | public `zlink_send`로 통합 완료 |
| `int zlink_gateway_send_rid(void *gateway, const zlink_routing_id_t *routing_id, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags)` | 성공 시 `0`, 실패 시 `-1` | `gateway`, `routing_id`, `parts`, `part_count`, `flags` | target rid 명시 multipart send | public `zlink_send_rid`로 통합 완료 |
| `int zlink_gateway_recv(void *gateway, zlink_routing_id_t *source_rid_out, zlink_msg_t **parts, size_t *part_count, int flags)` | 성공 시 `0`, 실패 시 `-1` | `gateway`, `source_rid_out`, `parts`, `part_count`, `flags` | `source_rid + multipart payload` recv | public `zlink_recv`로 통합 완료 |
| `int zlink_spot_node_recv(void *node, zlink_msg_t **parts, size_t *part_count, int flags, char *topic_id_out, size_t *topic_id_len)` | 성공 시 `0`, 실패 시 `-1` | `node`, `parts`, `part_count`, `flags`, `topic_id_out`, `topic_id_len` | topic-aware multipart recv | `source_rid` 정렬 완료 |
| `int zlink_spot_sub_recv(void *sub, zlink_msg_t **parts, size_t *part_count, int flags, char *topic_id_out, size_t *topic_id_len)` | 성공 시 `0`, 실패 시 `-1` | `sub`, `parts`, `part_count`, `flags`, `topic_id_out`, `topic_id_len` | topic-aware multipart recv | `source_rid` 정렬 완료 |

### 4.2 목표 canonical surface

| API | 반환값 | 파라미터 | 목표 의미 | 비고 |
| --- | --- | --- | --- | --- |
| `int zlink_send(void *s, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags)` | 성공 시 `0`, 실패 시 `-1` | `s`, `parts`, `part_count`, `flags` | raw `PAIR`, `DEALER`, `ROUTER`, `gateway`의 non-directed send canonical | canonical |
| `int zlink_send_rid(void *s, const zlink_routing_id_t *target_rid, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags)` | 성공 시 `0`, 실패 시 `-1` | `s`, `target_rid`, `parts`, `part_count`, `flags` | target peer 지정 multipart send canonical | canonical |
| `int zlink_recv(void *s, zlink_routing_id_t *source_rid_out, zlink_msg_t **parts_out, size_t *part_count_out, zlink_send_flags_t flags)` | 성공 시 `0`, 실패 시 `-1` | `s`, `source_rid_out`, `parts_out`, `part_count_out`, `flags` | 일반 direct recv canonical. callback과 동일한 `source_rid + multipart payload` 반환 | canonical |
| `int zlink_spot_node_recv(void *node, zlink_routing_id_t *source_rid_out, zlink_msg_t **parts, size_t *part_count, char *topic_id_out, size_t *topic_id_len, zlink_send_flags_t flags)` | 성공 시 `0`, 실패 시 `-1` | `node`, `source_rid_out`, `parts`, `part_count`, `topic_id_out`, `topic_id_len`, `flags` | `source_rid + topic + multipart payload` recv | 확장 정렬 대상 |
| `int zlink_spot_sub_recv(void *sub, zlink_routing_id_t *source_rid_out, zlink_msg_t **parts, size_t *part_count, char *topic_id_out, size_t *topic_id_len, zlink_send_flags_t flags)` | 성공 시 `0`, 실패 시 `-1` | `sub`, `source_rid_out`, `parts`, `part_count`, `topic_id_out`, `topic_id_len`, `flags` | `source_rid + topic + multipart payload` recv | 확장 정렬 대상 |
| `int zlink_xpub_recv(void *s, zlink_routing_id_t *source_rid_out, int *subscribed_out, char *topic_id_out, size_t *topic_id_len, zlink_send_flags_t flags)` | 성공 시 `0`, 실패 시 `-1` | `s`, `source_rid_out`, `subscribed_out`, `topic_id_out`, `topic_id_len`, `flags` | subscribe/unsubscribe event metadata 중심 | 전용 recv family 유지 |

### 4.3 Callback/direct recv shape 대응표

| 수신 방식 | 현재 shape | 목표 shape |
| --- | --- | --- |
| raw socket callback | `source_rid + parts + part_count + userdata` | 유지 |
| raw socket direct recv | bytes 또는 single message | `source_rid + parts + part_count` |
| gateway callback | `source_rid + parts + part_count + userdata` | 유지 |
| gateway direct recv | `source_rid + parts + part_count` | 유지 |
| spot callback | `source_rid + topic + parts + part_count + userdata` | 유지 |
| spot direct recv | `topic + parts + part_count` | `source_rid + topic + parts + part_count` |
| xpub callback | `subscribed + topic + userdata` | `source_rid + subscribed + topic + userdata` |
| xpub direct recv | 일반 recv로 설명 어려움 | `source_rid + subscribed + topic + topic_len` 전용 recv 유지 |

### 4.4 호환성 정책

| 항목 | 방침 |
| --- | --- |
| source compatibility | 유지하지 않음 |
| ABI compatibility | 유지하지 않음 |
| deprecated wrapper 제공 | 하지 않음 |
| 기존 bytes API 이름 보존 | 하지 않음 |
| canonical 이름 승계 | 새 multipart msg 계약이 `zlink_send` / `zlink_recv` 이름을 가져감 |
| 문서화 우선순위 | compatibility보다 단순성과 설명 가능성을 우선 |

## 5. 설계 원칙

- public data-plane은 하나의 깊은 모델로 설명 가능해야 한다.
- "callback이면 rid가 있고 direct recv면 없다" 같은 모델 차이를 허용하지 않는다.
- "bytes recv는 예외" 같은 별도 규칙을 남기지 않는다.
- routing metadata는 payload frame에 숨기지 않고 명시적 파라미터로 분리한다.
- multipart ownership 규칙은 callback과 direct recv에서 동일해야 한다.
- 편의성보다 전체 surface의 설명 가능성과 일관성을 우선한다.
- source/ABI compatibility보다 canonical surface 단순화를 우선한다.
- send는 "peer 지정 필요 여부" 기준으로 두 개의 canonical 함수로만 구분한다.
- spot/xpub처럼 별도 metadata plane이 본질인 경우는 일반 canonical family에
  억지로 맞추지 않는다.
- public C API 통합 가능성 판단은 이름과 시그니처 기준으로 한다.
- socket/handle별 내부 구현 경로 차이는 public C API 통합의 반대 근거가 아니다.
- `XPUB`도 예외 family이지만 가능하면 sender identity(`source_rid`)를 포함해야 한다.
- 현재 구현이 `XPUB source_rid`를 public recv/callback으로 노출하지 못한다면,
  이는 제거가 아니라 기능 복원 대상으로 취급한다.
- public multipart send에서 frame 경계는 `part_count`가 표현하므로 `ZLINK_SNDMORE`는
  필요하지 않다.
- public send/recv flags는 가능하면 `0` 또는 `ZLINK_DONTWAIT`로만 축소한다.
- `topic_id_out_` / `topic_id_len_`는 `spot`과 `xpub`에서 동일한 binary-safe
  output 계약을 사용한다.
- `topic_id_len_`는 caller가 제공한 버퍼 크기를 입력으로 받고, 성공 시 실제 topic
  길이를 출력하는 in/out 파라미터다.
- 버퍼가 topic 전체를 수용할 수 있을 때만 trailing NUL을 추가한다.
- 논리적인 topic 길이는 항상 `topic_id_len_`로 해석한다.
- `topic_id_out_ == NULL`이면 topic 복사를 생략하고 필요한 길이만 `topic_id_len_`에
  기록할 수 있다.
- `topic_id_out_ != NULL`인데 버퍼가 부족하면 `-1`과 `EMSGSIZE`를 반환하고,
  필요한 전체 길이는 `topic_id_len_`에 기록한다.
- `source_rid_out_`는 `NULL`을 허용하며, 이 경우 sender identity 복사를 생략한다.
- `parts_out_`와 `part_count_out_`는 필수 출력 파라미터다.

### 5.1 Unsupported Socket/Handle Error Policy

이 문서의 public C API는 "지원하지 않는 subject가 들어왔을 때"와
"지원하는 subject지만 이 API가 그 family에 맞지 않을 때"를 구분한다.

- `NULL` subject pointer는 `EFAULT`
- 유효하지 않거나 이미 파괴된 pointer는 `EFAULT`
- public canonical family에 속하지 않는 socket/handle에 generic API를 호출하면 `ENOTSUP`
- 전용 family API에 잘못된 socket type을 넘기면 `EINVAL`
- 파라미터 자체가 잘못되면 `EINVAL`

구체 규칙:

- `zlink_send` / `zlink_send_rid` / `zlink_recv`
  - 지원 family:
    raw `PAIR`, `DEALER`, `ROUTER`, `STREAM`, `gateway`
  - 위 family가 아닌 subject에 호출하면 `ENOTSUP`
- `zlink_spot_node_recv`
  - 지원 family:
    `spot_node`
  - 다른 subject에 호출하면 `ENOTSUP`
- `zlink_spot_sub_recv`
  - 지원 family:
    `spot`
  - 다른 subject에 호출하면 `ENOTSUP`
- `zlink_xpub_recv`
  - 지원 family:
    raw `XPUB`
  - 다른 subject에 호출하면 `ENOTSUP`
- `zlink_recv_handler`
  - 지원 family:
    raw `PAIR`, `DEALER`, `ROUTER`, `STREAM`, `gateway`
  - 다른 socket type에 호출하면 `EINVAL`
- `zlink_recv_spot_handler`
  - 지원 family:
    `spot_node`, `spot`, raw `SUB`, raw `XSUB`
  - 다른 socket type 또는 handle family에 호출하면 `EINVAL`
- `zlink_recv_xpub_handler`
  - 지원 family:
    raw `XPUB`
  - 다른 socket type 또는 handle family에 호출하면 `EINVAL`
- generic `zlink_poller_add` / `zlink_poller_modify` / `zlink_poller_remove`
  - 지원 family:
    raw socket, monitor, `gateway`, `spot`, `spot_node`
  - 위 family가 아닌 subject에 호출하면 `ENOTSUP`
- monitor를 generic poller API에 넘기는 것은 허용한다.
  - monitor 전용 public poller helper는 유지하지 않는다.
  - generic 경로에서 monitor 전용 `POLLIN` only 검증은 적용하지 않는다.

정리:

- "이 pointer가 유효한 subject가 아니냐"는 `EFAULT`
- "유효한 subject지만 이 API가 지원하지 않는 family냐"는 `ENOTSUP`
- "지원 family용 API인데 socket type이 틀렸거나 파라미터가 잘못됐냐"는 `EINVAL`

## 6. Core 구현 영향 범위

예상 영향 파일:

- `core/include/zlink.h`
- `core/src/api/zlink.cpp`
- `core/tests/unittest/...`
- `core/tests/integration/...`
- 필요 시 `core/tests/e2e/...`

예상 구현 축:

- public header에서 bytes API 삭제 및 canonical 이름 재할당
- 일반 canonical send/recv family를 `zlink_send`, `zlink_send_rid`, `zlink_recv`로 정리
- callback recv와 direct recv의 공통 envelope 해석 로직 정리
- socket family별 `source_rid` 계약 정리
- stream/gateway 전용 data-plane send surface를 canonical send family로 흡수
- gateway 전용 recv surface를 canonical recv family로 흡수
- spot/xpub 예외 family 유지 범위 정리
- `XPUB` subscribe/unsubscribe event의 sender `source_rid` 복원
- public `zlink_send_flags_t`에서 `ZLINK_SNDMORE` 제거
- recv family의 parts array 해제 계약 문서화
- `topic_id_out_` / `topic_id_len_` 버퍼 부족 동작과 `EMSGSIZE` 계약 추가
- unsupported socket/handle 입력에 대한 errno 정책 정리 및 테스트 추가
- callback/direct parity 테스트 추가
- public C API는 통합하되 내부 구현은 socket/handle family별로 분기 가능

## 7. 핵심 쟁점

### 7.1 recv canonical 이름

- canonical recv는 `zlink_recv` 이름을 사용한다.
- 기존 bytes `zlink_recv`는 제거한다.

원칙:

- 이름보다 contract가 우선이다.
- canonical 이름은 가장 자주 설명될 기본 계약이 가져간다.
- multipart는 기본값이므로 함수명에 `_parts`를 붙이지 않는다.

### 7.2 send canonical shape

- canonical send는 `zlink_send`와 `zlink_send_rid` 두 개로 정리한다.
- 기존 bytes `zlink_send`는 제거한다.
- `target_rid`는 일반 send의 공통 파라미터로 두지 않는다.

원칙:

- recv가 multipart canonical이면 send도 multipart canonical이 더 자연스럽다.
- peer 지정 여부가 send family 분기의 유일한 기준이어야 한다.
- multipart는 기본값이므로 함수명에 `_parts`를 붙이지 않는다.

### 7.3 source_rid contract

- `source_rid`는 실제 sender peer identity여야 한다.
- socket family마다 의미가 달라지더라도 최소한 "누가 보냈는지"는 한 문장으로
  설명 가능해야 한다.
- public 문서에서 `source_rid`의 유효 범위와 안정성을 명시해야 한다.

## 8. 구현 순서

1. `core/include/zlink.h`에서 기존 bytes `zlink_send` / `zlink_recv` 삭제
2. 같은 이름으로 canonical multipart msg `zlink_send` / `zlink_recv` 선언
3. `zlink_send_rid` 추가 및 stream/gateway 전용 send API 흡수 계획 반영
4. `core/src/api/zlink.cpp`에서 direct recv 공통 envelope 해석 경로 정리
5. callback/direct recv parity 테스트 추가
6. `core` 내부 테스트를 새 canonical 계약으로 일괄 갱신

## 9. 테스트 원칙

- callback recv와 direct recv는 같은 입력에 대해 같은 `source_rid`와 같은 payload
  shape를 보여야 한다.
- multipart message에서 frame 수와 frame 순서가 동일해야 한다.
- routing envelope은 callback/direct recv 모두 public payload에 남지 않아야 한다.
- fail-fast 정책을 유지한다.
- retry, sleep 기반 동기화, bug-hiding 테스트 수정은 금지한다.

## 10. 회귀테스트 전략

이 작업의 회귀테스트는 "기존 테스트를 새 API로 포팅"만으로 충분하지 않다.
이번 변경은 rename이 아니라 public contract 변경이므로 기존 시나리오 이관과
새 계약 검증을 분리해서 다뤄야 한다.

### 10.1 테스트 구성 원칙

- 1단계: 기존 테스트를 새 API surface로 이관한다.
- 2단계: callback/direct parity를 검증하는 새 테스트를 추가한다.
- 3단계: 새 public contract 자체를 검증하는 전용 회귀테스트를 추가한다.
- 기존 테스트 이관은 baseline 보존용이고, 새 테스트는 redesign contract 보증용이다.

### 10.2 기존 테스트 이관 범위

- bytes `zlink_send` / `zlink_recv` 호출부를 새 canonical API로 치환한다.
- `zlink_stream_send`, `zlink_stream_send_msg` 호출부를 `zlink_send_rid`로 치환한다.
- `zlink_gateway_send`, `zlink_gateway_send_rid`, `zlink_gateway_recv` 호출부를
  `zlink_send`, `zlink_send_rid`, `zlink_recv`로 치환한다.
- `spot` / `xpub` 호출부는 새 예외 family 시그니처에 맞게 조정한다.

### 10.3 반드시 추가할 새 회귀 축

- `zlink_recv`가 callback과 동일한 `source_rid + multipart payload` shape를 주는지
- routing envelope이 public payload에서 제거되는지
- callback recv와 direct recv의 `source_rid`가 동일한지
- callback recv와 direct recv의 multipart frame 수와 순서가 동일한지
- `zlink_send` multipart 전송이 `part_count` 기준으로 정확히 경계를 유지하는지
- public `ZLINK_SNDMORE` 제거 후에도 multipart send 동작이 깨지지 않는지
- `stream` 전용 send 제거 후 `zlink_send_rid`로 기존 동작이 유지되는지
- `gateway` 전용 send/recv 제거 후 canonical family로 기존 동작이 유지되는지
- `XPUB`에서 `source_rid + subscribed + topic`이 direct recv/callback에서 일관되는지
- `spot` / `xpub`의 `topic_id_out_` / `topic_id_len_` 버퍼 계약이 맞는지
- `EMSGSIZE`, `EINVAL`, `ENOTSUP` 등 새 계약의 에러 동작이 맞는지
- unsupported socket/handle 입력 시 `EFAULT`, `ENOTSUP`, `EINVAL` 구분이 정책대로
  동작하는지
- generic poller가 raw socket, monitor, `gateway`, `spot`, `spot_node`를 모두 수용하는지
- generic poller의 monitor 경로에서는 monitor 전용 `POLLIN` 검증이 적용되지 않는지
- spot/spot_node가 pub/sub 양쪽으로 모두 등록된 상태에서 generic
  `zlink_poller_modify`가 `events_`로 정확히 한 registration만 겨냥하는지
- generic `zlink_poller_modify`에서 target registration이 없거나 pub/sub가 모호하면
  `EINVAL`를 반환하는지

### 10.4 중점 parity 테스트

이번 작업의 핵심 회귀테스트는 parity test다.

- 동일한 입력 메시지에 대해 callback 경로와 direct recv 경로를 각각 실행한다.
- 두 경로에서 받은 `source_rid`, topic metadata, multipart payload를 비교한다.
- socket/handle family별로 결과 shape가 완전히 같아야 한다.

최소 parity 대상:

- raw `PAIR`
- raw `DEALER`
- raw `ROUTER`
- raw `STREAM`
- `gateway`
- `spot`
- `XPUB`

### 10.5 제거 API 검증

- 제거된 API가 header에서 사라졌는지 확인한다.
- 제거된 public flag `ZLINK_SNDMORE`가 더 이상 노출되지 않는지 확인한다.
- 필요하면 compile-fail 또는 symbol absence 방식으로 검증한다.

### 10.6 Unsupported Subject Error Regression

- `NULL` subject pointer 입력 시 `EFAULT`를 반환하는지 확인한다.
- 파괴되었거나 유효하지 않은 handle 입력 시 `EFAULT`를 반환하는지 확인한다.
- generic `zlink_send` / `zlink_send_rid` / `zlink_recv`에 지원하지 않는 family를
  넘기면 `ENOTSUP`를 반환하는지 확인한다.
- `zlink_spot_node_recv` / `zlink_spot_sub_recv` / `zlink_xpub_recv`에 지원하지 않는
  family를 넘기면 `ENOTSUP`를 반환하는지 확인한다.
- `zlink_recv_handler` / `zlink_recv_spot_handler` / `zlink_recv_xpub_handler`에
  잘못된 socket type을 넘기면 `EINVAL`를 반환하는지 확인한다.
- generic `zlink_poller_add` / `modify` / `remove`에 raw socket, monitor,
  `gateway`, `spot`, `spot_node`를 넘겼을 때 각각 기대 경로로 동작하는지 확인한다.
- generic poller 경로에서 monitor subject는 허용되지만 monitor 전용 event 검증이
  적용되지 않는지 확인한다.
- spot/spot_node가 pub/sub 양쪽 등록된 상태에서 generic `modify`가 `events_`로
  정확히 한 방향만 수정하는지 확인한다.
- generic `modify`에서 해당 subject의 target registration이 없으면 `EINVAL`를
  반환하는지 확인한다.
- malformed parameter와 unsupported family가 섞인 경우에도 errno 우선순위가
  문서 정책과 일치하는지 확인한다.

## 11. 요약

- `core` public data-plane은 `msg_t` 중심으로 단일화한다.
- recv 기본형은 callback과 동일한 `source_rid + multipart payload`다.
- 기존 bytes `zlink_send()` / `zlink_recv()`는 compatibility 없이 제거한다.
- 새 canonical multipart msg 계약이 `zlink_send()` / `zlink_recv()` 이름을 승계한다.
- send는 `zlink_send`와 `zlink_send_rid` 두 개의 canonical family로 정리한다.
- `spot`과 `xpub`는 topic/event metadata 때문에 일반 canonical family와 분리한다.
- public `ZLINK_SNDMORE`는 제거하고 multipart 경계는 `part_count`만 사용한다.

## 12. 최종 제공 API 표

### 12.1 일반 canonical data-plane

| API | 반환값 | 파라미터 | 사용 소켓/handle | 의미 |
| --- | --- | --- | --- | --- |
| `int zlink_send(void *s, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags)` | 성공 시 `0`, 실패 시 `-1` | `s`, `parts`, `part_count`, `flags` | raw `PAIR`, `DEALER`, `ROUTER`, `gateway`의 non-directed path | peer 지정 없는 multipart send |
| `int zlink_send_rid(void *s, const zlink_routing_id_t *target_rid, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags)` | 성공 시 `0`, 실패 시 `-1` | `s`, `target_rid`, `parts`, `part_count`, `flags` | `ROUTER`, `STREAM`, `gateway` 등 target peer 지정이 필요한 socket/handle | target peer 지정 multipart send |
| `int zlink_recv(void *s, zlink_routing_id_t *source_rid_out, zlink_msg_t **parts_out, size_t *part_count_out, zlink_send_flags_t flags)` | 성공 시 `0`, 실패 시 `-1` | `s`, `source_rid_out`, `parts_out`, `part_count_out`, `flags` | `PAIR`, `DEALER`, `ROUTER`, `STREAM`, `gateway` 등 일반 multipart recv-capable socket/handle | `source_rid + multipart payload` direct recv |

### 12.2 예외 data-plane

| API family | 반환값 | 파라미터 핵심 | 사용 소켓/handle | 분리 이유 |
| --- | --- | --- | --- | --- |
| `spot_*_send` / `spot_*_recv` | API별 상이 | `source_rid`, `topic`, `parts`, `part_count` 중심 | `spot_node`, `spot` | topic metadata가 payload와 동급의 canonical 정보 |
| `zlink_xpub_recv` / `zlink_recv_xpub_handler` | API별 상이 | `source_rid`, `subscribed`, `topic_id`, `topic_id_len` 또는 `handler(source_rid, subscribed, topic, topic_len, userdata)` | `XPUB` | subscribe/unsubscribe event plane이며 일반 multipart recv와는 별도 계약이 필요하다 |

### 12.3 제거될 data-plane API

| API | 현재 형태 | 제거 사유 |
| --- | --- | --- |
| `zlink_send` bytes 버전 | `void *buf + size` | msg-only canonical surface와 충돌 |
| `zlink_recv` bytes 버전 | `void *buf + size` | callback/direct parity와 충돌 |
| `zlink_msg_send` | single-part helper | canonical surface를 얕게 만듦 |
| `zlink_msg_recv` | single-part helper | recv 기본이 multipart라는 원칙과 충돌 |
| `ZLINK_SNDMORE` | public send flag | multipart 경계를 `part_count`가 표현하므로 불필요 |
| `zlink_stream_send` | stream 전용 bytes/rid send | public `zlink_send_rid`로 통합 가능 |
| `zlink_stream_send_msg` | stream 전용 single-msg/rid send | public `zlink_send_rid`로 통합 가능 |
| `zlink_gateway_send` | gateway 전용 multipart send | public `zlink_send`로 통합 가능 |
| `zlink_gateway_send_rid` | gateway 전용 directed multipart send | public `zlink_send_rid`로 통합 가능 |
| `zlink_gateway_recv` | gateway 전용 multipart recv | public `zlink_recv`로 통합 가능 |

### 12.4 Public C API 통합 원칙

| 항목 | 방침 |
| --- | --- |
| 통합 판단 기준 | public C API 이름과 시그니처 기준 |
| 내부 구현 경로 통합 | 필수 아님 |
| socket/handle별 내부 분기 | 유지 가능 |
| gateway/stream 고유 semantics | 내부 dispatch에서 유지 |
| 문서 표현 | `public zlink_send` / `public zlink_send_rid` / `public zlink_recv`로 통합 가능 |

### 12.5 Callback Registration API

| 등록 API | callback typedef | 사용 소켓/handle | 변경 여부 | 비고 |
| --- | --- | --- | --- | --- |
| `int zlink_recv_handler(void *s, zlink_socket_msg_handler_fn handler, void *userdata)` | `void (*zlink_socket_msg_handler_fn)(const zlink_routing_id_t *source_rid, zlink_msg_t *parts, size_t part_count, void *userdata)` | raw `PAIR`, `DEALER`, `ROUTER`, `STREAM`, `gateway` | 등록 함수 유지 | 일반 multipart data-plane callback 등록 |
| `int zlink_recv_spot_handler(void *s, zlink_spot_handler_fn handler, void *userdata)` | `void (*zlink_spot_handler_fn)(const zlink_routing_id_t *source_rid, const char *topic, size_t topic_len, zlink_msg_t *parts, size_t part_count, void *userdata)` | `spot_node`, `spot`, raw `SUB`, raw `XSUB` | 등록 함수 유지 | topic-aware callback 등록 |
| `int zlink_recv_xpub_handler(void *s, zlink_xpub_handler_fn handler, void *userdata)` | 목표: `void (*zlink_xpub_handler_fn)(const zlink_routing_id_t *source_rid, int subscribed, const char *topic, size_t topic_len, void *userdata)` | raw `XPUB` | 등록 함수 유지, callback typedef 변경 | `subscribed == 1`이면 subscribe, `0`이면 unsubscribe |

추가 규칙:

- callback 등록 함수 이름은 이번 문서 범위에서 유지한다.
- 일반 data-plane canonicalization은 sync send/recv API 정리에 집중하며,
  callback registration API rename은 목표가 아니다.
- `XPUB`만 callback payload 계약이 변경된다.
- `zlink_recv_xpub_handler()`의 등록 방식은 유지하되,
  `zlink_xpub_handler_fn`은 `source_rid`를 포함하도록 확장한다.

## 13. Header Draft

이 섹션은 [`core/include/zlink.h`](/home/hep7/project/kairos/zlink/core/include/zlink.h)
에 반영할 public header 초안이다.

### 13.1 일반 canonical data-plane 초안

```c
/**
 * @brief Send a multipart message on a socket or handle.
 *
 * Ownership of all parts is transferred to the callee on success.
 * On failure, ownership remains with the caller unless a narrower contract is
 * documented for a specific handle family.
 *
 * This is the canonical public send API for non-directed sends.
 * Public multipart send does not use ZLINK_SNDMORE; part boundaries are
 * defined only by @p parts_ and @p part_count_.
 *
 * @param s_          Socket or handle.
 * @param parts_      Multipart message array.
 * @param part_count_ Number of entries in @p parts_.
 * @param flags_      Send flags (`0` or `ZLINK_DONTWAIT`).
 * @return 0 on success, -1 on failure (errno is set).
 */
ZLINK_EXPORT int zlink_send (void *s_,
                             zlink_msg_t *parts_,
                             size_t part_count_,
                             zlink_send_flags_t flags_);

/**
 * @brief Send a multipart message to a specific peer routing id.
 *
 * Ownership of all parts is transferred to the callee on success.
 * On failure, ownership remains with the caller unless a narrower contract is
 * documented for a specific handle family.
 *
 * This is the canonical public send API for directed sends.
 * Public multipart send does not use ZLINK_SNDMORE; part boundaries are
 * defined only by @p parts_ and @p part_count_.
 *
 * @param s_          Socket or handle.
 * @param target_rid_ Target peer routing id.
 * @param parts_      Multipart message array.
 * @param part_count_ Number of entries in @p parts_.
 * @param flags_      Send flags (`0` or `ZLINK_DONTWAIT`).
 * @return 0 on success, -1 on failure (errno is set).
 */
ZLINK_EXPORT int zlink_send_rid (void *s_,
                                 const zlink_routing_id_t *target_rid_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_,
                                 zlink_send_flags_t flags_);

/**
 * @brief Receive a multipart message from a socket or handle.
 *
 * Direct recv is the synchronous counterpart of direct callback dispatch.
 * The returned payload shape must match the callback payload shape for the
 * same socket or handle family.
 *
 * `source_rid_out_` identifies the remote source that sent the message.
 * Routing envelope metadata is removed from the public payload and reported
 * through `source_rid_out_`.
 *
 * On success, ownership of the returned parts array and all contained
 * `zlink_msg_t` instances is transferred to the caller. The caller must close
 * each part with `zlink_msg_close()` and then free the array itself with
 * `free()`.
 *
 * @param s_              Socket or handle.
 * @param source_rid_out_ Sender peer routing id output.
 * @param parts_out_      Output pointer to multipart message array.
 * @param part_count_out_ Output number of entries in @p parts_out_.
 * @param flags_          Recv flags.
 * `source_rid_out_` may be NULL to skip sender identity copy.
 * `parts_out_` and `part_count_out_` are required.
 * @return 0 on success, -1 on failure (errno is set).
 */
ZLINK_EXPORT int zlink_recv (void *s_,
                             zlink_routing_id_t *source_rid_out_,
                             zlink_msg_t **parts_out_,
                             size_t *part_count_out_,
                             zlink_send_flags_t flags_);
```

### 13.2 예외 family 초안 방향

```c
/**
 * @brief Receive a topic-aware multipart message from a Spot handle.
 *
 * This family remains separate from the general canonical recv API because
 * topic metadata is part of the public contract.
 * `topic_id_len_` is an in/out parameter: the caller supplies buffer capacity,
 * and on success the callee writes the actual topic length. If the buffer is
 * too small, the call fails with `EMSGSIZE` and stores the required length.
 * `topic_id_out_ == NULL` is allowed for size query.
 * `source_rid_out_` may be NULL to skip sender identity copy.
 */
ZLINK_EXPORT int zlink_spot_node_recv (void *node_,
                                       zlink_routing_id_t *source_rid_out_,
                                       zlink_msg_t **parts_out_,
                                       size_t *part_count_out_,
                                       char *topic_id_out_,
                                       size_t *topic_id_len_,
                                       zlink_send_flags_t flags_);

ZLINK_EXPORT int zlink_spot_sub_recv (void *sub_,
                                      zlink_routing_id_t *source_rid_out_,
                                      zlink_msg_t **parts_out_,
                                      size_t *part_count_out_,
                                      char *topic_id_out_,
                                      size_t *topic_id_len_,
                                      zlink_send_flags_t flags_);

/**
 * @brief Receive an XPUB subscription event.
 *
 * XPUB remains separate from the general canonical recv API because
 * subscription events are not generic multipart payload delivery.
 * `topic_id_len_` is an in/out parameter: the caller supplies buffer capacity,
 * and on success the callee writes the actual topic length. If the buffer is
 * too small, the call fails with `EMSGSIZE` and stores the required length.
 * `subscribed_out_` is an output parameter: `1` means subscribe and `0` means
 * unsubscribe.
 * `topic_id_out_ == NULL` is allowed for size query.
 * `source_rid_out_` may be NULL to skip sender identity copy.
 */
ZLINK_EXPORT int zlink_xpub_recv (void *s_,
                                  zlink_routing_id_t *source_rid_out_,
                                  int *subscribed_out_,
                                  char *topic_id_out_,
                                  size_t *topic_id_len_,
                                  zlink_send_flags_t flags_);

/**
 * @brief Install an XPUB subscription-event handler.
 *
 * This callback form remains available for event-driven use cases.
 */
typedef void (*zlink_xpub_handler_fn) (
                                       const zlink_routing_id_t *source_rid_,
                                       int subscribed_,
                                       const char *topic_,
                                       size_t topic_len_,
                                       void *userdata_);

ZLINK_EXPORT int zlink_recv_xpub_handler (
  void *s_, zlink_xpub_handler_fn handler_, void *userdata_);
```

원칙:

- `spot`은 `source_rid + topic + multipart payload` 계약을 유지한다.
- `xpub`은 `source_rid + subscribed + topic` event 계약을 유지한다.
- `xpub`은 poller/direct recv를 위해 전용 `zlink_xpub_recv()`를 유지한다.
- `xpub` callback은 event-driven path로 병행 유지한다.
- `subscribed_out_ == 1`은 subscribe, `0`은 unsubscribe를 뜻한다.
- `XPUB source_rid`는 public surface와 회귀테스트에 복원되어 있다.
- `topic_id_out_` / `topic_id_len_` 계약은 `spot`과 `xpub`에서 동일하게 맞춘다.
- `topic_id_len_`는 입력 버퍼 크기이자 성공 시 실제 topic 길이를 돌려주는 in/out 값이다.
- 버퍼가 충분할 때만 trailing NUL을 쓰고, 논리 길이는 항상 `topic_id_len_` 기준이다.
- `topic_id_out_ == NULL`이면 필요한 길이만 조회하는 호출을 허용한다.
- 버퍼 부족은 `EMSGSIZE`로 보고하고 필요한 길이는 `topic_id_len_`에 남긴다.
- recv로 반환된 parts array는 각 part를 `zlink_msg_close()` 한 뒤 `free()`로 해제한다.
- 이 두 family는 일반 `zlink_send` / `zlink_send_rid` / `zlink_recv`에
  억지로 맞추지 않는다.

### 13.3 제거 완료 선언 초안

아래 선언은 새 header에서 제거된 legacy 선언이다.

```c
ZLINK_EXPORT int zlink_send (void *s_,
                             const void *buf_,
                             size_t len_,
                             zlink_send_flags_t flags_);

ZLINK_EXPORT int zlink_recv (void *s_,
                             void *buf_,
                             size_t len_,
                             zlink_send_flags_t flags_);

ZLINK_EXPORT int zlink_msg_send (zlink_msg_t *msg_,
                                 void *s_,
                                 zlink_send_flags_t flags_);

ZLINK_EXPORT int zlink_msg_recv (zlink_msg_t *msg_,
                                 void *s_,
                                 zlink_send_flags_t flags_);

ZLINK_EXPORT int zlink_stream_send (void *s_,
                                    const zlink_routing_id_t *rid_,
                                    const void *data_,
                                    size_t size_,
                                    zlink_send_flags_t flags_);

ZLINK_EXPORT int zlink_stream_send_msg (void *s_,
                                        const zlink_routing_id_t *rid_,
                                        zlink_msg_t *msg_,
                                        zlink_send_flags_t flags_);

ZLINK_EXPORT int zlink_gateway_send (void *gateway,
                                     zlink_msg_t *parts,
                                     size_t part_count,
                                     zlink_send_flags_t flags);

ZLINK_EXPORT int zlink_gateway_send_rid (void *gateway,
                                         const zlink_routing_id_t *routing_id,
                                         zlink_msg_t *parts,
                                         size_t part_count,
                                         zlink_send_flags_t flags);

ZLINK_EXPORT int zlink_gateway_recv (void *gateway,
                                     zlink_routing_id_t *source_rid_out,
                                     zlink_msg_t **parts,
                                     size_t *part_count,
                                     int flags);
```

### 13.4 소켓/handle별 사용 규칙 초안

| socket/handle | `zlink_send` | `zlink_send_rid` | `zlink_recv` | 비고 |
| --- | --- | --- | --- | --- |
| `PAIR` | 사용 | 미사용 | 사용 | 단일 peer라도 `source_rid`는 항상 반환 |
| `DEALER` | 사용 | 미사용 | 사용 | recv에서 sender identity 일관 제공 |
| `ROUTER` | 사용 가능 | 사용 가능 | 사용 | direct reply/send-to-peer는 `zlink_send_rid` |
| `STREAM` | 미사용 | 사용 | 사용 | peer 지정 send는 항상 `zlink_send_rid` |
| `gateway` | 사용 가능 | 사용 가능 | 사용 | 기존 gateway 전용 data-plane API 흡수 |
| `spot_node` / `spot` | 별도 family | 별도 family | 별도 family | `source_rid + topic + multipart payload` 계약 |
| `XPUB` | 해당 없음 | 해당 없음 | 전용 recv family | `source_rid + subscribed + topic`를 다루는 `zlink_xpub_recv()` + `zlink_recv_xpub_handler()` event plane |

## 14. Poller Public C API 통합

### 14.0 결정 사항

- generic `zlink_poller_add` / `zlink_poller_modify` / `zlink_poller_remove`는
  raw socket뿐 아니라 monitor, `gateway`, `spot`, `spot_node` subject도 수용한다.
- 타입별 전용 poller 함수 12개(`gateway` 3 + `spot_pub` 3 + `spot_sub` 3 +
  `monitor` 3)는 모두 제거되었다.
- public poller API는 generic 3개(`zlink_poller_add` / `modify` / `remove`)만 남긴다.
- generic `zlink_poller_remove(poller, subject)`는 해당 subject에 연결된 모든
  registration을 제거하는 새 public 계약을 사용한다.

### 14.1 현재 상태

현재 poller API는 타입별 전용 함수 12개와 generic 함수 3개가 공존한다.

generic 함수 (3개):

| 함수 | 시그니처 | 현재 수용 범위 |
| --- | --- | --- |
| `zlink_poller_add` | `(poller, socket, user_data, events)` | raw socket만 |
| `zlink_poller_modify` | `(poller, socket, events)` | raw socket만 |
| `zlink_poller_remove` | `(poller, socket)` | raw socket만 |

타입별 전용 함수 (12개):

| subject | add | modify | remove |
| --- | --- | --- | --- |
| gateway | `zlink_poller_add_gateway` | `zlink_poller_modify_gateway` | `zlink_poller_remove_gateway` |
| spot_pub | `zlink_poller_add_spot_pub` | `zlink_poller_modify_spot_pub` | `zlink_poller_remove_spot_pub` |
| spot_sub | `zlink_poller_add_spot_sub` | `zlink_poller_modify_spot_sub` | `zlink_poller_remove_spot_sub` |
| monitor | `zlink_poller_add_monitor` | `zlink_poller_modify_monitor` | `zlink_poller_remove_monitor` |

내부 구현은 이미 `poller_add_registration`, `poller_find_registration_index`,
`release_poller_registration` 등 공통 헬퍼로 통합되어 있다.
`poller_subject_kind_t` enum으로 subject 종류를 구분하고 있다.

### 14.2 통합 방향

기존 generic `zlink_poller_add` / `zlink_poller_modify` / `zlink_poller_remove`가
gateway/spot/spot_node 핸들을 직접 받을 수 있도록 내부 디스패치를 확장한다.
타입별 전용 함수 12개(gateway 3 + spot_pub 3 + spot_sub 3 + monitor 3)는
public header에서 제거한다.

통합 후 public poller API:

```c
ZLINK_EXPORT int zlink_poller_add (void *poller_,
                                   void *subject_,
                                   void *user_data_,
                                   short events_);
ZLINK_EXPORT int zlink_poller_modify (void *poller_,
                                      void *subject_,
                                      short events_);
ZLINK_EXPORT int zlink_poller_remove (void *poller_, void *subject_);
```

- `subject_`는 raw socket, monitor, gateway 핸들, spot 핸들, spot_node 핸들 모두 수용한다.
- 시그니처 변경 없이 내부 디스패치만 확장한다.

### 14.3 런타임 타입 식별

내부적으로 `subject_` 타입을 다음 순서로 판별한다:

1. `is_registered_gateway_handle(subject_)` → gateway 경로
2. `is_registered_spot_handle(subject_)` → spot 경로 (events_로 pub/sub 구분)
3. `is_registered_spot_node_handle(subject_)` → spot_node 경로 (events_로 pub/sub 구분)
4. `as_socket_handle(subject_)` → raw socket / monitor 경로 (현행 유지)

타입 판별 함수는 이미 구현되어 있고 재사용 가능하다. generic
`zlink_poller_add/modify/remove`는 gateway/spot/spot_node/monitor 경로를
모두 수용하도록 확장되었다.

### 14.4 Spot pub/sub 구분 정책

spot/spot_node 핸들에서 pub/sub 방향은 `events_`로 구분한다.

| events_ | 경로 | 비고 |
| --- | --- | --- |
| `ZLINK_POLLOUT` | pub | `validate_spot_pub_poller_events` 적용 |
| `ZLINK_POLLIN` | sub | `validate_spot_sub_poller_events` 적용 |
| `ZLINK_POLLIN \| ZLINK_POLLOUT` | `EINVAL` | 동시 지정 불가 |

현재 spot API 검증이 pub=POLLOUT only, sub=POLLIN only이므로 모호함이 없다.

### 14.5 Monitor 처리

monitor는 `as_socket_handle()`으로 처리되며 raw socket과 동일한 타입 시그니처를
갖는다. 별도 레지스트리가 없어 런타임에 raw socket과 구분할 수 없다.

- monitor를 generic `zlink_poller_add`에 넘기면 raw socket 경로로 처리된다.
  이 경우 `validate_monitor_poller_events` (POLLIN only) 검증은 **적용되지 않는다**.
- generic 경로에서 monitor 전용 이벤트 검증을 하지 않는 것은 의도된 동작이다.
- 호환성은 목표가 아니므로 monitor 전용 public helper 3개는 남기지 않는다.
- 따라서 monitor poller 등록도 generic `zlink_poller_add` / `modify` / `remove`
  만 사용한다.

### 14.6 제거 완료 선언

```c
// 아래 12개 함수를 public header에서 제거한다.
ZLINK_EXPORT int zlink_poller_add_gateway (void *poller_, void *gateway_,
                                           void *user_data_, short events_);
ZLINK_EXPORT int zlink_poller_modify_gateway (void *poller_, void *gateway_,
                                              short events_);
ZLINK_EXPORT int zlink_poller_remove_gateway (void *poller_, void *gateway_);
ZLINK_EXPORT int zlink_poller_add_spot_pub (void *poller_, void *spot_or_node_,
                                            void *user_data_, short events_);
ZLINK_EXPORT int zlink_poller_modify_spot_pub (void *poller_, void *spot_or_node_,
                                               short events_);
ZLINK_EXPORT int zlink_poller_remove_spot_pub (void *poller_, void *spot_or_node_);
ZLINK_EXPORT int zlink_poller_add_spot_sub (void *poller_, void *spot_or_node_,
                                            void *user_data_, short events_);
ZLINK_EXPORT int zlink_poller_modify_spot_sub (void *poller_, void *spot_or_node_,
                                               short events_);
ZLINK_EXPORT int zlink_poller_remove_spot_sub (void *poller_, void *spot_or_node_);
ZLINK_EXPORT int zlink_poller_add_monitor (void *poller_, void *monitor_,
                                           void *user_data_, short events_);
ZLINK_EXPORT int zlink_poller_modify_monitor (void *poller_, void *monitor_,
                                              short events_);
ZLINK_EXPORT int zlink_poller_remove_monitor (void *poller_, void *monitor_);
```

### 14.7 내부 구현 변경

`zlink_poller_add` 내부에 디스패치 로직을 추가한다. 기존 타입별 함수의
핵심 로직(핸들 검증, 소켓 추출, ref-count, 이벤트 검증, registration 추적)은
static 내부 헬퍼로 보존한다.

```
zlink_poller_add(poller_, subject_, user_data_, events_)
  ├─ is_registered_gateway_handle(subject_)
  │   └─ validate_gateway_poller_events
  │   └─ increment_gateway_poller_ref
  │   └─ gateway_access_t::router_socket → poller_add_registration
  ├─ is_registered_spot_handle(subject_) || is_registered_spot_node_handle(subject_)
  │   └─ events_ & POLLOUT → pub 경로
  │   └─ events_ & POLLIN  → sub 경로
  │   └─ 양쪽 동시 → EINVAL
  │   └─ resolve → poller_socket → poller_add_registration
  └─ as_socket_handle(subject_)
      └─ 기존 raw socket 경로 (현행 유지)
```

`zlink_poller_modify` / `zlink_poller_remove`도 동일 패턴으로 디스패치를 확장한다.

`modify` 계약:

- `gateway`와 raw socket/monitor는 기존 의미를 유지한다.
- `spot` / `spot_node`는 `events_`로 pub/sub target registration을 식별한다.
- `events_ == ZLINK_POLLOUT`이면 pub registration만 수정한다.
- `events_ == ZLINK_POLLIN`이면 sub registration만 수정한다.
- `events_ == ZLINK_POLLIN | ZLINK_POLLOUT` 또는 그 외 모호한 값은 `EINVAL`다.
- 지정한 방향의 registration이 존재하지 않으면 `EINVAL`다.

`remove` 계약:

- `remove`는 `events_` 파라미터가 없다.
- 해당 subject에 대해 등록된 **모든** registration을 제거한다.
- 같은 spot 핸들이 pub/sub 각각 별도로 등록된 경우, `zlink_poller_remove(poller, spot)`
  호출 시 pub registration과 sub registration을 **모두** 제거한다.
- 내부적으로 `poller_find_registration_index`를 subject 기준으로 반복 조회하여
  해당 subject의 registration이 남지 않을 때까지 해제한다.
- 이는 기존 generic `remove`의 단일 socket 제거 의미보다 넓은 새 public 계약이다.

### 14.8 설계 원칙

- public C API 통합 판단은 이름과 시그니처 기준으로 한다.
- 내부 구현 경로 차이는 public C API 통합의 반대 근거가 아니다.
- 시그니처 변경 없이 기존 generic 함수의 수용 범위만 확장한다.
- 타입별 전용 함수 제거는 compatibility wrapper 없이 진행한다.
- monitor는 raw socket과 런타임 구분 불가하므로 generic 경로에 흡수한다.
- generic poller는 monitor subject도 수용하지만, monitor 전용 이벤트 검증까지 제공하지는 않는다.

## 15. 진행 상태

이 섹션은 문서 결정과 현재 `core` 작업 상태를 분리해서 기록한다.
아래 상태는 "문서상 결정되었는가"가 아니라 "현재 코드 반영이 끝났는가" 기준이다.

### 15.1 현재까지 완료된 항목

- `core/include/zlink.h`에서 기존 bytes `zlink_send` / `zlink_recv` 선언을 제거하고
  canonical multipart `zlink_send` / `zlink_recv` 선언으로 교체했다.
- `core/include/zlink.h`에 `zlink_send_rid`를 추가했다.
- `core/include/zlink.h`에서 `zlink_msg_send` / `zlink_msg_recv` 선언을 제거했다.
- `core/include/zlink.h`에서 `zlink_stream_send` / `zlink_stream_send_msg` 선언을 제거했다.
- `core/include/zlink.h`에서 `zlink_gateway_send` / `zlink_gateway_send_rid` /
  `zlink_gateway_recv` 선언을 제거했다.
- public header에서 `ZLINK_SNDMORE`를 제거했다.
- `spot_node_recv` / `spot_sub_recv` 시그니처에 `source_rid_out_`를 반영했다.
- `zlink_xpub_recv` public 시그니처를 문서 계약 방향으로 정렬했다.
- `zlink_xpub_handler_fn` typedef에 `source_rid_`를 포함하는 형태가 반영되어 있다.
- generic `zlink_poller_add` / `modify` / `remove`가 `gateway`, `spot`,
  `spot_node`, monitor subject를 받는 방향의 내부 확장이 이미 시작되어 있다.
- `zlink_poller_remove`를 subject의 모든 registration 제거 계약으로 확장하는
  내부 헬퍼가 이미 반영되어 있다.
- `spot` / `spot_node`의 generic poller `modify` / `remove` 계약을 고정하는
  회귀테스트가 추가되어 있다.
- generic monitor poller 경로에서 non-`POLLIN` event registration이 허용되는
  계약을 고정하는 회귀테스트가 추가되어 있다.
- `XPUB` direct recv의 `EMSGSIZE` + required topic length 계약을 고정하는
  회귀테스트가 추가되어 있다.

### 15.2 이번 리뷰에서 닫은 항목

- `spot_sub_recv` / `spot_node_recv`의 `EMSGSIZE` + required topic length 계약을
  회귀테스트로 추가했다.
- `spot_sub_recv`의 `EMSGSIZE` 조기 반환 경로에서도 `source_rid_out_`를 유지하도록
  구현을 보강했다. 이 변경은 `spot_node_recv` 경로에도 동일하게 반영된다.
- `XPUB` direct recv의 `source_rid + subscribed + topic` 계약은
  [`test_socket_with_handler.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_socket_with_handler.cpp)
  로 검증되고 있음을 재확인했다.
- unsupported subject errno 정책은
  [`unittest_service_mode_policy.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_service_mode_policy.cpp)
  에서 `EFAULT` / `ENOTSUP` / `EINVAL` 구분으로 계속 고정된다.
- public header와 source에서 legacy public API 선언/심볼이 제거된 상태를 재확인했다.

### 15.3 구현자가 혼동하면 안 되는 항목

- poller 전용 함수 12개는 **모두 삭제 완료** 상태다.
  `gateway` 3개, `spot_pub` 3개, `spot_sub` 3개, `monitor` 3개를 모두 포함한다.
- public poller API는 `zlink_poller_add` / `zlink_poller_modify` /
  `zlink_poller_remove` 3개만 남긴다.
- monitor는 generic poller 경로에 흡수한다.
  monitor 전용 public poller helper를 남기지 않는다.
- `spot_node`와 `spot`은 일반 canonical recv family에 흡수하지 않는다.
  `topic` metadata가 public contract에 포함되므로 별도 family를 유지한다.
