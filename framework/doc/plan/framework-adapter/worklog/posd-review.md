# Framework POSD Review

## Iteration 1

상태: `implemented`

### Red Flags

- `implemented`: send/publish public submit API가 sync/no-wait 값을 노출했다.
- `implemented`: stream connector send builder가 sync `Exec()` 경로를 통해 fire-and-forget `Task.Run` queue를 사용했다.
- `verified`: public surface의 packet name override는 `WithPacketName(...)`,
  `PacketName`, `ZLinkPacketAttribute`, `ZlinkStreamPacketNameAttribute`,
  `IZlinkStreamPacketNameResolver`로 정리했다. 내부 envelope와 protocol header의
  `MessageName` 필드는 wire 호환을 위한 구현 세부로 남겼다.

### Alternatives

- 대안 1: 공통 submit runtime을 framework core에 두고 call builder가 operation만 넘긴다.
- 대안 2: 각 builder에서 직접 native socket을 호출하되 public API를 먼저 정리한다.

선택: 이번 변경은 대안 2를 적용했다. public API에서 오래된 실행 경로를 제거하는 범위를 먼저 닫고, 더 큰 submit runtime 통합은 다음 반복 항목으로 남겼다.

수정 결과:

- `IZLinkSendCall`, `IZLinkPublishCall`은 `ValueTask Async(...)`만 실행 함수로 가진다.
- stream connector core와 codec wrapper는 `Async(...)`만 실행 함수로 가진다.
- send builder의 sync `Exec()`와 그 내부 `Task.Run` queue를 제거했다.

남은 red flag:

- `pending`: framework send/publish/request submit queue는 아직 plan의 bounded pending queue + ready drain runtime으로 통합되지 않았다.
- `verified`: public builder 이름은 draft의 `WithPacketName(...)`과 일치한다.

## Iteration 2

상태: `implemented`

### Red Flags

- `verified`: stream connector public resolver와 attribute가 message-name 용어를
  노출했다.
- `verified`: handler attribute와 context가 public property로 `MessageName`을
  노출했다.
- `pending`: framework send/publish/request submit queue는 아직 plan의 bounded
  pending queue + ready drain runtime으로 통합되지 않았다.

### Alternatives

- 대안 1: 내부 wire 필드까지 모두 `PacketName`으로 rename한다.
- 대안 2: public API만 `PacketName`으로 정리하고, envelope/header 내부 필드는
  compatibility와 구현 안정성을 위해 유지한다.

선택: 대안 2를 선택했다. 사용자가 호환성은 고려하지 말라고 했지만, 내부 wire 필드명은
사용자 인터페이스 복잡도를 만들지 않는다. public surface를 먼저 정리하는 쪽이 변경
범위 대비 효과가 크다.

수정 결과:

- `ZLinkRequestAttribute`, `ZLinkSendAttribute`, `ZLinkEventAttribute`는
  `PacketName` property를 사용한다.
- `IZLinkHandlerContext`와 `ZLinkHandlerInvocation`은 `PacketName` property를
  노출한다.
- stream connector resolver와 attribute는 `ZlinkStreamPacketName*` 이름을 사용한다.

남은 red flag:

- `pending`: bounded pending queue + ready drain submit runtime.
