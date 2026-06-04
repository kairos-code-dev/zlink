[English](./peer-disconnect-rid.md) | [한국어](./peer-disconnect-rid.ko.md)

# Peer Disconnect by Routing ID 내부 구조

이 문서는 `zlink_disconnect_rid()`와
`zlink_spot_node_disconnect_peer_rid()`가 내부에서 어떤 소유권 경계를
쓰는지 설명한다. Routing ID(라우팅 식별자)는 각 연결을 구분하는 고유 바이트 열이다.

## 소켓 경로

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

공개 C API는 핸들 검증만 하고, 실제 작업은
`socket_base_t::term_peer_rid()`로 넘긴다. 이 함수는 먼저 Discovery가 attach한
소켓인지 확인한다. attached 소켓은 Discovery가 수명주기를
소유하므로 수동 disconnect를 `EBUSY`로 거부한다.

ROUTER와 STREAM은 라우팅 맵을 가진 소켓이라 맵을 조회해 대상 pipe를
찾는다. STREAM은 로컬 연결 id를 4바이트 routing id로 쓰므로 입력
rid 길이를 `sizeof(uint32_t)`로 제한한다.

그 밖의 소켓은 현재 attached pipe 스냅샷에서 source routing id가 같은
pipe를 찾는다. 같은 rid가 둘 이상이면 종료 대상을 확정할 수 없으므로
`EADDRINUSE`를 반환하고 어떤 pipe도 종료하지 않는다.

## 중복 rid 정책

`options_t::rid_duplicate_policy`는 공통 소켓 옵션
`ZLINK_OPT_RID_DUPLICATE_POLICY`의 저장 위치다. 기본값은
`ZLINK_RID_DUPLICATE_REJECT`다.

ROUTER는 새 pipe가 기존 피어 identity를 대체할 수 있는지 판단할 때 이 저장된
중복 정책 상태를 그대로 읽는다.

## SpotNode 경로

SpotNode는 Discovery provider가 알려 준 node routing id와 엔드포인트 집합의
인덱스를 유지한다. `zlink_spot_node_disconnect_peer_rid()`는 target node rid로
엔드포인트 집합을 찾은 뒤, 기존 엔드포인트 disconnect 경로와 같은 control command를
엔드포인트별로 보낸다.

Spot facade에는 별도 함수가 없다. 피어 연결과 mesh 소켓은 Spot이 아니라
SpotNode runtime이 소유한다.
