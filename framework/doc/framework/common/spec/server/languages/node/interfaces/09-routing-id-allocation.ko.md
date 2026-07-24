# Node.js automatic Routing ID 공개 인터페이스

[인터페이스 목차](README.ko.md) · [MeshNode](../../../../21-mesh-node.ko.md)

## 1. Builder

`ZLinkMeshNodeBuilder`의 canonical declaration과 fixed RID·prefix member는
[기초 타입과 구성](01-foundation-configuration.ko.md)이 소유한다. `ZLinkFanoutChannelBuilder`의 canonical
declaration과 같은 두 member는 [Channel과 messaging](02-channel-messaging.ko.md)이 소유한다. 이 문서는 두
builder를 다시 선언하지 않고 automatic allocation 동작만 정의한다.

Fixed RID와 automatic RID prefix는 함께 설정할 수 없다. Object role이 `none`인 manual MeshNode만 fixed RID를
사용할 수 있다. Automatic discovery 또는 object role이 `client`·`server`이면 automatic RID를 사용한다.

Prefix는 UTF-8 1..64 bytes이며 `[A-Za-z0-9._-]`만 허용한다. Runtime은
`<prefix>-<lowercase-canonical-uuid-v4>` 형식의 RID를 생성한다. UUID v4는 `8-4-4-4-12` 자리의
lowercase canonical 문자열로 표현한다. 전체 RID는 255 bytes 이하여야 하며 provider slot이나 allocation
group을 사용하지 않는다. Descriptor owner claim이 충돌하면 새 UUID로 다시 시도하지 않고 즉시
`RoutingIdConflict`로 실패한다.

Object Server의 Entry Spot ID는 같은 prefix의
`<prefix>-entry-<lowercase-canonical-uuid-v4>` 형식이며 MeshNode와 별도로 생성한 UUID v4를 사용한다.
`ZLinkMeshNodeDescriptor.entrySpotId`가 같은 lifecycle의 exact mapping을 제공한다. Global Spot ID가
active owner와 충돌하면 새 UUID로 다시 시도하지 않고 즉시 `SpotIdConflict`로 startup을 실패시킨다.
Caller가 지정한 User·Instance Spot ID가 이 예약 형식과 일치하면 Store와 factory 전에
`invalidConfiguration`으로 거부한다.

Public trace category는 `host-lifecycle`, `topology-discovery`다.
