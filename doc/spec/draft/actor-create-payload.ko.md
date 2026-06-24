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

호환성을 제거할 수 있는 릴리스라면 기존 actor 생성 함수가 payload parts를 함께 받도록 시그니처를 바꾼다.
기존 ABI를 유지해야 한다면 payload를 받는 새 함수를 추가하고, 기존 함수는 empty payload wrapper로 둔다.

예상 C API 형태:

```c
zlink_config_result_t zlink_spot_node_actor_new(
    void *node,
    const char *actor_id,
    zlink_msg_t *parts,
    size_t part_count,
    zlink_actor_ref_t *actor_out);
```

ABI 유지가 필요한 경우의 대안:

```c
zlink_config_result_t zlink_spot_node_actor_new_with_request(
    void *node,
    const char *actor_id,
    zlink_msg_t *parts,
    size_t part_count,
    zlink_actor_ref_t *actor_out);
```

최종 함수 이름과 ABI 정책은 구현 직전에 확정한다.

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
