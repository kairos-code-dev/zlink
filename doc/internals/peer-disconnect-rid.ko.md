[English](peer-disconnect-rid.md) | [한국어](peer-disconnect-rid.ko.md)

# Peer Disconnect by Routing ID 내부 구조

이 문서는 `zlink_disconnect_rid()`와
`zlink_spot_node_disconnect_peer_rid()`가 내부에서 어떤 소유권 경계를
사용하는지 설명한다.

## Socket 경로

```text
+---------------------+
| public C API        |
+---------------------+
| socket_base_t       |
+---------------------+
| socket-specific map |
+---------------------+
| pipe termination    |
+---------------------+
```

공개 C API는 handle 검증만 수행하고, 실제 작업은
`socket_base_t::term_peer_rid()`로 넘긴다. 이 함수는 Discovery attached
socket인지 먼저 확인한다. attached socket은 Discovery가 lifecycle을
소유하므로 수동 disconnect를 `EBUSY`로 거부한다.

ROUTER와 STREAM은 routing map을 가진 socket이므로 map lookup으로 대상 pipe를
찾는다. STREAM은 local connection id를 4바이트 routing id로 쓰기 때문에 입력
rid 길이를 `sizeof(uint32_t)`로 제한한다.

그 밖의 socket은 현재 attached pipe snapshot에서 source routing id가 같은
pipe를 찾는다. 같은 rid가 둘 이상이면 파괴적인 종료 대상을 확정할 수 없으므로
`EADDRINUSE`를 반환하고 어떤 pipe도 종료하지 않는다.

## 중복 rid 정책

`options_t::rid_duplicate_policy`는 공통 socket option
`ZLINK_OPT_RID_DUPLICATE_POLICY`의 저장 위치다. 기본값은
`ZLINK_RID_DUPLICATE_REJECT`다.

ROUTER는 기존 `ZLINK_ROUTER_OPT_HANDOVER` 경로를 이 정책과 같은 상태로
연결해 호환성을 유지한다. 새 코드에서는 공통 option을 사용한다.

## SpotNode 경로

SpotNode는 discovery provider가 알려 준 node routing id와 endpoint 집합의
인덱스를 유지한다. `zlink_spot_node_disconnect_peer_rid()`는 target node rid로
endpoint 집합을 찾고, 기존 endpoint disconnect 경로와 같은 control command를
endpoint별로 보낸다.

Spot facade에는 별도 함수가 없다. peer 연결과 mesh socket은 Spot이 아니라
SpotNode runtime이 소유한다.
