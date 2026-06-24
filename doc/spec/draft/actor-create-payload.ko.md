# Actor Create Payload API 초안

> 이 문서는 구현 전 초안이다. 현재 공개 계약이 아니며, `core/include/zlink.h` 또는
> `core/include/zlink/service/spot.h`에 반영된 정식 API 계약이 아니다.

## 목적

actor를 생성할 때 호출자가 초기화에 필요한 message payload를 함께 보낼 수 있게 한다. 이 payload는
Spot 밖 호출자가 actor 객체를 직접 만지지 않아도, Spot-owned create callback 안에서 actor 초기 상태를
설정하기 위한 입력으로 사용한다.

## 현재 상태

현재 core actor 생성 API는 actor id만 받고 actor ref를 반환한다. 생성 요청에 payload를 실을 수 없기
때문에 framework handler가 생성 직후 actor 객체에 직접 접근해 초기 상태를 넣기 쉽다. 이 접근은 Spot의
직렬 처리 경계를 약화시킨다.

## 변경 방향

actor 생성 요청은 actor id와 optional multipart message payload를 함께 받을 수 있어야 한다. core C API는
계속 actor 객체를 노출하지 않고 actor ref만 반환한다.

이번 구현에서는 기존 ABI를 유지한다. 기존 actor 생성 함수는 empty payload 요청으로 남기고, payload를
받는 새 함수를 추가한다. 이렇게 하면 기존 C 호출자는 actor id만 넘기는 계약을 그대로 쓰고, bindings와
framework는 payload가 필요한 경로에서 새 함수를 명시적으로 호출할 수 있다.

확정 C API 형태:

```c
zlink_config_result_t zlink_spot_node_actor_new_with_request(
    void *node,
    const char *actor_id,
    zlink_msg_t *parts,
    size_t part_count,
    zlink_actor_ref_t *actor_out);
```

기존 함수는 다음 의미를 유지한다.

```c
zlink_config_result_t zlink_spot_node_actor_new(
    void *node,
    const char *actor_id,
    zlink_actor_ref_t *actor_out);
```

`zlink_spot_node_actor_new()`는 payload가 없는 actor 생성 요청이다. `parts == NULL`이고
`part_count == 0`인 `zlink_spot_node_actor_new_with_request()` 호출과 같은 뜻이다.

Spot-owned create callback이 생성 payload를 받을 수 있도록 actor lifecycle receive API도 payload를
함께 꺼낼 수 있어야 한다. 기존 lifecycle receive API는 event만 꺼내는 계약으로 유지하고, payload가
필요한 runtime과 binding은 새 receive API를 사용한다.

```c
zlink_recv_result_t zlink_spot_recv_actor_lifecycle_with_request(
    void *spot,
    zlink_spot_actor_lifecycle_event_t *event_out,
    zlink_msg_t **parts_out,
    size_t *part_count_out,
    zlink_recv_flags_t flags);
```

기존 `zlink_spot_recv_actor_lifecycle()`로 actor-created lifecycle event를 받으면 payload는 core가 닫고
반환하지 않는다. payload를 읽어야 하는 runtime은 반드시
`zlink_spot_recv_actor_lifecycle_with_request()`를 호출한다.

## Payload 처리 규칙

- payload는 actor 생성과 같은 logical action에 속한다.
- payload는 Spot 또는 Entry Spot 쪽 actor-created callback에서만 해석한다.
- payload가 없으면 empty message로 처리한다.
- 생성 payload decode 실패나 callback 실패는 actor 생성 실패로 처리하고 actor ref를 외부에 반환하지 않는다.
- 이미 존재하는 actor에 대한 get-or-create 호출은 새 payload로 기존 actor를 다시 초기화하지 않는다.
- 기존 actor에 정보를 갱신해야 하면 actor request 또는 admission API를 별도로 사용한다.
- 같은 actor id로 생성 중인 요청이 겹치면 첫 생성 payload만 create callback에 전달한다.
- 나머지 get-or-create 호출은 생성 완료 후 같은 actor ref를 받는다.

## 구현 후 정식 반영 위치

구현과 회귀 테스트가 끝난 뒤에는 정식 spec 문서에 현재 구현과 공개 header에 존재하는 계약만 반영한다.
정식 문서에는 이 초안의 대안이나 구현 전 가정을 그대로 옮기지 않는다.
