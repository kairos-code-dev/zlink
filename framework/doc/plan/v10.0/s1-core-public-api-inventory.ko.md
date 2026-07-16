# S1 Core public API 전수 inventory

## 0. 문서 상태

이 문서는 정식 spec이 아니라 S1 실행을 위한 임시 추적 문서다. 현재 checkout의 공개 header 집합과
현재 checkout에서 만들어진 동적 library의 export 집합을 고정하고, 각 항목을 Core 10.0.0 계약에서 어떻게
처리할지 기록한다. 공개 계약은 `core/doc/spec/` 아래의 정식 문서가 소유하며, 이 inventory가 정식 spec을
대신하지 않는다.

이 문서에서 공개 header 집합과 동적 export 집합은 서로 다른 검증 universe다. 같은 함수가 두 표에 한 번씩
나오는 것은 중복이 아니다. 함수 표에서는 header 선언을, export 표에서는 동적 symbol을 각각 정확히 한 번
검증한다.

## 1. 분류 규칙

| 분류 | 의미 |
|---|---|
| 10.0.0 계약에 유지 | 현재 공개 identifier가 10.0.0 공개 계약에도 남음 |
| 10.0.0 정식 service API로 대체 | 현재 identifier는 공개 표면에서 없어지고 정식 service owner의 새 identifier 또는 record 계약이 책임을 이어받음 |
| 제거 | 현재 identifier와 해당 공개 책임을 10.0.0 공개 표면에 두지 않음 |
| header 없는 internal export 정리 | 공개 header 선언이 없는 동적 export이며 public API로 승격하지 않고 export table에서 제거함 |

제거된 enum 숫자를 reserved로 남기는 것은 해당 public enumerator를 유지한다는 뜻이 아니다. 예약은 새 의미로
재사용하지 않기 위한 ABI 규칙이고, 이 inventory에서는 공개 identifier의 존속 여부를 분류한다.

## 2. 입력과 정규화

- `core/include/zlink.h`가 포함하는 저장소 header 폐쇄 11개를 입력으로 사용했다.
- 함수는 `ZLINK_EXPORT`가 붙은 선언을 함수 이름으로 정규화했다.
- public type은 이름이 `zlink_`로 시작하는 `typedef`다. enum type은 별도 집합으로 분리했다.
- 조건부 platform 선언은 public type 또는 field 이름이 같으면 한 항목으로 정규화했다.
- field는 공개 struct의 `struct-name.field-name`으로 정규화했다.
- macro는 caller가 사용할 `ZLINK_` 이름만 포함한다. include guard, `ZLINK_EXPORT`,
  `ZLINK_DEFINED_STDINT`는 공개 계약 macro에서 제외했고, 두 header에 반복된 version macro는 이름으로
  한 번만 셌다.
- 동적 export는 `nm -D --defined-only` 결과에서 `zlink_`로 시작하는 이름을 정렬·중복 제거했다.

source에 나타난 `#define ZLINK_...` 지시문은 56개다. 여기에는 include guard 8개, build visibility를
위한 `ZLINK_EXPORT` 조건 분기 7개, `ZLINK_DEFINED_STDINT` 1개와 두 header에 중복된 version macro
5개가 포함된다. 이를 제외하거나 이름으로 합치면 caller가 사용하는 public macro identifier는 35개다.

public struct type은 29개다. struct tag가 있는 선언만 세면 28개지만, tag 없이 typedef한
`zlink_monitor_event_t`도 공개 type이므로 전수 inventory에는 포함했다. 비-enum type 51개는 이 struct
29개와 scalar alias·callback·function type 22개를 합친 수치다.
### 2.1 입력 SHA-256

```text
73622ec627960a334d0ae32f654b9ec13fc30d9305c69f2809af53325f896c04  core/include/zlink.h
da6939626fb79e6f0bca574f92f39a948ae218c25520b5d262137bbc6c5f014e  core/include/zlink/common.h
e3bc277cad31750ef5007982688a630e989602ff0208f90856819d538c99619c  core/include/zlink/core/api.h
bf2cf4ed3a43c1215c0924b9e78f595c70a31152387872b30b083c65305d3eac  core/include/zlink/message/api.h
c23a3250effc0093bb5918b02f0a9bb235acff65a35e1aa0b12a22ef3afed2e9  core/include/zlink/service/actor.h
aeb1b9bd9046fb0235fddcc70ff64f4b306eb23c56c61d50ab51a20b9a0f308e  core/include/zlink/socket/api.h
fbd0ea335d9c3790f665ec90b5731950b1ca52891ff817fbe4ce1d91a744e56e  core/include/zlink/eventing/api.h
f58400148faf9b758f609fab13fa10b08623f59b42cfb9bd91c74f10a4684c8c  core/include/zlink/service/spot.h
d24841177e7413a348bc92c05bc537c65de1d2762a1e650c4d5f5db8be9cf575  core/include/zlink/service/common.h
5a88aa32643100272617d8ad21f0843e01463ea0b3f0f7fd8399e5f2637d5fce  core/include/zlink_enum.h
654ab50726cc545e33bb7b1a1059437eb354866813eecfa9b7ae8d4cb6eec6ca  core/include/zlink_errno.h
```

| 입력 | SHA-256 |
|---|---|
| 위 header manifest 전체 | `d2dbd9a3061403980eeb9ff0356cf5bace8f546d7834e6001f31a82954622b16` |
| `core/build/lib/libzlink.so`가 가리키는 동적 library bytes | `ba55783a1971e3d28ebf6f3b367cfae289bdadc3d3e8fd52275b7833acc7e754` |
| 정규화한 header inventory TSV | `c28aaeff2f28c509a488d69f5f8d0f9c2a6089098cc0db94b017cf158cb9560c` |
| 정렬·중복 제거한 `zlink_*` export 이름 | `b85b8119936e7ba1ace7b791d5985bd2ef122928b86b01ebced7280e8882effd` |
| 정렬·중복 제거한 public macro 이름 | `4dca975acc7490a1695f147253bc705e19a36fbc7f24a173f8b3cbd8fde69325` |

### 2.2 정확한 추출 명령

```bash
cd /home/hep7/project/kairos/zlink

headers=(
  core/include/zlink.h
  core/include/zlink/common.h
  core/include/zlink/core/api.h
  core/include/zlink/message/api.h
  core/include/zlink/service/actor.h
  core/include/zlink/socket/api.h
  core/include/zlink/eventing/api.h
  core/include/zlink/service/spot.h
  core/include/zlink/service/common.h
  core/include/zlink_enum.h
  core/include/zlink_errno.h
)
sha256sum "${headers[@]}"
sha256sum "${headers[@]}" | sha256sum
sha256sum core/build/lib/libzlink.so

nm -D --defined-only core/build/lib/libzlink.so \
  | awk '$3 ~ /^zlink_/ {print $3}' \
  | LC_ALL=C sort -u \
  | tee /tmp/zlink-export.names \
  | sha256sum

gcc -E -dM -Icore/include core/include/zlink.h \
  | awk '$2 ~ /^ZLINK_/ {sub(/\(.*/, "", $2); print $2}' \
  | grep -Ev '^(ZLINK_(COMMON|CORE_API|EVENTING_API|MESSAGE_API|SERVICE_ACTOR|SERVICE_COMMON|SERVICE_SPOT|SOCKET_API)_H_INCLUDED|ZLINK_DEFINED_STDINT)$' \
  | LC_ALL=C sort -u \
  | tee /tmp/zlink-public-macro.names \
  | wc -l
sha256sum /tmp/zlink-public-macro.names
```

함수, type, enum, enumerator와 field는 여러 줄 선언과 조건부 선언을 함께 처리해야 하므로 §10의 검증 명령에서
동일한 11개 header를 Python 정규식 parser로 다시 추출한다. 그 명령은 이 문서의 각 kind별 행과 추출 집합을
양방향 비교한다.

## 3. 수량과 분류 합계

| Universe | Kind | 전체 | 유지 | service 대체 | 제거 | internal export 정리 |
|---|---|---:|---:|---:|---:|---:|
| 공개 header | 함수 | 183 | 108 | 57 | 18 | 0 |
| 공개 header | 비-enum type | 51 | 23 | 17 | 11 | 0 |
| 공개 header | enum type | 48 | 33 | 7 | 8 | 0 |
| 공개 header | enumerator | 311 | 246 | 27 | 38 | 0 |
| 공개 header | struct field | 158 | 49 | 65 | 44 | 0 |
| 공개 header | public macro | 35 | 25 | 0 | 10 | 0 |
| 동적 library | `zlink_*` export | 213 | 108 | 57 | 18 | 30 |

공개 header 함수 183개는 모두 동적 export에 존재한다. 동적 export 213개 가운데
공개 header 함수와 일치하는 항목은 183개이고, header 선언이 없는 항목은
30개다.
## 4. 공개 함수

| Kind | Identifier | 분류 | 10.0.0 처리 또는 근거 | 현재 checkout 위치 |
|---|---|---|---|---|
| FUNC | `zlink_atomic_counter_dec` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_atomic_counter_destroy` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_atomic_counter_inc` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_atomic_counter_new` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_atomic_counter_set` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_atomic_counter_value` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_bind` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_close` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_connect` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_ctx_auto_hwm_recalculate` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_ctx_get` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_ctx_new` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_ctx_set` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_ctx_set_data` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_ctx_shutdown` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_ctx_term` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_dealer_recv_part` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_dealer_reply_part` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_dealer_request_part` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_disconnect` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_disconnect_rid` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_errno` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_get_dealer_option` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_get_option` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_get_pub_option` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_get_router_option` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_get_routing_id` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_get_spot_node_option` | 10.0.0 정식 service API로 대체 | zlink_get_mesh_node_option | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_get_spot_option` | 제거 | Spot request timeout은 operation 인자가 소유하므로 Spot option bag을 두지 않음 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_get_stream_option` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_get_sub_option` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_has` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_monitor_close` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_monitor_ignore_handler` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_monitor_status` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_msg_adopt` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| FUNC | `zlink_msg_close` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| FUNC | `zlink_msg_copy` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| FUNC | `zlink_msg_data` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| FUNC | `zlink_msg_gets` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| FUNC | `zlink_msg_init` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| FUNC | `zlink_msg_init_data` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| FUNC | `zlink_msg_init_size` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| FUNC | `zlink_msg_move` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| FUNC | `zlink_msg_refcnt` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| FUNC | `zlink_msg_size` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| FUNC | `zlink_multipart_close` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| FUNC | `zlink_poll` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_poller_add` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_poller_add_fd` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_poller_add_timer` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_poller_destroy` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_poller_modify` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_poller_modify_fd` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_poller_new` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_poller_remove` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_poller_remove_fd` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_poller_remove_timer` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_poller_size` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_poller_wait` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_proxy` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_proxy_steerable` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_publish_part` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_recv_handler` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_recv_part` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_remote_actor_get_ref` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_actor_lookup_remote | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_router_recv_part` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_router_reply_part` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_router_reply_spot_part` | 10.0.0 정식 service API로 대체 | zlink_mesh_reply | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_router_request_part` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_router_request_spot_part` | 10.0.0 정식 service API로 대체 | MeshNode service request ingress 계약 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_router_send_spot_part` | 10.0.0 정식 service API로 대체 | MeshNode service send ingress 계약 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_send_part` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_send_part_rid` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_send_ready_handler` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_set_dealer_option` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_set_option` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_set_pub_option` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_set_router_option` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_set_routing_id` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_set_spot_node_option` | 10.0.0 정식 service API로 대체 | zlink_set_mesh_node_option | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_set_spot_option` | 제거 | Spot request timeout은 operation 인자가 소유하므로 Spot option bag을 두지 않음 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_set_stream_option` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_set_sub_option` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_set_subscription` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_set_tls_client` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_set_tls_server` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_sleep` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_socket` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_socket_get_channel_name` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_socket_monitor_handler` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_socket_monitor_open` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_socket_monitor_recv` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_socket_set_channel_name` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_spot_actor_join_recv` | 10.0.0 정식 service API로 대체 | Spot control claim/batch 계약 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_actor_join_reply` | 10.0.0 정식 service API로 대체 | zlink_actor_join_reply | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_actors` | 제거 | Actor location은 Actor lookup과 location authority가 소유하며 Spot-local inventory query를 공개하지 않음 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_destroy` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_dispatch_event_handler` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_set_ready_handler | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_spot_drain_channel_reply` | 10.0.0 정식 service API로 대체 | infrastructure completion claim/batch 계약 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_drain_reply` | 10.0.0 정식 service API로 대체 | infrastructure completion claim/batch 계약 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_new` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_actor_bind_remote_session` | 10.0.0 정식 service API로 대체 | zlink_stream_session_bind_actor | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_actor_close_bound_session` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_actor_close_bound_session | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_actor_destroy` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_actor_destroy | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_actor_forward_bound_session_part` | 10.0.0 정식 service API로 대체 | zlink_stream_session_send_to_actor | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_actor_join_entry_spot` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_actor_join_entry_spot | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_actor_join_spot` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_actor_join_spot | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_actor_leave_spot` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_actor_leave_spot | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_actor_lookup` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_actor_lookup | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_actor_new` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_actor_new | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_actor_new_with_request` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_actor_new | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_actor_recv_part` | 10.0.0 정식 service API로 대체 | zlink_mesh_claim_recv_batch | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_actor_reply_no_bind` | 10.0.0 정식 service API로 대체 | zlink_mesh_reply | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_actor_send_bound_session_msg` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_actor_send_bound_session | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_actors` | 제거 | MeshNode 전체 Actor inventory query를 제공하지 않으며 Actor location은 exact lookup/completion 계약이 소유함 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_connect_peer` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_connect_peer | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_connect_peer_rid` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_connect_peer | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_destroy` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_destroy | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_disconnect_peer` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_remove_peer_connection | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_disconnect_peer_rid` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_disconnect_peer | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_entry_spot` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_entry_spot | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_internal_sockets` | 제거 | 내부 socket과 mailbox 배선을 공개 query로 노출하지 않음 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_new` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_new | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_peers` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_peers | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_publisher_close` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_publisher_destroy | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_publisher_new` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_publisher_new | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_publisher_publish` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_publisher_publish | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_request_to_actor` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_request_to_actor | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_send_to_actor` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_send_to_actor | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_set_pub_bind` | 제거 | MeshNode는 PUB/SUB network plane을 만들지 않음 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_set_pub_routing_id` | 제거 | MeshNode는 PUB network identity를 갖지 않음 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_set_router_bind` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_set_bind | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_set_sub_routing_id` | 제거 | MeshNode는 SUB network identity를 갖지 않음 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_spot_get_or_new` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_spot_get_or_new | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_spot_lookup` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_spot_lookup | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_spots` | 제거 | MeshNode 전체 Spot inventory query를 제공하지 않으며 facade별 status만 공개함 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_status` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_status | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_node_subjects` | 제거 | remote subscription inventory를 공개하지 않음 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_publish_part` | 10.0.0 정식 service API로 대체 | zlink_spot_publish | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_spot_recv_actor_lifecycle` | 10.0.0 정식 service API로 대체 | Spot control claim/batch 계약 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_recv_actor_lifecycle_with_request` | 10.0.0 정식 service API로 대체 | Spot control claim/batch 계약 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_recv_part` | 10.0.0 정식 service API로 대체 | zlink_mesh_claim_recv_batch | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_spot_recv_subscription_event` | 제거 | remote subscription control event를 공개하지 않음 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_spot_reply_router_part` | 10.0.0 정식 service API로 대체 | zlink_mesh_reply | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_spot_reply_spot_part` | 10.0.0 정식 service API로 대체 | zlink_mesh_reply | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_spot_request_channel_part` | 10.0.0 정식 service API로 대체 | zlink_spot_request_to_channel | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_spot_request_router_part` | 10.0.0 정식 service API로 대체 | MeshNode service send/request 계약 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_spot_request_spot_part` | 10.0.0 정식 service API로 대체 | zlink_spot_request_to_spot | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_spot_route_bridge_attach_router_channel` | 제거 | bridge endpoint 연결을 공개 계약에서 제거 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_route_bridge_close` | 제거 | bridge handle을 공개 계약에서 제거 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_route_bridge_drain` | 제거 | bridge drain을 공개 계약에서 제거 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_route_bridge_handle_router_received` | 제거 | bridge ingress를 공개 계약에서 제거 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_route_bridge_new` | 제거 | bridge handle을 공개 계약에서 제거 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_route_bridge_request` | 제거 | bridge request를 공개 계약에서 제거 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_route_bridge_send` | 제거 | bridge 전송을 공개 계약에서 제거 | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_spot_send_channel_part` | 10.0.0 정식 service API로 대체 | zlink_spot_send_to_channel | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_spot_send_spot_part` | 10.0.0 정식 service API로 대체 | zlink_spot_send_to_spot | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_spot_subscribe_part` | 10.0.0 정식 service API로 대체 | zlink_mesh_claim_recv_batch | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_spot_timer_new` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_stopwatch_intermediate` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_stopwatch_start` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_stopwatch_stop` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_stream_bind_actor` | 10.0.0 정식 service API로 대체 | zlink_stream_session_bind_actor | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_stream_bound_actors` | 10.0.0 정식 service API로 대체 | zlink_stream_session_bindings | `core/include/zlink/service/spot.h` |
| FUNC | `zlink_stream_packet_handler` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_stream_send_bound_actor_part` | 10.0.0 정식 service API로 대체 | zlink_stream_session_send_to_actor | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_stream_unbind_actor` | 10.0.0 정식 service API로 대체 | zlink_stream_session_unbind_actor | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_strerror` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_subscribe_part` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_subscription_at` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_thread_join` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_thread_start` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_timer_destroy` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_timer_handler` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_timer_new` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_timer_recv` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_timer_start` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_timer_stop` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FUNC | `zlink_unbind` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_unset_subscription` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| FUNC | `zlink_version` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| FUNC | `zlink_xpub_recv_part` | 10.0.0 계약에 유지 | 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |

## 5. 비-enum public type

| Kind | Identifier | 분류 | 10.0.0 처리 또는 근거 | 현재 checkout 위치 |
|---|---|---|---|---|
| TYPE | `zlink_actor_join_entry_spot_handler_fn` | 10.0.0 정식 service API로 대체 | operation completion claim/batch 계약 | `core/include/zlink/socket/api.h` |
| TYPE | `zlink_actor_join_entry_spot_result_t` | 10.0.0 정식 service API로 대체 | operation completion record 계약 | `core/include/zlink/service/actor.h` |
| TYPE | `zlink_actor_join_info_t` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t와 reply token | `core/include/zlink/service/actor.h` |
| TYPE | `zlink_actor_join_result_t` | 10.0.0 정식 service API로 대체 | operation completion record 계약 | `core/include/zlink/service/actor.h` |
| TYPE | `zlink_actor_join_spot_handler_fn` | 10.0.0 정식 service API로 대체 | operation completion claim/batch 계약 | `core/include/zlink/socket/api.h` |
| TYPE | `zlink_actor_lookup_handler_fn` | 10.0.0 정식 service API로 대체 | operation completion claim/batch 계약 | `core/include/zlink/socket/api.h` |
| TYPE | `zlink_actor_lookup_result_t` | 10.0.0 정식 service API로 대체 | completion의 zlink_actor_location_t data | `core/include/zlink/service/actor.h` |
| TYPE | `zlink_actor_recv_info_t` | 10.0.0 정식 service API로 대체 | zlink_mesh_receive_record_t | `core/include/zlink/service/actor.h` |
| TYPE | `zlink_actor_ref_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/service/actor.h` |
| TYPE | `zlink_actor_route_t` | 10.0.0 정식 service API로 대체 | zlink_actor_location_t | `core/include/zlink/service/actor.h` |
| TYPE | `zlink_channel_role_t` | 제거 | channel role alias를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| TYPE | `zlink_fd_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| TYPE | `zlink_free_fn` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/message/api.h` |
| TYPE | `zlink_monitor_event_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| TYPE | `zlink_monitor_handler_fn` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| TYPE | `zlink_monitor_state_mask_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| TYPE | `zlink_monitor_status_detail_mask_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| TYPE | `zlink_monitor_status_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| TYPE | `zlink_msg_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/message/api.h` |
| TYPE | `zlink_poller_event_mask_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| TYPE | `zlink_poller_event_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| TYPE | `zlink_pollitem_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| TYPE | `zlink_reply_handler_fn` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/socket/api.h` |
| TYPE | `zlink_routing_id_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/message/api.h` |
| TYPE | `zlink_send_ready_handler_fn` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/socket/api.h` |
| TYPE | `zlink_service_event_detail_mask_t` | 제거 | 현재 service event detail family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| TYPE | `zlink_socket_monitor_event_mask_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| TYPE | `zlink_socket_monitor_event_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| TYPE | `zlink_socket_monitor_handler_fn` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| TYPE | `zlink_socket_monitor_open_options_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| TYPE | `zlink_socket_msg_handler_fn` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/socket/api.h` |
| TYPE | `zlink_spot_actor_lifecycle_event_t` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t | `core/include/zlink/service/actor.h` |
| TYPE | `zlink_spot_actor_lifecycle_info_t` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t | `core/include/zlink/service/actor.h` |
| TYPE | `zlink_spot_dispatch_event_handler_fn` | 10.0.0 정식 service API로 대체 | zlink_mesh_ready_handler_fn | `core/include/zlink/socket/api.h` |
| TYPE | `zlink_spot_dispatch_info_t` | 10.0.0 정식 service API로 대체 | zlink_mesh_ready_record_t | `core/include/zlink/socket/api.h` |
| TYPE | `zlink_spot_node_actor_entry_t` | 제거 | 제거한 MeshNode Actor inventory query 전용 output type | `core/include/zlink/service/spot.h` |
| TYPE | `zlink_spot_node_options_t` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_options_t | `core/include/zlink/service/spot.h` |
| TYPE | `zlink_spot_node_peer_entry_t` | 10.0.0 정식 service API로 대체 | zlink_mesh_peer_entry_t | `core/include/zlink/service/spot.h` |
| TYPE | `zlink_spot_node_peer_filter_t` | 10.0.0 정식 service API로 대체 | MeshNode peer query 입력 | `core/include/zlink/service/spot.h` |
| TYPE | `zlink_spot_node_socket_entry_t` | 제거 | 제거한 internal socket query 전용 output type | `core/include/zlink/service/spot.h` |
| TYPE | `zlink_spot_node_socket_filter_t` | 제거 | 제거한 internal socket query 전용 input type | `core/include/zlink/service/spot.h` |
| TYPE | `zlink_spot_node_spot_entry_t` | 제거 | 제거한 MeshNode Spot inventory query 전용 output type | `core/include/zlink/service/spot.h` |
| TYPE | `zlink_spot_node_status_t` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_status_t | `core/include/zlink/service/spot.h` |
| TYPE | `zlink_spot_node_subject_entry_t` | 제거 | remote subscription inventory를 제거 | `core/include/zlink/service/spot.h` |
| TYPE | `zlink_spot_node_subject_filter_t` | 제거 | remote subscription inventory를 제거 | `core/include/zlink/service/spot.h` |
| TYPE | `zlink_spot_route_bridge_endpoint_options_t` | 제거 | bridge 계약을 제거 | `core/include/zlink/service/spot.h` |
| TYPE | `zlink_spot_route_bridge_options_t` | 제거 | bridge 계약을 제거 | `core/include/zlink/service/spot.h` |
| TYPE | `zlink_stream_packet_handler_fn` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/socket/api.h` |
| TYPE | `zlink_subscribe_handler_fn` | 제거 | 공개 handler 등록 계약이 없음 | `core/include/zlink/socket/api.h` |
| TYPE | `zlink_thread_fn` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/core/api.h` |
| TYPE | `zlink_timer_handler_fn` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |

## 6. enum type과 enumerator

| Kind | Identifier | 분류 | 10.0.0 처리 또는 근거 | 현재 checkout 위치 |
|---|---|---|---|---|
| ENUM_TYPE | `zlink_auto_hwm_profile_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_auto_hwm_recalc_reason_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_bind_result_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUM_TYPE | `zlink_close_result_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUM_TYPE | `zlink_config_result_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUM_TYPE | `zlink_connect_result_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUM_TYPE | `zlink_ctx_option_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_dealer_message_type_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_dealer_option_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_disconnect_reason_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_handler_result_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUM_TYPE | `zlink_monitor_source_kind_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_monitor_state_flag_e` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_monitor_status_detail_flag_e` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_option_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_part_flag_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/socket/api.h` |
| ENUM_TYPE | `zlink_poller_event_flag_e` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_poller_source_kind_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_protocol_error_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_pub_option_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_recv_flags_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_recv_result_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUM_TYPE | `zlink_request_result_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUM_TYPE | `zlink_rid_duplicate_policy_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_router_option_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_send_flags_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_service_event_detail_flag_e` | 제거 | 현재 service event detail family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_service_event_subject_kind_t` | 제거 | remote subject event family를 제거 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_service_role_t` | 제거 | 현재 service/channel role family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_socket_monitor_event_e` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_socket_type_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_spot_actor_lifecycle_event_kind_t` | 10.0.0 정식 service API로 대체 | zlink_actor_lifecycle_kind_t | `core/include/zlink/service/actor.h` |
| ENUM_TYPE | `zlink_spot_dispatch_event_t` | 10.0.0 정식 service API로 대체 | Mesh ready domain과 record kind | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_spot_dispatch_subject_kind_t` | 10.0.0 정식 service API로 대체 | zlink_mesh_owner_kind_t | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_spot_kind_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/service/common.h` |
| ENUM_TYPE | `zlink_spot_node_mode_t` | 제거 | MeshNode는 하나의 ROUTER network plane만 사용 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_spot_node_option_t` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_option_t | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_spot_node_socket_owner_t` | 제거 | 제거한 internal socket query 전용 enum | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_spot_node_state_t` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_state_t | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_spot_option_t` | 제거 | Spot option bag 제거와 함께 type을 제거 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_spot_peer_kind_t` | 제거 | 두 peer kind를 하나의 Mesh peer 계약으로 통합 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_spot_peer_source_t` | 10.0.0 정식 service API로 대체 | zlink_mesh_peer_source_t | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_spot_peer_state_t` | 10.0.0 정식 service API로 대체 | zlink_mesh_peer_state_t | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_spot_role_t` | 제거 | remote Spot PUB/SUB role을 제거 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_stream_option_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_sub_option_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUM_TYPE | `zlink_submit_result_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUM_TYPE | `zlink_submit_retry_mode_t` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_AUTO_HWM_PROFILE_BALANCED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_AUTO_HWM_PROFILE_COMPACT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_AUTO_HWM_PROFILE_THROUGHPUT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_AUTO_HWM_RECALC_REASON_DEFERRED_SHRINK` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_AUTO_HWM_RECALC_REASON_INITIAL` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_AUTO_HWM_RECALC_REASON_NONE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_AUTO_HWM_RECALC_REASON_POLICY_TOGGLE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_AUTO_HWM_RECALC_REASON_REFRESH` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_AUTO_HWM_RECALC_REASON_ROLE_CHANGE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_BIND_ADDR_IN_USE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_BIND_INTERNAL_ERROR` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_BIND_INVALID_ARGUMENT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_BIND_INVALID_HANDLE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_BIND_NOT_SUPPORTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_BIND_OK` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CLOSE_BUSY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CLOSE_INTERNAL_ERROR` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CLOSE_INVALID_HANDLE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CLOSE_OK` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CLOSE_SHUTDOWN` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CONFIG_INTERNAL_ERROR` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CONFIG_INVALID_ARGUMENT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CONFIG_INVALID_HANDLE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CONFIG_INVALID_STATE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CONFIG_NOT_FOUND` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CONFIG_NOT_SUPPORTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CONFIG_OK` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CONNECT_BUSY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CONNECT_CONFLICT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CONNECT_INTERNAL_ERROR` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CONNECT_INVALID_ARGUMENT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CONNECT_INVALID_HANDLE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CONNECT_NOT_FOUND` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CONNECT_NOT_SUPPORTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CONNECT_OK` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_CTX_OPT_AUTO_HWM_ENABLE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_CTX_OPT_AUTO_HWM_PROFILE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_CTX_OPT_AUTO_HWM_RECALC_DEBOUNCE_MS` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_CTX_OPT_BLOCKY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_DEALER_MESSAGE_ERROR_REPLY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_DEALER_MESSAGE_RAW` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_DEALER_MESSAGE_REPLY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_DEALER_MESSAGE_REQUEST` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_DEALER_OPT_PROBE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_DEALER_OPT_WEIGHT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_DISCONNECT_REASON_CTX_TERM` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_DISCONNECT_REASON_HANDSHAKE_FAILED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_DISCONNECT_REASON_TRANSPORT_ERROR` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_DISCONNECT_REASON_UNKNOWN` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_ACCEPTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_ACCEPT_FAILED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_ALL` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_BIND_FAILED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_CLOSED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_CLOSE_FAILED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_CONNECTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_CONNECTION_READY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_CONNECT_DELAYED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_CONNECT_RETRIED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_DETAIL_CHANNEL_NAME` | 제거 | 현재 service event detail family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_DETAIL_ENDPOINT` | 제거 | 현재 service event detail family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_DETAIL_PEER_RID` | 제거 | 현재 service event detail family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_DETAIL_SUBJECT` | 제거 | 현재 service event detail family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_DETAIL_SUBJECT_KIND` | 제거 | 현재 service event detail family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_DETAIL_SUBJECT_RID` | 제거 | 현재 service event detail family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_DISCONNECTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_HANDSHAKE_FAILED_AUTH` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_LISTENING` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_MONITOR_STOPPED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_EVENT_PEER_WEIGHT_CHANGED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_HANDLER_BUSY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_HANDLER_DEADLOCK` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_HANDLER_INTERNAL_ERROR` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_HANDLER_INVALID_ARGUMENT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_HANDLER_INVALID_HANDLE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_HANDLER_NOT_SUPPORTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_HANDLER_OK` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_IO_THREADS` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_MAX_MSGSZ` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_MAX_SOCKETS` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_MONITOR_SOURCE_SOCKET` | 10.0.0 계약에 유지 | raw socket monitor source를 뜻하므로 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_MONITOR_SOURCE_SPOT_PUB` | 제거 | 물리 Spot PUB/SUB monitor source를 제거 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_MONITOR_SOURCE_SPOT_SUB` | 제거 | 물리 Spot PUB/SUB monitor source를 제거 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_MONITOR_STATE_BOUND_READY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_MONITOR_STATE_CLOSED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_MONITOR_STATE_READY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_MONITOR_STATUS_DETAIL_AUTO_HWM_BUDGET` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_MONITOR_STATUS_DETAIL_AUTO_HWM_BUFFERS` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_MONITOR_STATUS_DETAIL_RCV_PENDING_MSGS` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_MONITOR_STATUS_DETAIL_SND_PENDING_MSGS` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_MSG_T_SIZE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_AFFINITY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_BACKLOG` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_BINDTODEVICE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_BLOCKY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_CONFLATE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_CONNECT_TIMEOUT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_EVENTS` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_FD` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_HANDSHAKE_IVL` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_HEARTBEAT_IVL` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_HEARTBEAT_TIMEOUT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_HEARTBEAT_TTL` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_IMMEDIATE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_INVERT_MATCHING` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_IPV6` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_LAST_ENDPOINT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_LINGER` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_MAXMSGSIZE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_MULTICAST_HOPS` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_MULTICAST_MAXTPDU` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_RATE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_RCVBUF` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_RCVHWM` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_RCVTIMEO` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_RECONNECT_IVL` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_RECONNECT_IVL_MAX` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_RECOVERY_IVL` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_RID_DUPLICATE_POLICY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_ROUTE_VALUE_MAX_SIZE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_SNDBUF` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_SNDHWM` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_SNDTIMEO` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_SUBMIT_RETRY_ATTEMPTS` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_SUBMIT_RETRY_MODE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_SUBMIT_RETRY_TIMEOUT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_TCP_KEEPALIVE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_TCP_KEEPALIVE_CNT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_TCP_KEEPALIVE_IDLE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_TCP_KEEPALIVE_INTVL` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_TCP_MAXRT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_TCP_NODELAY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_TLS_CA` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_TLS_CERT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_TLS_HOSTNAME` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_TLS_KEY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_TLS_PASSWORD` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_TLS_REQUIRE_CLIENT_CERT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_TLS_TRUST_SYSTEM` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_TLS_VERIFY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_TOS` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_TYPE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_OPT_ZMP_METADATA` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_PART_FINAL` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/socket/api.h` |
| ENUMERATOR | `ZLINK_PART_MORE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/socket/api.h` |
| ENUMERATOR | `ZLINK_POLLCOMPLETION` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_POLLERR` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_POLLER_SOURCE_FD` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_POLLER_SOURCE_SOCKET` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_POLLER_SOURCE_TIMER` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_POLLIN` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_POLLITEMS_DFLT` | 10.0.0 계약에 유지 | raw polling 기본 상수로 유지하며 정식 polling spec 반영을 확인해야 함 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_POLLOUT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_POLLPRI` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_PUB_OPT_APPROVE_SUBSCRIBE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_PUB_OPT_MANUAL` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_PUB_OPT_MANUAL_LAST_VALUE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_PUB_OPT_NODROP` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_PUB_OPT_REJECT_SUBSCRIBE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_PUB_OPT_TOPICS_COUNT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_PUB_OPT_VERBOSE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_PUB_OPT_VERBOSER` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_PUB_OPT_WELCOME_MSG` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_RECV_BUSY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_RECV_FLAGS_DONTWAIT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_RECV_FLAGS_NONE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_RECV_INTERNAL_ERROR` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_RECV_INVALID_HANDLE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_RECV_NOT_SUPPORTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_RECV_NO_DATA` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_RECV_OK` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_RECV_TERMINATED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_REQUEST_BUSY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_REQUEST_CONFLICT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_REQUEST_INTERNAL_ERROR` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_REQUEST_INVALID_ARGUMENT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_REQUEST_INVALID_STATE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_REQUEST_NOT_CONNECTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_REQUEST_NOT_FOUND` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_REQUEST_NOT_SUPPORTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_REQUEST_OK` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_REQUEST_PROTOCOL_ERROR` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_REQUEST_REJECTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_REQUEST_TERMINATED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_REQUEST_TIMED_OUT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_RID_DUPLICATE_HANDOVER` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_RID_DUPLICATE_REJECT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_ROUTER_OPT_MANDATORY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_ROUTER_OPT_PROBE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_ROUTER_OPT_WEIGHT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SEND_FLAGS_DONTWAIT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SEND_FLAGS_NONE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SERVICE_EVENT_DETAIL_CHANNEL_NAME` | 제거 | 현재 service event detail family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SERVICE_EVENT_DETAIL_ENDPOINT` | 제거 | 현재 service event detail family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SERVICE_EVENT_DETAIL_PEER_RID` | 제거 | 현재 service event detail family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SERVICE_EVENT_DETAIL_SUBJECT` | 제거 | 현재 service event detail family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SERVICE_EVENT_DETAIL_SUBJECT_KIND` | 제거 | 현재 service event detail family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SERVICE_EVENT_DETAIL_SUBJECT_RID` | 제거 | 현재 service event detail family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SERVICE_EVENT_SUBJECT_NONE` | 제거 | remote subject event family를 제거 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SERVICE_EVENT_SUBJECT_PATTERN` | 제거 | remote subject event family를 제거 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SERVICE_EVENT_SUBJECT_TOPIC` | 제거 | remote subject event family를 제거 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SERVICE_ROLE_DEALER` | 제거 | 현재 service/channel role family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SERVICE_ROLE_INVALID` | 제거 | 현재 service/channel role family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SERVICE_ROLE_PUB` | 제거 | 현재 service/channel role family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SERVICE_ROLE_ROUTER` | 제거 | 현재 service/channel role family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SERVICE_ROLE_SPOT` | 제거 | 현재 service/channel role family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SERVICE_ROLE_SUB` | 제거 | 현재 service/channel role family를 목표 계약에 두지 않음 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_ANY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_DEALER` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_LIMIT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_MONITOR_EVENT_ACCEPT_FAILED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_MONITOR_EVENT_ALL` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_MONITOR_EVENT_BIND_FAILED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_MONITOR_EVENT_CLOSED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_MONITOR_EVENT_CLOSE_FAILED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_MONITOR_EVENT_CONNECTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_MONITOR_EVENT_CONNECT_DELAYED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_MONITOR_EVENT_CONNECT_RETRIED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_MONITOR_EVENT_DISCONNECTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_AUTH` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_NO_DETAIL` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_PROTOCOL` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_MONITOR_EVENT_LISTENING` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_MONITOR_EVENT_MONITOR_STOPPED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_MONITOR_EVENT_PEER_WEIGHT_CHANGED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_PAIR` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_PUB` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_ROUTER` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_STREAM` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_SUB` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_XPUB` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SOCKET_XSUB` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_ACTOR_LIFECYCLE_DISCONNECTED` | 10.0.0 정식 service API로 대체 | zlink_actor_lifecycle_kind_t | `core/include/zlink/service/actor.h` |
| ENUMERATOR | `ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED` | 10.0.0 정식 service API로 대체 | zlink_actor_lifecycle_kind_t | `core/include/zlink/service/actor.h` |
| ENUMERATOR | `ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT` | 10.0.0 정식 service API로 대체 | zlink_actor_lifecycle_kind_t | `core/include/zlink/service/actor.h` |
| ENUMERATOR | `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE` | 10.0.0 정식 service API로 대체 | Mesh ready domain과 record kind | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_LIFECYCLE_READABLE` | 10.0.0 정식 service API로 대체 | Mesh ready domain과 record kind | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE` | 10.0.0 정식 service API로 대체 | Mesh ready domain과 record kind | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE` | 10.0.0 정식 service API로 대체 | Mesh ready domain과 record kind | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE` | 10.0.0 정식 service API로 대체 | Mesh ready domain과 record kind | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE` | 10.0.0 정식 service API로 대체 | Mesh ready domain과 record kind | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE` | 10.0.0 정식 service API로 대체 | Mesh ready domain과 record kind | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR` | 10.0.0 정식 service API로 대체 | zlink_mesh_owner_kind_t | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER` | 10.0.0 정식 service API로 대체 | zlink_mesh_owner_kind_t | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_DISPATCH_SUBJECT_SPOT` | 10.0.0 정식 service API로 대체 | zlink_mesh_owner_kind_t | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_DISPATCH_SUBJECT_TIMER` | 10.0.0 정식 service API로 대체 | zlink_mesh_owner_kind_t | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_KIND_ENTRY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/service/common.h` |
| ENUMERATOR | `ZLINK_SPOT_KIND_INVALID` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/service/common.h` |
| ENUMERATOR | `ZLINK_SPOT_KIND_USER` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/service/common.h` |
| ENUMERATOR | `ZLINK_SPOT_NODE_MODE_ALL` | 제거 | MeshNode는 하나의 ROUTER network plane만 사용 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_NODE_MODE_PUBSUB` | 제거 | MeshNode는 하나의 ROUTER network plane만 사용 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_NODE_MODE_ROUTED` | 제거 | MeshNode는 하나의 ROUTER network plane만 사용 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX` | 제거 | PUB/SUB plane 또는 Core dispatch worker option을 제거 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN` | 제거 | PUB/SUB plane 또는 Core dispatch worker option을 제거 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_NODE_OPT_PUBSUB_HWM` | 제거 | PUB/SUB plane 또는 Core dispatch worker option을 제거 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE` | 제거 | PUB/SUB plane 또는 Core dispatch worker option을 제거 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_NODE_OPT_ROUTER_HWM` | 10.0.0 정식 service API로 대체 | 대응하는 zlink_mesh_node_option_t 값 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE` | 10.0.0 정식 service API로 대체 | 대응하는 zlink_mesh_node_option_t 값 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_NODE_SOCKET_OWNER_ANY` | 제거 | 제거한 internal socket query 전용 값 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_NODE_SOCKET_OWNER_NODE` | 제거 | 제거한 internal socket query 전용 값 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT` | 제거 | 제거한 internal socket query 전용 값 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_NODE_STATE_CONNECTING` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_state_t | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_NODE_STATE_ERROR` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_state_t | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_NODE_STATE_IDLE` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_state_t | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_NODE_STATE_PARTIAL_READY` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_state_t | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_NODE_STATE_READY` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_state_t | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS` | 제거 | request operation의 timeout 인자가 같은 책임을 소유 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_PEER_KIND_ROUTER_CHANNEL` | 제거 | 두 peer kind를 하나의 Mesh peer 계약으로 통합 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_PEER_KIND_SPOT_MESH` | 제거 | 두 peer kind를 하나의 Mesh peer 계약으로 통합 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_PEER_SOURCE_DISCOVERY` | 10.0.0 정식 service API로 대체 | zlink_mesh_peer_source_t | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_PEER_SOURCE_MANUAL` | 10.0.0 정식 service API로 대체 | zlink_mesh_peer_source_t | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_PEER_SOURCE_MIXED` | 10.0.0 정식 service API로 대체 | zlink_mesh_peer_source_t | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_PEER_STATE_CONFIGURED` | 10.0.0 정식 service API로 대체 | zlink_mesh_peer_state_t | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_PEER_STATE_CONNECTED` | 10.0.0 정식 service API로 대체 | zlink_mesh_peer_state_t | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_PEER_STATE_CONNECTING` | 10.0.0 정식 service API로 대체 | zlink_mesh_peer_state_t | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_ROLE_PUB` | 제거 | remote Spot PUB/SUB role을 제거 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SPOT_ROLE_SUB` | 제거 | remote Spot PUB/SUB role을 제거 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_STREAM_OPT_NOTIFY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SUBMIT_BACKPRESSURED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_SUBMIT_INTERNAL_ERROR` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_SUBMIT_INVALID_ARGUMENT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_SUBMIT_INVALID_HANDLE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_SUBMIT_INVALID_STATE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_SUBMIT_NOT_ADMITTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_SUBMIT_NOT_CONNECTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_SUBMIT_NOT_FOUND` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_SUBMIT_NOT_SUPPORTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_SUBMIT_OK` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_SUBMIT_OUT_OF_MEMORY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_SUBMIT_RETRY_LOCAL_FAILURE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SUBMIT_RETRY_OFF` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_SUBMIT_SEQ_EXHAUSTED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_SUBMIT_TERMINATED` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_SUBMIT_THREAD_VIOLATION` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_errno.h` |
| ENUMERATOR | `ZLINK_SUB_OPT_TOPICS_COUNT` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_THREAD_AFFINITY_CPU_ADD` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_THREAD_AFFINITY_CPU_REMOVE` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_THREAD_NAME_PREFIX` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_THREAD_PRIORITY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |
| ENUMERATOR | `ZLINK_THREAD_SCHED_POLICY` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink_enum.h` |

## 7. 공개 struct field

| Kind | Identifier | 분류 | 10.0.0 처리 또는 근거 | 현재 checkout 위치 |
|---|---|---|---|---|
| FIELD | `zlink_actor_join_entry_spot_result_t.actor` | 10.0.0 정식 service API로 대체 | operation completion record 계약 | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_entry_spot_result_t.flags` | 10.0.0 정식 service API로 대체 | operation completion record 계약 | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_entry_spot_result_t.join_epoch` | 10.0.0 정식 service API로 대체 | operation completion record 계약 | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_entry_spot_result_t.join_result_code` | 10.0.0 정식 service API로 대체 | operation completion record 계약 | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_entry_spot_result_t.joined_spot_rid` | 10.0.0 정식 service API로 대체 | operation completion record 계약 | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_entry_spot_result_t.result` | 10.0.0 정식 service API로 대체 | operation completion record 계약 | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_entry_spot_result_t.target_node_rid` | 10.0.0 정식 service API로 대체 | operation completion record 계약 | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_info_t.flags` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t와 reply token | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_info_t.join_epoch` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t와 reply token | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_info_t.request` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t와 reply token | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_info_t.source_actor` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t와 reply token | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_info_t.source_node_rid` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t와 reply token | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_info_t.source_spot_rid` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t와 reply token | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_info_t.target_actor` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t와 reply token | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_info_t.target_node_rid` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t와 reply token | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_info_t.target_spot_rid` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t와 reply token | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_result_t.actor` | 10.0.0 정식 service API로 대체 | operation completion record 계약 | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_result_t.flags` | 10.0.0 정식 service API로 대체 | operation completion record 계약 | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_result_t.join_epoch` | 10.0.0 정식 service API로 대체 | operation completion record 계약 | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_result_t.join_result_code` | 10.0.0 정식 service API로 대체 | operation completion record 계약 | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_result_t.joined_spot_rid` | 10.0.0 정식 service API로 대체 | operation completion record 계약 | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_join_result_t.result` | 10.0.0 정식 service API로 대체 | operation completion record 계약 | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_lookup_result_t.actor` | 10.0.0 정식 service API로 대체 | completion의 zlink_actor_location_t data | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_lookup_result_t.flags` | 10.0.0 정식 service API로 대체 | completion의 zlink_actor_location_t data | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_lookup_result_t.result` | 10.0.0 정식 service API로 대체 | completion의 zlink_actor_location_t data | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_recv_info_t.actor` | 10.0.0 정식 service API로 대체 | zlink_mesh_receive_record_t | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_recv_info_t.flags` | 10.0.0 정식 service API로 대체 | zlink_mesh_receive_record_t | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_recv_info_t.request_id` | 10.0.0 정식 service API로 대체 | zlink_mesh_receive_record_t | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_recv_info_t.source_node_rid` | 10.0.0 정식 service API로 대체 | zlink_mesh_receive_record_t | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_recv_info_t.source_session_rid` | 10.0.0 정식 service API로 대체 | zlink_mesh_receive_record_t | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_ref_t.actor_id` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_ref_t.generation` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_ref_t.node_rid` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_route_t.actor` | 10.0.0 정식 service API로 대체 | zlink_actor_location_t | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_route_t.current_spot_kind` | 10.0.0 정식 service API로 대체 | zlink_actor_location_t | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_actor_route_t.current_spot_rid` | 10.0.0 정식 service API로 대체 | zlink_actor_location_t | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_monitor_event_t.event` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_event_t.local_addr` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_event_t.remote_addr` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_event_t.routing_id` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_event_t.value` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_applied_rcvhwm` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_applied_sndhwm` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_connection_bucket_count` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_connection_bucket_enabled` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_connection_bucket_hwm_4k` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_connection_bucket_hysteresis_retained` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_connection_bucket_index` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_deferred_rcvhwm` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_deferred_sndhwm` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_effective_message_bytes` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_effective_rcvbuf` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_effective_sndbuf` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_enabled` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_last_recalc_ms` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_last_recalc_reason` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_policy_class` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_profile` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_role` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_send_blocked_ratio_ppm` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_size_cap` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_socket_message_slots` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.auto_hwm_unit_budget_bytes` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.detail_flags` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.rcv_pending_msgs` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.snd_pending_msgs` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.source_kind` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_monitor_status_t.state_flags` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_msg_t._` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/message/api.h` |
| FIELD | `zlink_poller_event_t.events` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_poller_event_t.fd` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_poller_event_t.socket` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_poller_event_t.source_kind` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_poller_event_t.timer` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_poller_event_t.user_data` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_pollitem_t.events` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_pollitem_t.fd` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_pollitem_t.revents` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_pollitem_t.socket` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_routing_id_t.data` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/message/api.h` |
| FIELD | `zlink_routing_id_t.size` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/message/api.h` |
| FIELD | `zlink_socket_monitor_open_options_t.events` | 10.0.0 계약에 유지 | 같은 공개 type identifier를 유지 | `core/include/zlink/eventing/api.h` |
| FIELD | `zlink_spot_actor_lifecycle_event_t.info` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_spot_actor_lifecycle_event_t.kind` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_spot_actor_lifecycle_info_t.current_actor` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_spot_actor_lifecycle_info_t.current_spot_rid` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_spot_actor_lifecycle_info_t.flags` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_spot_actor_lifecycle_info_t.join_epoch` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_spot_actor_lifecycle_info_t.previous_actor` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_spot_actor_lifecycle_info_t.previous_spot_rid` | 10.0.0 정식 service API로 대체 | zlink_actor_control_record_t | `core/include/zlink/service/actor.h` |
| FIELD | `zlink_spot_dispatch_info_t.event` | 10.0.0 정식 service API로 대체 | zlink_mesh_ready_record_t | `core/include/zlink/socket/api.h` |
| FIELD | `zlink_spot_dispatch_info_t.subject` | 10.0.0 정식 service API로 대체 | zlink_mesh_ready_record_t | `core/include/zlink/socket/api.h` |
| FIELD | `zlink_spot_dispatch_info_t.subject_kind` | 10.0.0 정식 service API로 대체 | zlink_mesh_ready_record_t | `core/include/zlink/socket/api.h` |
| FIELD | `zlink_spot_node_actor_entry_t.actor` | 제거 | 제거한 MeshNode Actor inventory query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_actor_entry_t.current_spot_kind` | 제거 | 제거한 MeshNode Actor inventory query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_actor_entry_t.current_spot_rid` | 제거 | 제거한 MeshNode Actor inventory query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_actor_entry_t.last_changed_ms` | 제거 | 제거한 MeshNode Actor inventory query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_actor_entry_t.pending_message_count` | 제거 | 제거한 MeshNode Actor inventory query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_actor_entry_t.route_synced` | 제거 | 제거한 MeshNode Actor inventory query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_options_t.mode` | 제거 | mode 선택을 제거하고 MeshNode는 하나의 network plane만 사용 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_peer_entry_t.channel_name` | 10.0.0 정식 service API로 대체 | zlink_mesh_peer_entry_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_peer_entry_t.connected_since_ms` | 10.0.0 정식 service API로 대체 | zlink_mesh_peer_entry_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_peer_entry_t.kind` | 제거 | peer kind 구분을 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_peer_entry_t.last_changed_ms` | 10.0.0 정식 service API로 대체 | zlink_mesh_peer_entry_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_peer_entry_t.local_endpoint` | 10.0.0 정식 service API로 대체 | zlink_mesh_peer_entry_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_peer_entry_t.peer_endpoint` | 10.0.0 정식 service API로 대체 | zlink_mesh_peer_entry_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_peer_entry_t.source` | 10.0.0 정식 service API로 대체 | zlink_mesh_peer_entry_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_peer_entry_t.state` | 10.0.0 정식 service API로 대체 | zlink_mesh_peer_entry_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_peer_entry_t.weight` | 10.0.0 정식 service API로 대체 | zlink_mesh_peer_entry_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_peer_filter_t.peer_endpoint` | 10.0.0 정식 service API로 대체 | MeshNode peer query 입력 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_peer_filter_t.source` | 10.0.0 정식 service API로 대체 | MeshNode peer query 입력 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_peer_filter_t.state` | 10.0.0 정식 service API로 대체 | MeshNode peer query 입력 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_socket_entry_t.auto_hwm_visible` | 제거 | 제거한 internal socket query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_socket_entry_t.monitor_status` | 제거 | 제거한 internal socket query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_socket_entry_t.owner` | 제거 | 제거한 internal socket query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_socket_entry_t.owner_id` | 제거 | 제거한 internal socket query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_socket_entry_t.owner_name` | 제거 | 제거한 internal socket query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_socket_entry_t.socket_name` | 제거 | 제거한 internal socket query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_socket_entry_t.socket_type` | 제거 | 제거한 internal socket query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_socket_filter_t.owner` | 제거 | 제거한 internal socket query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_socket_filter_t.socket_name` | 제거 | 제거한 internal socket query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_socket_filter_t.socket_type` | 제거 | 제거한 internal socket query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_spot_entry_t.dispatch_handler_attached` | 제거 | 제거한 MeshNode Spot inventory query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_spot_entry_t.joined_actor_count` | 제거 | 제거한 MeshNode Spot inventory query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_spot_entry_t.last_changed_ms` | 제거 | 제거한 MeshNode Spot inventory query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_spot_entry_t.pending_actor_join_count` | 제거 | 제거한 MeshNode Spot inventory query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_spot_entry_t.route_synced` | 제거 | 제거한 MeshNode Spot inventory query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_spot_entry_t.spot_kind` | 제거 | 제거한 MeshNode Spot inventory query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_spot_entry_t.spot_rid` | 제거 | 제거한 MeshNode Spot inventory query 전용 field | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_status_t.active_peer_count` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_status_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_status_t.channel_name` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_status_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_status_t.configured_peer_count` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_status_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_status_t.connected_peer_count` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_status_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_status_t.disconnected_routed_target_count` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_status_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_status_t.disconnected_sub_target_count` | 제거 | remote subscription 상태를 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_status_t.last_changed_ms` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_status_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_status_t.last_error` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_status_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_status_t.local_endpoint` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_status_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_status_t.node_routing_id` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_status_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_status_t.ready_subject_count` | 제거 | remote subscription 상태를 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_status_t.state` | 10.0.0 정식 service API로 대체 | zlink_mesh_node_status_t | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_status_t.subject_count` | 제거 | remote subscription 상태를 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_subject_entry_t.active_peer_count` | 제거 | remote subscription inventory를 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_subject_entry_t.last_changed_ms` | 제거 | remote subscription inventory를 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_subject_entry_t.ready_peer_count` | 제거 | remote subscription inventory를 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_subject_entry_t.role` | 제거 | remote subscription inventory를 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_subject_entry_t.subject` | 제거 | remote subscription inventory를 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_subject_entry_t.subject_kind` | 제거 | remote subscription inventory를 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_subject_filter_t.role` | 제거 | remote subscription inventory를 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_subject_filter_t.subject` | 제거 | remote subscription inventory를 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_node_subject_filter_t.subject_kind` | 제거 | remote subscription inventory를 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_route_bridge_endpoint_options_t.capabilities` | 제거 | bridge 계약을 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_route_bridge_endpoint_options_t.inbound_relay_policy` | 제거 | bridge 계약을 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_route_bridge_endpoint_options_t.struct_size` | 제거 | bridge 계약을 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_route_bridge_options_t.default_request_timeout_ms` | 제거 | bridge 계약을 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_route_bridge_options_t.error_reply_policy` | 제거 | bridge 계약을 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_route_bridge_options_t.receive_mode` | 제거 | bridge 계약을 제거 | `core/include/zlink/service/spot.h` |
| FIELD | `zlink_spot_route_bridge_options_t.struct_size` | 제거 | bridge 계약을 제거 | `core/include/zlink/service/spot.h` |

## 8. public macro

| Kind | Identifier | 분류 | 10.0.0 처리 또는 근거 | 현재 checkout 위치 |
|---|---|---|---|---|
| MACRO | `ZLINK_ACTOR_ID_MAX` | 10.0.0 계약에 유지 | 같은 identifier를 유지하며 목표 값은 Actor spec에서 고정 | `core/include/zlink/service/actor.h` |
| MACRO | `ZLINK_ACTOR_JOIN_INFO_REMOTE` | 제거 | 현재 join-info flag를 제거하고 목표 record에 별도 data를 정의 | `core/include/zlink/service/actor.h` |
| MACRO | `ZLINK_ACTOR_RECV_INFO_NO_BIND` | 제거 | 현재 recv-info flag를 제거하고 목표 record에 별도 data를 정의 | `core/include/zlink/service/actor.h` |
| MACRO | `ZLINK_CHANNEL_ROLE_DEALER` | 제거 | channel role alias family를 제거 | `core/include/zlink_enum.h` |
| MACRO | `ZLINK_CHANNEL_ROLE_INVALID` | 제거 | channel role alias family를 제거 | `core/include/zlink_enum.h` |
| MACRO | `ZLINK_CHANNEL_ROLE_PUB` | 제거 | channel role alias family를 제거 | `core/include/zlink_enum.h` |
| MACRO | `ZLINK_CHANNEL_ROLE_ROUTER` | 제거 | channel role alias family를 제거 | `core/include/zlink_enum.h` |
| MACRO | `ZLINK_CHANNEL_ROLE_SPOT` | 제거 | channel role alias family를 제거 | `core/include/zlink_enum.h` |
| MACRO | `ZLINK_CHANNEL_ROLE_SUB` | 제거 | channel role alias family를 제거 | `core/include/zlink_enum.h` |
| MACRO | `ZLINK_CTX_AUTO_HWM_ENABLE_DFLT` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/core/api.h` |
| MACRO | `ZLINK_CTX_AUTO_HWM_MSG_UNIT_BYTES_DFLT` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/core/api.h` |
| MACRO | `ZLINK_CTX_AUTO_HWM_PROFILE_DFLT` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/core/api.h` |
| MACRO | `ZLINK_CTX_AUTO_HWM_RECALC_DEBOUNCE_MS_DFLT` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/core/api.h` |
| MACRO | `ZLINK_DISCONNECT_CTX_TERM` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/socket/api.h` |
| MACRO | `ZLINK_DISCONNECT_HANDSHAKE_FAILED` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/socket/api.h` |
| MACRO | `ZLINK_DISCONNECT_TRANSPORT_ERROR` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/socket/api.h` |
| MACRO | `ZLINK_DISCONNECT_UNKNOWN` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/socket/api.h` |
| MACRO | `ZLINK_DONTWAIT` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/socket/api.h` |
| MACRO | `ZLINK_HAUSNUMERO` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink_errno.h` |
| MACRO | `ZLINK_HAVE_POLLER` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/eventing/api.h` |
| MACRO | `ZLINK_IO_THREADS_DFLT` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/core/api.h` |
| MACRO | `ZLINK_MAKE_VERSION` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/common.h` |
| MACRO | `ZLINK_MAX_SOCKETS_DFLT` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/core/api.h` |
| MACRO | `ZLINK_MSG_METADATA_KEY_USER_MIN` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/message/api.h` |
| MACRO | `ZLINK_MSG_METADATA_VALUE_MAX` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/message/api.h` |
| MACRO | `ZLINK_NULL` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/socket/api.h` |
| MACRO | `ZLINK_PLAIN` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/socket/api.h` |
| MACRO | `ZLINK_SPOT_ROUTE_BRIDGE_CAP_SPOT_ROUTE` | 제거 | bridge capability를 제거 | `core/include/zlink/service/spot.h` |
| MACRO | `ZLINK_SPOT_ROUTE_BRIDGE_ROUTE_ONLY` | 제거 | bridge capability를 제거 | `core/include/zlink/service/spot.h` |
| MACRO | `ZLINK_THREAD_PRIORITY_DFLT` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/core/api.h` |
| MACRO | `ZLINK_THREAD_SCHED_POLICY_DFLT` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/core/api.h` |
| MACRO | `ZLINK_VERSION` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/common.h` |
| MACRO | `ZLINK_VERSION_MAJOR` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/common.h` |
| MACRO | `ZLINK_VERSION_MINOR` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/common.h` |
| MACRO | `ZLINK_VERSION_PATCH` | 10.0.0 계약에 유지 | 같은 public macro identifier를 유지 | `core/include/zlink/common.h` |

## 9. 동적 `zlink_*` export

| Kind | Identifier | 분류 | 10.0.0 처리 또는 근거 | 현재 checkout 위치 |
|---|---|---|---|---|
| EXPORT | `zlink_actor_replay_readable_for_spot` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_atomic_counter_dec` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_atomic_counter_destroy` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_atomic_counter_inc` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_atomic_counter_new` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_atomic_counter_set` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_atomic_counter_value` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_bind` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_close` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_connect` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_ctx_auto_hwm_recalculate` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_ctx_get` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_ctx_new` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_ctx_set` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_ctx_set_data` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_ctx_shutdown` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_ctx_term` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_dealer_recv_part` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_dealer_reply_part` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_dealer_request_part` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_disconnect` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_disconnect_rid` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_errno` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_get_dealer_option` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_get_option` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_get_pub_option` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_get_router_option` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_get_routing_id` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_get_spot_node_option` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_get_mesh_node_option | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_get_spot_option` | 제거 | FUNC 분류와 동일: Spot option bag을 제거 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_get_stream_option` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_get_sub_option` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_has` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_monitor_close` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_monitor_ignore_handler` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_monitor_status` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_msg_adopt` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| EXPORT | `zlink_msg_close` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| EXPORT | `zlink_msg_copy` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| EXPORT | `zlink_msg_data` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| EXPORT | `zlink_msg_gets` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| EXPORT | `zlink_msg_init` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| EXPORT | `zlink_msg_init_data` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| EXPORT | `zlink_msg_init_size` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| EXPORT | `zlink_msg_move` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| EXPORT | `zlink_msg_refcnt` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| EXPORT | `zlink_msg_size` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| EXPORT | `zlink_multipart_close` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/message/api.h` |
| EXPORT | `zlink_poll` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_poller_add` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_poller_add_fd` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_poller_add_timer` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_poller_destroy` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_poller_modify` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_poller_modify_fd` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_poller_new` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_poller_remove` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_poller_remove_fd` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_poller_remove_timer` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_poller_size` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_poller_wait` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_proxy` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_proxy_steerable` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_publish_part` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_recv_handler` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_recv_part` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_remote_actor_get_ref` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_actor_lookup_remote | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_router_enable_request_reply_receive` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_router_enable_spot_receive` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_router_recv_part` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_router_reply_part` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_router_reply_spot_part` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_reply | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_router_request_part` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_router_request_spot_part` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: MeshNode service request ingress 계약 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_router_send_spot_part` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: MeshNode service send ingress 계약 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_send_part` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_send_part_rid` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_send_ready_handler` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_service_publish_internal` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_service_subscribe_recv_internal` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_set_dealer_option` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_set_option` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_set_pub_option` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_set_router_option` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_set_routing_id` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_set_spot_node_option` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_set_mesh_node_option | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_set_spot_option` | 제거 | FUNC 분류와 동일: Spot option bag을 제거 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_set_stream_option` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_set_sub_option` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_set_subscription` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_set_tls_client` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_set_tls_server` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_sleep` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_socket` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_socket_get_channel_name` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_socket_monitor_handler` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_socket_monitor_open` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_socket_monitor_recv` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_socket_publish_internal` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_socket_recv_internal` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_socket_request_reply_cleanup` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_socket_request_reply_get_default_timeout` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_socket_request_reply_set_default_timeout` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_socket_send_internal` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_socket_send_rid_internal` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_socket_set_channel_name` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_socket_subscribe_recv_internal` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_socket_xpub_recv_internal` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_spot_actor_join_recv` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: Spot control claim/batch 계약 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_actor_join_reply` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_actor_join_reply | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_actors` | 제거 | FUNC 분류와 동일: Spot-local Actor inventory query를 제거 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_destroy` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_dispatch_event_handler` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_set_ready_handler | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_spot_drain_channel_reply` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: infrastructure completion claim/batch 계약 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_drain_reply` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: infrastructure completion claim/batch 계약 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_drain_routed_router_ingress` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_spot_has_joined_or_pending_actor` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_spot_install_routed_router_dispatch` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_spot_new` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_actor_bind_remote_session` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_stream_session_bind_actor | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_actor_close_bound_session` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_actor_close_bound_session | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_actor_destroy` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_actor_destroy | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_actor_forward_bound_session_part` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_stream_session_send_to_actor | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_actor_join_entry_spot` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_actor_join_entry_spot | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_actor_join_spot` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_actor_join_spot | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_actor_leave_spot` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_actor_leave_spot | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_actor_lookup` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_actor_lookup | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_actor_new` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_actor_new | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_actor_new_with_request` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_actor_new | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_actor_recv_part` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_claim_recv_batch | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_actor_reply_no_bind` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_reply | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_actor_send_bound_session_msg` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_actor_send_bound_session | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_actors` | 제거 | FUNC 분류와 동일: MeshNode 전체 Actor inventory query 제거 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_connect_peer` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_connect_peer | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_connect_peer_rid` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_connect_peer | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_destroy` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_destroy | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_disconnect_peer` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_remove_peer_connection | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_disconnect_peer_rid` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_disconnect_peer | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_entry_spot` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_entry_spot | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_has_any_actor` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_spot_node_internal_sockets` | 제거 | FUNC 분류와 동일: 내부 socket query 제거 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_new` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_new | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_peers` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_peers | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_publisher_close` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_publisher_destroy | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_publisher_new` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_publisher_new | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_publisher_publish` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_publisher_publish | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_request_to_actor` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_request_to_actor | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_send_to_actor` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_send_to_actor | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_set_pub_bind` | 제거 | FUNC 분류와 동일: MeshNode는 PUB/SUB network plane을 만들지 않음 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_set_pub_routing_id` | 제거 | FUNC 분류와 동일: MeshNode는 PUB network identity를 갖지 않음 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_set_router_bind` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_set_bind | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_set_sub_routing_id` | 제거 | FUNC 분류와 동일: MeshNode는 SUB network identity를 갖지 않음 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_spot_get_or_new` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_spot_get_or_new | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_spot_lookup` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_spot_lookup | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_spots` | 제거 | FUNC 분류와 동일: MeshNode 전체 Spot inventory query 제거 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_status` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_node_status | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_node_subjects` | 제거 | FUNC 분류와 동일: remote subscription inventory를 공개하지 않음 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_notify_dispatch_event` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_spot_notify_dispatch_info` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_spot_process_routed_router` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_spot_publish_part` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_spot_publish | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_spot_recv_actor_lifecycle` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: Spot control claim/batch 계약 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_recv_actor_lifecycle_with_request` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: Spot control claim/batch 계약 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_recv_part` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_claim_recv_batch | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_spot_recv_subscription_event` | 제거 | FUNC 분류와 동일: remote subscription control event를 공개하지 않음 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_spot_reply_router_part` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_reply | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_spot_reply_spot_part` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_reply | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_spot_request_channel_part` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_spot_request_to_channel | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_spot_request_reply_cleanup_router` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_spot_request_reply_cleanup_spot` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_spot_request_reply_get_default_timeout` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_spot_request_reply_set_default_timeout` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_spot_request_router_part` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: MeshNode service send/request 계약 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_spot_request_spot_part` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_spot_request_to_spot | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_spot_route_bridge_attach_router_channel` | 제거 | FUNC 분류와 동일: bridge endpoint 연결을 공개 계약에서 제거 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_route_bridge_close` | 제거 | FUNC 분류와 동일: bridge handle을 공개 계약에서 제거 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_route_bridge_drain` | 제거 | FUNC 분류와 동일: bridge drain을 공개 계약에서 제거 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_route_bridge_handle_router_received` | 제거 | FUNC 분류와 동일: bridge ingress를 공개 계약에서 제거 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_route_bridge_new` | 제거 | FUNC 분류와 동일: bridge handle을 공개 계약에서 제거 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_route_bridge_request` | 제거 | FUNC 분류와 동일: bridge request를 공개 계약에서 제거 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_route_bridge_send` | 제거 | FUNC 분류와 동일: bridge 전송을 공개 계약에서 제거 | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_spot_send_channel_part` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_spot_send_to_channel | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_spot_send_spot_part` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_spot_send_to_spot | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_spot_subscribe_part` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_mesh_claim_recv_batch | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_spot_timer_new` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_spot_try_process_routed_router_parts` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_stopwatch_intermediate` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_stopwatch_start` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_stopwatch_stop` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_stream_attach_raw` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_stream_bind_actor` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_stream_session_bind_actor | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_stream_bound_actors` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_stream_session_bindings | `core/include/zlink/service/spot.h` |
| EXPORT | `zlink_stream_detach` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_stream_packet_handler` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_stream_send_bound_actor_part` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_stream_session_send_to_actor | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_stream_unbind_actor` | 10.0.0 정식 service API로 대체 | FUNC 분류와 동일: zlink_stream_session_unbind_actor | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_strerror` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_subscribe_part` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_subscription_at` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_test_set_submit_retry_fault` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_thread_join` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_thread_start` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_timer_cleanup_spot` | header 없는 internal export 정리 | public API로 승격하지 않고 export table에서 제거 | `core/build/lib/libzlink.so` |
| EXPORT | `zlink_timer_destroy` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_timer_handler` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_timer_new` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_timer_recv` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_timer_start` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_timer_stop` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/eventing/api.h` |
| EXPORT | `zlink_unbind` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_unset_subscription` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |
| EXPORT | `zlink_version` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/core/api.h` |
| EXPORT | `zlink_xpub_recv_part` | 10.0.0 계약에 유지 | FUNC 분류와 동일: 같은 공개 identifier를 유지 | `core/include/zlink/socket/api.h` |

## 10. 중복·누락 검증

다음 검증은 표의 `Kind`와 identifier를 읽어 현재 checkout에서 다시 추출한 집합과 비교한다. 성공 조건은
kind별 중복 0개, source 누락 0개, 문서 초과 0개, 공개 header 함수의 export 누락 0개,
header 없는 export 30개 전부가 `header 없는 internal export 정리`로
분류되는 것이다.

```bash
cd /home/hep7/project/kairos/zlink
python3 - <<'PY'
import hashlib, pathlib, re, subprocess

headers = [
    'core/include/zlink.h',
    'core/include/zlink/common.h',
    'core/include/zlink/core/api.h',
    'core/include/zlink/message/api.h',
    'core/include/zlink/service/actor.h',
    'core/include/zlink/socket/api.h',
    'core/include/zlink/eventing/api.h',
    'core/include/zlink/service/spot.h',
    'core/include/zlink/service/common.h',
    'core/include/zlink_enum.h',
    'core/include/zlink_errno.h',
]
texts = {name: pathlib.Path(name).read_text() for name in headers}
clean = lambda text: re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)

func = set()
enum_type = set()
enumerator = set()
struct_type = set()
field = set()
other_type = set()
for raw in texts.values():
    text = clean(raw)
    for declaration in re.findall(r'ZLINK_EXPORT\s+([^;]+);', text, re.S):
        match = re.search(r'\b(zlink_[A-Za-z0-9_]+)\s*\(', declaration)
        if match:
            func.add(match.group(1))
    for match in re.finditer(
        r'typedef\s+enum\s+(\w+)\s*\{(.*?)\}\s*(zlink_\w+)\s*;', text, re.S
    ):
        enum_type.add(match.group(3))
        enumerator.update(re.findall(r'\b(ZLINK_[A-Z0-9_]+)\s*(?==|,)', match.group(2)))
    for match in re.finditer(
        r'typedef\s+struct(?:\s+\w+)?\s*\{(.*?)\}\s*(zlink_\w+)\s*;', text, re.S
    ):
        body, owner = match.group(1), match.group(2)
        struct_type.add(owner)
        for declaration in body.split(';')[:-1]:
            found = re.search(
                r'([A-Za-z_]\w*)\s*(?:\[[^\]]+\])?(?:\s+__attribute__\s*\(.*\))?\s*$',
                declaration.strip(), re.S)
            if found:
                field.add(f'{owner}.{found.group(1)}')
    without_blocks = re.sub(
        r'typedef\s+(?:enum|struct)(?:\s+\w+)?\s*\{.*?\}\s*zlink_\w+\s*;',
        ' ', text, flags=re.S)
    for statement in re.findall(r'\btypedef\b.*?;', without_blocks, re.S):
        found = re.search(r'\(\s*\*?\s*(zlink_[A-Za-z0-9_]+)\s*\)\s*\(', statement)
        if not found:
            found = re.search(r'\b(zlink_[A-Za-z0-9_]+)\s*;$', statement)
        if found:
            other_type.add(found.group(1))

excluded = {
    'ZLINK_COMMON_H_INCLUDED', 'ZLINK_CORE_API_H_INCLUDED',
    'ZLINK_EVENTING_API_H_INCLUDED', 'ZLINK_MESSAGE_API_H_INCLUDED',
    'ZLINK_SERVICE_ACTOR_H_INCLUDED', 'ZLINK_SERVICE_COMMON_H_INCLUDED',
    'ZLINK_SERVICE_SPOT_H_INCLUDED', 'ZLINK_SOCKET_API_H_INCLUDED',
    'ZLINK_EXPORT', 'ZLINK_DEFINED_STDINT',
}
macro = set()
for raw in texts.values():
    macro.update(
        name for name in re.findall(r'^\s*#\s*define\s+(ZLINK_[A-Za-z0-9_]+)', raw, re.M)
        if name not in excluded
    )

nm = subprocess.check_output(
    ['nm', '-D', '--defined-only', 'core/build/lib/libzlink.so'], text=True)
export = {
    parts[2] for line in nm.splitlines()
    if len(parts := line.split()) >= 3 and parts[2].startswith('zlink_')
}
expected = {
    'FUNC': func,
    'TYPE': struct_type | other_type,
    'ENUM_TYPE': enum_type,
    'ENUMERATOR': enumerator,
    'FIELD': field,
    'MACRO': macro,
    'EXPORT': export,
}

doc = pathlib.Path('framework/doc/plan/v10.0/s1-core-public-api-inventory.ko.md').read_text()
rows = re.findall(
    r'^\| (FUNC|TYPE|ENUM_TYPE|ENUMERATOR|FIELD|MACRO|EXPORT) \| `([^`]+)` \| ([^|]+?) \|',
    doc, re.M)
documented = {kind: [] for kind in expected}
dispositions = {kind: {} for kind in expected}
allowed = {
    '10.0.0 계약에 유지',
    '10.0.0 정식 service API로 대체',
    '제거',
    'header 없는 internal export 정리',
}
for kind, name, disposition in rows:
    documented[kind].append(name)
    disposition = disposition.strip()
    assert disposition in allowed, f'unknown disposition: {kind} {name} {disposition}'
    dispositions[kind][name] = disposition

for kind, source_set in expected.items():
    values = documented[kind]
    assert len(values) == len(set(values)), f'{kind} duplicate: {len(values) - len(set(values))}'
    assert set(values) == source_set, (
        f'{kind} missing={sorted(source_set - set(values))} '
        f'extra={sorted(set(values) - source_set)}')

assert func <= export, f'header functions without export: {sorted(func - export)}'
for name in func:
    assert dispositions['EXPORT'][name] == dispositions['FUNC'][name], (
        f'function/export disposition mismatch: {name}')
internal = export - func
for name in internal:
    pattern = rf'^\| EXPORT \| `{re.escape(name)}` \| header 없는 internal export 정리 \|'
    assert re.search(pattern, doc, re.M), f'internal export disposition mismatch: {name}'
assert len(internal) == 30
canonical = []
for kind in ('FUNC', 'TYPE', 'ENUM_TYPE', 'ENUMERATOR', 'FIELD', 'MACRO'):
    canonical.extend((kind, name) for name in expected[kind])
header_hash = hashlib.sha256(
    ''.join(f'{kind}\t{name}\n' for kind, name in sorted(canonical)).encode()
).hexdigest()
export_hash = hashlib.sha256(
    ''.join(f'{name}\n' for name in sorted(export)).encode()
).hexdigest()
macro_names_hash = hashlib.sha256(
    ''.join(f'{name}\n' for name in sorted(macro)).encode()
).hexdigest()
assert header_hash == 'c28aaeff2f28c509a488d69f5f8d0f9c2a6089098cc0db94b017cf158cb9560c'
assert export_hash == 'b85b8119936e7ba1ace7b791d5985bd2ef122928b86b01ebced7280e8882effd'
assert macro_names_hash == '4dca975acc7490a1695f147253bc705e19a36fbc7f24a173f8b3cbd8fde69325'
print('PUBLIC API INVENTORY CLEAN')
print({kind: len(values) for kind, values in documented.items()})
print('header_functions_without_export', len(func - export))
print('headerless_internal_exports', len(internal))
print('header_inventory_sha256', header_hash)
print('export_names_sha256', export_hash)
print('macro_names_sha256', macro_names_hash)
PY
```

현재 checkout 검증 결과:

```text
PUBLIC API INVENTORY CLEAN
{'FUNC': 183, 'TYPE': 51, 'ENUM_TYPE': 48, 'ENUMERATOR': 311, 'FIELD': 158, 'MACRO': 35, 'EXPORT': 213}
header_functions_without_export 0
headerless_internal_exports 30
header_inventory_sha256 c28aaeff2f28c509a488d69f5f8d0f9c2a6089098cc0db94b017cf158cb9560c
export_names_sha256 b85b8119936e7ba1ace7b791d5985bd2ef122928b86b01ebced7280e8882effd
macro_names_sha256 4dca975acc7490a1695f147253bc705e19a36fbc7f24a173f8b3cbd8fde69325
```

## 11. S1 정식 spec 역방향 inventory

미해결 후보는 없다. `zlink_set_spot_option`, `zlink_get_spot_option`, `zlink_spot_option_t`,
`ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS`, `zlink_spot_actors`와 함께 다음 세 query와 그 전용 type·enum·field를
10.0.0 공개 표면에서 제거한다.

- `zlink_spot_node_internal_sockets`: 내부 socket과 mailbox 배선을 공개하지 않는다.
- `zlink_spot_node_spots`: MeshNode 전체 Spot inventory를 제공하지 않고 facade별 `zlink_spot_status()`만
  제공한다.
- `zlink_spot_node_actors`: MeshNode 전체 Actor inventory를 제공하지 않고 exact Actor lookup과 completion
  record만 제공한다.

정식 10.0.0 target은 service, monitoring, polling과 result-enum 영문/한국어 문서의 C block을 함께
역추출한다. 현재 header에 없는 target identifier도 function, type, enum type, enumerator, field와 macro로
분리해 정렬하고 hash를 고정한다. 현재 identifier의 `service 대체` 행에 exact target identifier가 있으면
그 identifier가 반드시 정식 C block에 있어야 한다. 새 기능이라 현재 identifier와 일대일 predecessor가 없는
target도 역방향 집합과 hash에 포함되므로 formal 문서에서 추가·삭제하면 validator 갱신 없이는 통과하지 않는다.

```bash
cd /home/hep7/project/kairos/zlink
python3 - <<'PY'
import hashlib, pathlib, re

headers = [
    'core/include/zlink.h', 'core/include/zlink/common.h',
    'core/include/zlink/core/api.h', 'core/include/zlink/message/api.h',
    'core/include/zlink/service/actor.h', 'core/include/zlink/socket/api.h',
    'core/include/zlink/eventing/api.h', 'core/include/zlink/service/spot.h',
    'core/include/zlink/service/common.h', 'core/include/zlink_enum.h',
    'core/include/zlink_errno.h',
]
formal_pairs = [
    ('core/doc/spec/core/service/mesh-node.ko.md', 'core/doc/spec/core/service/mesh-node.md'),
    ('core/doc/spec/core/service/spot.ko.md', 'core/doc/spec/core/service/spot.md'),
    ('core/doc/spec/core/service/actor.ko.md', 'core/doc/spec/core/service/actor.md'),
    ('core/doc/spec/core/service/dispatch.ko.md', 'core/doc/spec/core/service/dispatch.md'),
    ('core/doc/spec/core/service/stream-session.ko.md', 'core/doc/spec/core/service/stream-session.md'),
    ('core/doc/spec/core/monitoring.ko.md', 'core/doc/spec/core/monitoring.md'),
    ('core/doc/spec/core/errors.ko.md', 'core/doc/spec/core/errors.md'),
    ('core/doc/spec/core/polling.ko.md', 'core/doc/spec/core/polling.md'),
]
kinds = ('FUNC', 'TYPE', 'ENUM_TYPE', 'ENUMERATOR', 'FIELD', 'MACRO')

def c_blocks(path):
    text = pathlib.Path(path).read_text()
    fence = chr(96) * 3
    pattern = rf'^{fence}c\s*\n(.*?)^{fence}\s*$'
    return '\n'.join(re.findall(pattern, text, re.M | re.S))

def parse(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    out = {kind: set() for kind in kinds}
    for declaration in re.findall(r'ZLINK_EXPORT\s+([^;]+);', text, re.S):
        found = re.search(r'\b(zlink_[A-Za-z0-9_]+)\s*\(', declaration)
        if found:
            out['FUNC'].add(found.group(1))
    for found in re.finditer(
        r'typedef\s+enum\s+\w+\s*\{(.*?)\}\s*(zlink_\w+)\s*;', text, re.S):
        out['ENUM_TYPE'].add(found.group(2))
        out['ENUMERATOR'].update(
            re.findall(r'\b(ZLINK_[A-Z0-9_]+)\s*(?==|,)', found.group(1)))
    for found in re.finditer(r'(?<!typedef\s)enum\s*\{(.*?)\}\s*;', text, re.S):
        out['ENUMERATOR'].update(
            re.findall(r'\b(ZLINK_[A-Z0-9_]+)\s*(?==|,)', found.group(1)))
    for found in re.finditer(
        r'typedef\s+struct(?:\s+\w+)?\s*\{(.*?)\}\s*(zlink_\w+)\s*;', text, re.S):
        body, owner = found.group(1), found.group(2)
        out['TYPE'].add(owner)
        for declaration in body.split(';')[:-1]:
            field = re.search(
                r'([A-Za-z_]\w*)\s*(?:\[[^\]]+\])?\s*$', declaration.strip(), re.S)
            if field:
                out['FIELD'].add(f'{owner}.{field.group(1)}')
    without_blocks = re.sub(
        r'typedef\s+(?:enum|struct)(?:\s+\w+)?\s*\{.*?\}\s*zlink_\w+\s*;',
        ' ', text, flags=re.S)
    for statement in re.findall(r'\btypedef\b.*?;', without_blocks, re.S):
        found = re.search(r'\(\s*\*?\s*(zlink_\w+)\s*\)\s*\(', statement)
        if not found:
            found = re.search(r'\b(zlink_\w+)\s*;$', statement)
        if found:
            out['TYPE'].add(found.group(1))
    out['MACRO'].update(
        re.findall(r'^\s*#\s*define\s+(ZLINK_\w+)', text, re.M))
    return out

legacy = {kind: set() for kind in kinds}
for path in headers:
    parsed = parse(pathlib.Path(path).read_text())
    for kind in kinds:
        legacy[kind].update(parsed[kind])

formal = {kind: set() for kind in kinds}
formal_text = ''
for ko, en in formal_pairs:
    ko_c, en_c = c_blocks(ko), c_blocks(en)
    assert ko_c == en_c, f'C block ko/en mismatch: {ko} {en}'
    formal_text += '\n' + en_c
    parsed = parse(en_c)
    for kind in kinds:
        formal[kind].update(parsed[kind])

target = {kind: formal[kind] - legacy[kind] for kind in kinds}
expected = {
    'FUNC': (84, '5c7e00dec12264f03b43847a1f78feeb18569a68833bcb6a6572b7913b8e830f'),
    'TYPE': (30, '3f313c08469842df4b653866c978cae274a9d704743db6a4a42000c38d470098'),
    'ENUM_TYPE': (16, '7ff77e0d3a8882fb78e7eb29e76bd72bdaedda42c06cbc36c2037c41bcf9b14c'),
    'ENUMERATOR': (101, '0d35709d8e48a0eb774d67422574180eb559b5171e5dfa63a7a4c4a49904d831'),
    'FIELD': (202, 'de23924216822f8053a71765d54022ac7480cd423cdc58490f01f0658f6dcc02'),
    'MACRO': (12, '79ee13a8019ea363387b3508f4b01da3b17cc372312ff44eae7b6c236e7576ef'),
}
for kind in kinds:
    names = sorted(target[kind])
    digest = hashlib.sha256(''.join(f'{name}\n' for name in names).encode()).hexdigest()
    assert (len(names), digest) == expected[kind], (kind, len(names), digest)

all_target = sorted((kind, name) for kind in kinds for name in target[kind])
all_digest = hashlib.sha256(
    ''.join(f'{kind}\t{name}\n' for kind, name in all_target).encode()).hexdigest()
assert len(all_target) == 445
assert all_digest == '96c27d2d094dbce1f35082762c76a27e05370b474860924dab8bcd37a02c56aa'

doc = pathlib.Path(
    'framework/doc/plan/v10.0/s1-core-public-api-inventory.ko.md').read_text()
for name in (
    'zlink_spot_node_internal_sockets', 'zlink_spot_node_spots',
    'zlink_spot_node_actors'):
    for kind in ('FUNC', 'EXPORT'):
        pattern = rf'^\| {kind} \| `{name}` \| 제거 \|'
        assert re.search(pattern, doc, re.M), f'removal not fixed: {kind} {name}'
    assert name not in formal['FUNC'], f'removed query remains formal: {name}'

assert 'zlink_actor_join_completion_t.join_result' in formal['FIELD']
assert 'zlink_actor_join_completion_t.join_result_code' not in formal['FIELD']

replacement_cells = re.findall(
    r'^\| (?:FUNC|TYPE|ENUM_TYPE|ENUMERATOR|FIELD|MACRO|EXPORT) \| `[^`]+` '
    r'\| 10\.0\.0 정식 service API로 대체 \| ([^|]+?) \|', doc, re.M)
target_refs = {
    token for cell in replacement_cells
    for token in re.findall(r'\b(?:zlink_[A-Za-z0-9_]+|ZLINK_[A-Z0-9_]+)\b', cell)
}
formal_names = set().union(*(formal[kind] for kind in kinds))
assert target_refs <= formal_names, f'non-formal replacement target: {sorted(target_refs - formal_names)}'

print('S1 FORMAL REVERSE INVENTORY CLEAN')
print({kind: len(target[kind]) for kind in kinds})
print('formal_target_identifier_count', len(all_target))
print('formal_target_identifier_sha256', all_digest)
print('exact_replacement_target_refs', len(target_refs))
PY
```

현재 checkout 검증 결과:

```text
S1 FORMAL REVERSE INVENTORY CLEAN
{'FUNC': 84, 'TYPE': 30, 'ENUM_TYPE': 16, 'ENUMERATOR': 101, 'FIELD': 202, 'MACRO': 12}
formal_target_identifier_count 445
formal_target_identifier_sha256 96c27d2d094dbce1f35082762c76a27e05370b474860924dab8bcd37a02c56aa
exact_replacement_target_refs 54
```
