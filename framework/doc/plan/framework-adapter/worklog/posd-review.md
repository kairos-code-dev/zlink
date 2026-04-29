# Framework POSD Review

## Iteration 1

상태: `implemented`

### Red Flags

- `implemented`: send/publish public submit API가 sync/no-wait 값을 노출했다.
- `implemented`: stream connector send builder가 sync `Exec()` 경로를 통해 fire-and-forget `Task.Run` queue를 사용했다.
- `pending`: packet name과 message name 용어가 public surface와 내부 구현에서 섞여 있다.

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
- `pending`: public `WithMessageName(...)` 이름은 draft의 `WithPacketName(...)`과 아직 다르다.
