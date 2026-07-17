[English](04-errno-map.md) | 한국어

[스펙 목차](../README.ko.md) · [코어 목차](README.ko.md) · [오류와 result enum](03-errors.ko.md)

# Result와 errno 대응

이 문서는 ZLink Core 10.0.0 공개 API의 result enum과 thread-local errno 대응을 정의한다. 대상 독자는
C API와 bindings에서 오류를 안정적으로 분류하는 개발자다. result는 제어 흐름의 기준이고 errno는 같은
실패를 더 세밀하게 설명한다.

## 1. 공통 우선순위

한 호출에서 여러 실패 조건이 겹치면 다음 순서로 하나를 반환한다.

1. `NULL`, 잘못된 길이, enum 값과 malformed metadata 같은 argument 오류
2. handle 종류와 lifecycle state 오류
3. target, generation, binding과 connection 조회 오류
4. admission, capacity와 backpressure 오류
5. transport와 내부 runtime 오류

성공한 함수의 errno는 정의하지 않는다. 아래 표에 여러 errno가 있으면 함수 owner 문서가 그 조건을
구분한다.

## 2. Submit result

| Result | errno | 의미 |
|---|---|---|
| `ZLINK_SUBMIT_OK` | - | 함수가 정한 ownership 전이가 완료됨 |
| `ZLINK_SUBMIT_BACKPRESSURED` | `EAGAIN`, `ETIMEDOUT`, `ENOBUFS` | queue 또는 reservation capacity 부족 |
| `ZLINK_SUBMIT_NOT_CONNECTED` | `ENOTCONN` | target pipe 또는 session 연결 없음 |
| `ZLINK_SUBMIT_NOT_FOUND` | `ENOENT` | channel member, Spot, Actor 또는 binding 없음 |
| `ZLINK_SUBMIT_NOT_ADMITTED` | `EACCES` | target route는 식별했지만 handshake 또는 신규 outbound weight 같은 현재 admission 정책이 submit을 거부함 |
| `ZLINK_SUBMIT_TERMINATED` | `ETERM`, `ESHUTDOWN` | Context 또는 service lifecycle 종료 |
| `ZLINK_SUBMIT_INVALID_HANDLE` | `EFAULT` | handle이 `NULL`이거나 종류가 다름 |
| `ZLINK_SUBMIT_INVALID_ARGUMENT` | `EINVAL`, `EMSGSIZE` | 잘못된 pointer, count, name, metadata 또는 flags |
| `ZLINK_SUBMIT_NOT_SUPPORTED` | `ENOTSUP` | handle에서 지원하지 않는 operation |
| `ZLINK_SUBMIT_INVALID_STATE` | `EBUSY`, `ESTALE`, `EALREADY`, `ESHUTDOWN` | lifecycle, generation, token 또는 binding state 오류 |
| `ZLINK_SUBMIT_THREAD_VIOLATION` | `EDEADLK`, `EPERM` | 금지한 callback 재진입 또는 thread 사용 |
| `ZLINK_SUBMIT_OUT_OF_MEMORY` | `ENOMEM` | 필요한 storage 확보 실패 |
| `ZLINK_SUBMIT_SEQ_EXHAUSTED` | `EOVERFLOW` | operation sequence 공간 소진 |
| `ZLINK_SUBMIT_INTERNAL_ERROR` | 보존된 errno | 다른 공개 분류가 없는 내부 실패 |

이 결과는 raw socket send/request submit, MeshNode node·channel·publisher, Spot direct·channel·publish,
Actor messaging과 STREAM session operation에 적용한다. service complete multipart input은 성공과 실패 모두
borrowed ownership을 유지한다. raw socket input ownership은 각 socket 문서가 정한다.

## 3. Request completion result

| Result | errno | 의미 |
|---|---|---|
| `ZLINK_REQUEST_OK` | - | terminal success |
| `ZLINK_REQUEST_TIMED_OUT` | `ETIMEDOUT` | operation 또는 shutdown deadline 만료 |
| `ZLINK_REQUEST_NOT_FOUND` | `ENOENT` | terminal target 부재 |
| `ZLINK_REQUEST_TERMINATED` | `ETERM`, `ESHUTDOWN` | owner lifecycle 종료 |
| `ZLINK_REQUEST_PROTOCOL_ERROR` | `EPROTO`, `ENOCOMPATPROTO` | malformed 또는 호환되지 않는 reply |
| `ZLINK_REQUEST_INTERNAL_ERROR` | 보존된 errno | 다른 terminal 분류가 없는 내부 실패 |
| `ZLINK_REQUEST_REJECTED` | `EACCES`, `ECANCELED` | application 또는 admission 거절 |
| `ZLINK_REQUEST_CONFLICT` | `EEXIST`, `ESTALE` | compare-and-set 또는 generation 충돌 |
| `ZLINK_REQUEST_BUSY` | `EBUSY` | active claim, binding 또는 lifecycle operation 존재 |
| `ZLINK_REQUEST_NOT_CONNECTED` | `ENOTCONN` | terminal route 단절 |
| `ZLINK_REQUEST_INVALID_ARGUMENT` | `EINVAL` | asynchronous validation 실패 |
| `ZLINK_REQUEST_INVALID_STATE` | `ESTALE`, `EALREADY`, `ESHUTDOWN` | terminal token 또는 lifecycle state 오류 |
| `ZLINK_REQUEST_NOT_SUPPORTED` | `ENOTSUP` | operation 미지원 |
| `ZLINK_REQUEST_BACKPRESSURED` | `EAGAIN`, `ENOBUFS` | non-blocking mailbox admission 또는 원자적 전체 reservation 실패 |

request submit 성공 뒤에는 operation ID마다 이 terminal result를 정확히 한 번 completion record로
전달한다. synchronous shutdown과 transfer prepare도 같은 enum을 반환한다.

## 4. Receive result

| Result | errno | 의미 |
|---|---|---|
| `ZLINK_RECV_OK` | - | complete record 하나 이상 수신 |
| `ZLINK_RECV_NO_DATA` | `EAGAIN`, `ETIMEDOUT` | nonblocking 또는 receive timeout에 data 없음 |
| `ZLINK_RECV_BUSY` | `EBUSY` | 다른 receive mode 또는 같은 mutable batch 사용 중 |
| `ZLINK_RECV_TERMINATED` | `ETERM` | Context 종료 |
| `ZLINK_RECV_INVALID_HANDLE` | `EFAULT` | handle, 필수 output pointer 또는 batch가 유효하지 않음 |
| `ZLINK_RECV_NOT_SUPPORTED` | `ENOTSUP` | handle이 해당 receive를 지원하지 않음 |
| `ZLINK_RECV_INTERNAL_ERROR` | 보존된 errno | 다른 공개 분류가 없는 내부 실패 |
| `ZLINK_RECV_BUFFER_TOO_SMALL` | `ENOBUFS` | 첫 complete record를 batch에 넣을 수 없거나 raw subscription topic buffer가 0 또는 부족함 |
| `ZLINK_RECV_INVALID_STATE` | `EINVAL`, `ESTALE`, `ESHUTDOWN` | claim domain, generation 또는 revoke state 오류 |

batch receive의 `BUFFER_TOO_SMALL`에서는 batch가 비어 있고 required output이 필요한 message·part·byte
capacity를 제공한다. raw subscription receive에서는 `topic_id_len_out_`만 필요한 topic 길이로 바꾸고
queue의 topic·payload, 다른 output과 `part_out_`을 변경하지 않는다. caller는 충분한 topic buffer로 같은
message를 다시 수신한다.

## 5. Handler와 close result

| Result | errno | 의미 |
|---|---|---|
| `ZLINK_HANDLER_INVALID_ARGUMENT` | `EINVAL` | handler 또는 mask가 잘못됨 |
| `ZLINK_HANDLER_BUSY` | `EBUSY` | 배타적인 receive model이 이미 등록됨 |
| `ZLINK_HANDLER_NOT_SUPPORTED` | `ENOTSUP` | handle에서 handler를 지원하지 않음 |
| `ZLINK_HANDLER_DEADLOCK` | `EDEADLK` | 같은 callback 안의 금지한 등록·해제 |
| `ZLINK_HANDLER_INVALID_HANDLE` | `EFAULT` | handle이 유효하지 않음 |
| `ZLINK_HANDLER_INTERNAL_ERROR` | 보존된 errno | 다른 공개 분류가 없는 내부 실패 |
| `ZLINK_CLOSE_BUSY` | `EBUSY`, `EDEADLK` | `EBUSY` — active child handle, callback 또는 API가 존재함. `EDEADLK` — 같은 handle의 shutdown·destroy lifecycle 재진입(01-mesh-node §11) |
| `ZLINK_CLOSE_SHUTDOWN` | `ESHUTDOWN` | 이미 종료된 handle |
| `ZLINK_CLOSE_INVALID_HANDLE` | `EFAULT`, `ESTALE` | pointer 또는 opaque value가 유효하지 않음 |
| `ZLINK_CLOSE_INTERNAL_ERROR` | 보존된 errno | 다른 공개 분류가 없는 내부 실패 |

## 6. Bind와 connect result

| Result | errno | 의미 |
|---|---|---|
| `ZLINK_BIND_INVALID_ARGUMENT` | `EINVAL` | endpoint가 잘못됨 |
| `ZLINK_BIND_ADDR_IN_USE` | `EADDRINUSE` | endpoint가 이미 사용 중 |
| `ZLINK_BIND_NOT_SUPPORTED` | `ENOTSUP`, `EPROTONOSUPPORT` | transport 미지원 |
| `ZLINK_BIND_INVALID_HANDLE` | `EFAULT` | handle이 유효하지 않음 |
| `ZLINK_BIND_INTERNAL_ERROR` | 보존된 errno | 다른 공개 분류가 없는 bind 실패 |
| `ZLINK_CONNECT_INVALID_ARGUMENT` | `EINVAL` | endpoint 또는 expected RID가 잘못됨 |
| `ZLINK_CONNECT_NOT_SUPPORTED` | `ENOTSUP`, `EPROTONOSUPPORT` | transport 또는 operation 미지원 |
| `ZLINK_CONNECT_INVALID_HANDLE` | `EFAULT` | handle이 유효하지 않음 |
| `ZLINK_CONNECT_INTERNAL_ERROR` | 보존된 errno | 다른 공개 분류가 없는 connect 실패 |
| `ZLINK_CONNECT_NOT_FOUND` | `ENOENT` | intent 또는 peer가 없음 |
| `ZLINK_CONNECT_CONFLICT` | `EEXIST`, `ESTALE`, `EADDRINUSE` | MeshName, RID 또는 generation 충돌 |
| `ZLINK_CONNECT_BUSY` | `EBUSY`, `ESHUTDOWN` | lifecycle이 변경을 허용하지 않음 |
| `ZLINK_CONNECT_AUTH_FAILED` | `EACCES` | trust profile 또는 peer 인증 실패 |

## 7. Configuration result

| Result | errno | 의미 |
|---|---|---|
| `ZLINK_CONFIG_INVALID_HANDLE` | `EFAULT` | handle 또는 output pointer가 유효하지 않음 |
| `ZLINK_CONFIG_INVALID_ARGUMENT` | `EINVAL`, `EMSGSIZE` | option, size, name 또는 value가 잘못됨 |
| `ZLINK_CONFIG_NOT_SUPPORTED` | `ENOTSUP` | handle과 option 조합 미지원 |
| `ZLINK_CONFIG_INTERNAL_ERROR` | 보존된 errno | 다른 공개 분류가 없는 내부 실패 |
| `ZLINK_CONFIG_INVALID_STATE` | `EINVAL`, `EBUSY`, `ESTALE`, `EALREADY`, `ESHUTDOWN`, `ENOTCONN`, `ETIMEDOUT`, `EPROTO` | lifecycle phase, generation, terminal state 또는 Actor transfer data plane이 변경을 거부함 |
| `ZLINK_CONFIG_NOT_FOUND` | `ENOENT` | local query target 없음 |
| `ZLINK_CONFIG_CONFLICT` | `EEXIST` | 중복 identity, name 또는 binding |
| `ZLINK_CONFIG_BUFFER_TOO_SMALL` | `ENOBUFS` | caller output capacity 부족, partial output 없음 |
| `ZLINK_CONFIG_BUSY` | `EBUSY` | 같은 mutable object를 동시에 사용함 |

## 8. Service 함수 family

| Family | Submit·request owner | Receive·configuration owner |
|---|---|---|
| MeshNode | `zlink_mesh_node_send_*`, `request_*`, `publisher_publish` | lifecycle, peer, status와 publisher option |
| Dispatch | `zlink_mesh_reply` | ready·receive batch, claim과 handler |
| Spot | `zlink_spot_send_*`, `request_*`, `publish` | Spot lifecycle, subscription, timer와 publish option |
| Actor | Actor create·join·leave·message·transfer | Actor lookup, claim과 control record |
| STREAM session | bind·unbind·send·request·close | service lifecycle, binding query와 status |

각 함수의 exact argument, ownership과 lifecycle 조건은 위 family의 정식 service 문서가 소유한다.
