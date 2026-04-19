[English](README.md) | [한국어](README.ko.md)

[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md)

# 서비스 공통 규칙

이 문서는 서비스 관련 스펙이 함께 따르는 공통 규칙을 설명합니다. SPOT,
Registry, Discovery는 각각 다른 공개 API를 가지지만, 세 문서는 같은 주소 해석
규칙을 공유합니다. 특히 SPOT direct routed 메시징에서 `node_rid`,
`spot_rid`를 어떤 뜻으로 해석해야 하는지, 그리고 `spot_rid`만 알고 있을 때
어떤 순서로 목적지 `node_rid`를 구해야 하는지를 이 문서에서 먼저 정리합니다.

## 문서별 역할

- [spot.ko.md](spot.ko.md): SPOT publish/subscribe, channel send/request, routed
  send/request/reply 함수 계약
- [registry.ko.md](registry.ko.md): 어떤 `SpotNode`가 어떤 `spot_rid`를 현재 맡고
  있는지에 대한 최종 기준, 등록/해제, 덮어쓰기, 만료 규칙
- [discovery.ko.md](discovery.ko.md): Registry 기준 정보를 가까운 곳에 저장해 두고
  빠르게 조회하는 방법, 갱신 방법, 대규모 환경 가정

이렇게 나누는 이유는 SPOT 문서가 메시지를 보내고 받는 함수 자체에 집중하고,
주소 관리 규칙은 Registry와 Discovery 문서에서 따로 다루기 위해서입니다.

## 공통 주소 해석 규칙

서비스 문서에서 `routing_id`는 네트워크 주소(IP, 포트, endpoint 문자열)와
같은 뜻이 아닙니다. 특히 SPOT direct routed 경로에서 `node_rid`와 `spot_rid`
는 둘 다 **논리 주소**입니다.

- `SpotNode`의 routing id는 "이 SpotNode를 어떤 이름으로 부를 것인가"를
  나타냅니다.
- `Spot`의 routing id는 "이 Spot을 어떤 이름으로 부를 것인가"를 나타냅니다.
- 둘 다 `zlink_set_routing_id()`로 사용자가 직접 이름을 줄 수 있습니다.
- 이 값만 보고 특정 IP, 포트, 또는 머신 위치를 바로 알 수 있는 것은 아닙니다.

`service_name`은 Discovery가 어느 서비스 묶음을 보고 있는지 설명하는 값일
뿐만 아니라, 관리형 자동 연결과 논리 주소 조회가 적용되는 **서비스 범위**이기도
합니다.

- Discovery가 붙은 자동 연결은 같은 `service_name` 안에서만 일어납니다.
- `zlink_discovery_resolve_spot()`도 현재 Discovery가 보고 있는
  `service_name` 안에서 `spot_rid`를 해석합니다.
- 따라서 관리형 구성에서는 논리 SPOT 주소를 `(service_name, spot_rid)` 쌍으로
  이해해야 합니다.
- 같은 `spot_rid`가 서로 다른 `service_name`에 동시에 존재할 수 있습니다.

## 주소 지정 방식

SPOT 메시징에서 목적지를 정하는 방식은 크게 세 가지입니다.

- channel 호출 방식:
  호출자가 `channel_name`만 지정하면, attach된 `DEALER`가 해당 channel의
  `ROUTER(server)` 집합 중 하나에 요청을 보냅니다. `Spot` facade의
  `zlink_spot_send_channel()` / `zlink_spot_request_channel()`을 사용합니다.
- peer routed 방식:
  호출자가 `peer_rid`를 직접 지정해 같은 SPOT mesh의 특정 peer에게 보냅니다.
  `Spot` facade의 `zlink_spot_send_router()` / `zlink_spot_request_router()`를
  사용합니다.
- ROUTER 쪽 direct SPOT 주소 지정:
  호출자가 `dest_node_rid + dest_spot_rid`를 모두 알고 있고, ROUTER socket의
  `zlink_router_send_spot()` / `zlink_router_request_spot()`을 사용합니다.
  이 방식은 `Spot` facade가 아니라 저수준 ROUTER API에서만 사용할 수 있습니다.

`Spot` public surface에서는 `dest_node_rid + dest_spot_rid`를 직접 받는
direct send/request 함수가 제거되었습니다. 일반적인 서비스 호출은
channel 방식이나 peer routed 방식으로 처리하고, concrete destination 직접
지정이 정말 필요할 때만 ROUTER direct 경로를 사용합니다.

### 논리 spot_rid 조회

호출자가 `spot_rid`만 알고 있을 때, 아래 함수로 현재 Discovery의
`service_name` 범위 안에서 owner `node_rid`를 구할 수 있습니다.

```c
zlink_config_result_t zlink_discovery_resolve_spot (
  void *discovery,
  const zlink_routing_id_t *spot_rid,
  zlink_routing_id_t *owner_node_rid_out);
```

이 함수가 성공하면, 호출자는 반환된 `owner_node_rid_out`과 원래의 `spot_rid`를
묶어 ROUTER 쪽 direct 함수(`zlink_router_send_spot()`,
`zlink_router_request_spot()`)에 전달할 수 있습니다. 이 조회 결과는 현재
Discovery의 `service_name` 범위 안에서만 유효합니다. reply는 예외입니다.
reply는 새로 조회하지 않고, request를 받을 때 함께 전달된 source 주소를 그대로
사용해야 합니다.

## 수동 구성과 관리형 구성

- 수동 구성:
  Discovery나 Registry 없이 peer를 직접 연결해서 쓰는 방식입니다. peer
  routed 경로에서는 `peer_rid`를 알고 있어야 하고, ROUTER direct 경로에서는
  `dest_node_rid + dest_spot_rid`를 모두 알고 있어야 합니다.
- 관리형 구성:
  Registry가 "현재 `service_name` 안에서 어떤 `spot_rid`를 어느 `SpotNode`가
  맡고 있는가"를 관리하고, Discovery가 그 결과를 가까운 곳에서 빠르게 조회할 수
  있게 도와주는 방식입니다. 이 경우에는 `zlink_discovery_resolve_spot()`로
  `node_rid`를 구한 뒤 ROUTER direct 함수로 보낼 수 있습니다.

## 자동 연결 공통 원칙

Discovery에 붙은 서비스는 현재 Discovery의 `service_name`을 자동 연결 범위로
사용합니다. 즉 관리형 자동 연결은 다른 서비스 이름으로 넘어가지 않습니다.

- 같은 `service_name`의 peer만 자동 발견 대상이 됩니다.
- 수동 connect/disconnect와 Discovery 자동 연결은 섞지 않습니다.
- SPOT Node mesh와 raw socket family 자동 연결은 모두 이 서비스 범위를 공유합니다.
- raw socket family의 역할별 자동 연결 규칙과 DEALER 정책은
  [discovery.ko.md](discovery.ko.md)에서 정의합니다.

## 읽는 순서

1. SPOT API 계약을 보려면 [spot.ko.md](spot.ko.md)
2. `spot_rid -> owner_node_rid` 주소 관리 규칙을 보려면
   [registry.ko.md](registry.ko.md)
3. Discovery가 이 결과를 어떻게 조회하고 갱신하는지 보려면
   [discovery.ko.md](discovery.ko.md)
