# Node.js automatic Routing ID 공개 인터페이스

[인터페이스 목차](README.ko.md) · [MeshNode](../../../21-mesh-node.ko.md)

## 1. Builder

`ZLinkMeshNodeBuilder`의 canonical declaration과 fixed RID·prefix member는
[기초 타입과 구성](01-foundation-configuration.ko.md)이 소유한다. `ZLinkFanoutChannelBuilder`의 canonical
declaration과 같은 두 member는 [Channel과 messaging](02-channel-messaging.ko.md)이 소유한다. 이 문서는 두
builder를 다시 선언하지 않고 automatic allocation 동작만 정의한다.

Fixed RID와 automatic RID prefix는 함께 설정할 수 없다. Object role이 `none`인 manual MeshNode만 fixed RID를
사용할 수 있다. Automatic discovery 또는 object role이 `client`·`server`이면 automatic RID를 사용한다.

Prefix는 UTF-8 1..64 bytes이며 `[A-Za-z0-9._-]`만 허용한다. Runtime은 RFC 4122 UUID v4의 lowercase
canonical 36자 형식(`8-4-4-4-12`)을 사용해 `<prefix>-<uuid>` RID를 생성한다. 전체 RID는 255 bytes
이하여야 하며 provider slot이나 allocation group을 사용하지 않는다. Descriptor owner claim이 충돌하면 기존
record를 바꾸지 않고 첫 claim에서 즉시 `RoutingIdConflict`로 실패한다. 두 번째 UUID나 claim은 만들지 않는다.

Public trace category는 `host-lifecycle`, `topology-discovery`다.

## 2. Entry Spot ID

Object Server lifecycle의 Entry Spot ID는 Runtime이 발급한다. MeshNode와 같은 diagnostic prefix를
사용하고 MeshNode UUID와 독립적으로 생성한 RFC 4122 UUID v4의 lowercase canonical 36자
형식(`8-4-4-4-12`)으로 `<prefix>-entry-<entry-uuid>`를 만든다. Full RID는 UTF-8로 255 bytes 이하다.
Full MeshNode RID에 marker나 suffix를 이어 붙이는 방식은 금지한다. Prefix를 설정하지 않으면 같은
lifecycle의 automatic MeshNode RID에 사용한 Runtime 기본 diagnostic prefix를 사용한다.

같은 lifecycle에서는 Entry Spot ID가 바뀌지 않는다. Endpoint가 같은 replacement lifecycle도 새 MeshNode
RID와 새 Entry Spot ID를 각각 발급한다. Global Spot namespace의 active conflict가 있으면 첫 claim에서
startup을 `RoutingIdConflict`로 끝내며 두 번째 Entry UUID나 claim을 만들지 않는다.

`ZLinkMeshNodeDescriptor.entrySpotId`는 Object Server가 실제로 발급하고 기존 descriptor `newClaim`
operation에서 함께 예약한 Entry Spot ID를 그대로 게시한다. `newClaim`은 MeshNode descriptor identity와
Entry Spot global identity를 exact owner lease·lifecycle로 한 transaction에서 claim한다. 별도 Entry claim
public method는 제공하지 않는다. 어느 identity라도 active owner와 충돌하면 descriptor, Entry claim과 index를
바꾸지 않는다.

`entrySpotId`는 descriptor immutable field와 digest에 포함되며 renew나 update로 바꿀 수 없다. Descriptor
remove와 owner cleanup은 exact owner lease·lifecycle이 일치할 때만 Entry claim을 함께 해제한다. Stale cleanup은
successor descriptor나 Entry claim을 제거할 수 없다. User Spot과 Instance Spot `reserve`는 같은 global
identity claim과 충돌하는 RID를 거부하고 authority나 capacity를 일부 변경하지 않는다. Actor
placement·join과 relocation은 descriptor의 RID와 lifecycle generation을 exact mapping으로 사용하고 문자열을
parse하지 않는다. Entry Spot ID를 application이 지정하거나 별도로 claim하는 public member는 제공하지 않는다.

## 3. 검증 요구

- Automatic MeshNode RID와 Entry Spot ID가 서로 독립적인 lowercase canonical UUID v4를 사용하며 충돌 시
  첫 claim에서 즉시 실패하고 두 번째 UUID나 claim을 만들지 않는지 검증한다.
- Object Server `newClaim`의 두 identity claim이 원자적이며 충돌 시 부분 상태를 남기지 않는지 검증한다.
- User Spot과 Instance Spot `reserve`가 active Entry claim 충돌을 거부하는지 검증한다.
- Exact cleanup만 Entry claim을 해제하고 stale cleanup은 successor claim을 제거하지 못하는지 검증한다.
- Renew와 update가 immutable `entrySpotId` 또는 descriptor digest를 바꾸지 못하는지 검증한다.
