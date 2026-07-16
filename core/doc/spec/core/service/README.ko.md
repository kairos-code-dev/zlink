[English](README.md) | 한국어

[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md)

# Service API

Core 10.0.0 service 계층은 MeshNode가 transport 위치, service mailbox와 request correlation을 소유하고,
Spot·Actor·STREAM session이 각자의 논리적 상태와 수신 owner를 제공하는 구조다. raw socket은 service
객체를 알지 않으며 service는 raw frame과 peer 목록을 caller에게 노출하지 않는다.

| 문서 | 책임 |
|---|---|
| [MeshNode](mesh-node.ko.md) | MeshName, ChannelName membership, peer admission, node/channel messaging와 Logical Multicast submit |
| [Dispatch](dispatch.ko.md) | application/infrastructure ready, domain별 claim, receive batch, operation과 reply |
| [Spot](spot.ko.md) | Spot lifecycle, direct messaging, local subscription과 Logical Multicast 수신 |
| [Actor](actor.ko.md) | ActorRef, Actor mailbox, Spot membership, lifecycle과 transfer fence |
| [STREAM session](stream-session.ko.md) | raw STREAM과 MeshNode 연결, session–Actor binding, 양방향 전송과 barrier |

classic PUB/SUB와 범용 STREAM은 [socket 목차](../socket/README.ko.md)의 독립 계약이다.

## Versioned service 구조체 공통 계약

`struct_size`와 `version`을 첫 두 field로 가지는 모든 service 구조체는 아래 세 ownership 분류 가운데 하나를
따른다. 이 표가 각 owner 문서의 공통 size, version과 오류 계약을 소유한다.

| 분류 | 구조체와 사용 위치 |
|---|---|
| caller가 초기화하는 input | `zlink_mesh_node_options_t`, `zlink_mesh_peer_connection_options_t`, `zlink_actor_transfer_prepare_t`, `zlink_mesh_monitor_open_options_t` |
| caller가 초기화하는 output | `zlink_mesh_publish_detail_t`, `zlink_mesh_node_status_t`, `zlink_mesh_peer_entry_t`, `zlink_spot_status_t`, local lookup의 `zlink_actor_location_t`, `zlink_mesh_receive_requirements_t`, `zlink_stream_session_binding_t`, `zlink_stream_session_status_t`, monitor recv의 `zlink_mesh_monitor_event_t`, `zlink_mesh_monitor_status_t` |
| Core가 소유하는 read-only view | ready batch의 `zlink_mesh_ready_record_t`, receive batch의 `zlink_mesh_receive_record_t`, `kind_data`의 `zlink_mesh_send_ready_data_t`, `zlink_actor_control_record_t`, `zlink_actor_join_completion_t`, `zlink_actor_transfer_control_t`, completion `kind_data`의 `zlink_actor_location_t`, monitor callback의 `zlink_mesh_monitor_event_t` |

caller-init input과 output은 호출 전에 `struct_size = sizeof(해당 타입)`, `version = 1`로 설정한다. 배열 output은
capacity 안의 각 element를 같은 방식으로 초기화한다. Core는 `struct_size`가 현재 공개 타입 크기 이상이고
`version == 1`인지 다른 field보다 먼저 검사하며, 알려진 현재 타입 크기를 넘는 tail은 읽거나 쓰지 않는다.
크기가 작거나 version이 다르면 output payload를 일부 기록하지 않고 `errno == EINVAL`로 실패한다.

| API result family | size/version 실패 결과 |
|---|---|
| handle을 반환하는 constructor 또는 monitor open | `NULL`, `errno == EINVAL` |
| connect | `ZLINK_CONNECT_INVALID_ARGUMENT`, `errno == EINVAL` |
| submit | `ZLINK_SUBMIT_INVALID_ARGUMENT`, `errno == EINVAL` |
| synchronous request | `ZLINK_REQUEST_INVALID_ARGUMENT`, `errno == EINVAL` |
| configuration/query | `ZLINK_CONFIG_INVALID_ARGUMENT`, `errno == EINVAL` |
| receive | `ZLINK_RECV_INVALID_STATE`, `errno == EINVAL` |

caller-init output storage는 caller가 계속 소유한다. 성공 시 Core는 현재 version의 전체 공개 prefix를 채운다.
Core-owned view는 Core가 `struct_size = sizeof(해당 타입)`, `version = 1`로 채우므로 caller가 초기화하지 않는다.
이 view와 그 내부 pointer는 해당 callback 호출 또는 owner batch 수명까지만 유효하며 수정하거나 해제할 수 없다.
같은 타입이 두 위치에서 쓰이면 위치별 분류를 적용한다. 예를 들어 local Actor lookup output은 caller-init
storage이고 completion의 Actor location은 receive batch가 소유하는 view다.
