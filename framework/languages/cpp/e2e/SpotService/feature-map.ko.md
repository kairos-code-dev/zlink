# C++ SpotService E2E feature map

이 파일은 Config 2 SpotService 시나리오 중 C++ framework 공개 API로 바로 검증한 항목과,
현재 공개 표면이 없어 E2E 앱에서 내부 패킷을 직접 만들지 않기로 한 항목을 구분한다.

## 구현됨

- `SM-A1`: entry spot join으로 user spot 생성과 reply spot id를 검증한다.
- `SM-A2`: 같은 user spot에 연속 상태 변경 request를 보내 누적 상태와 순서를 검증한다.
- `SM-A4`: 같은 key가 같은 owner node와 같은 spot rid로 매핑되는지 검증한다.
- `SM-A6`: actor 없는 user spot을 생성한 뒤 public `close_spot`으로 닫아 initialize/closing
  lifecycle evidence를 검증한다.
- `SM-A7`: 같은 spot rid를 다른 spot 타입으로 다시 `get_or_create_spot`할 때
  `spot_type_mismatch`로 거부되고 기존 user spot 상태가 유지되는지 검증한다.
- `SM-B1`: `play-a` local actor join과 user spot dispatch를 검증한다.
- `SM-B2`: `play-b` remote actor join과 cross-node dispatch를 검증한다.
- `SM-B3`: join request의 문자열, 숫자, 배열 payload가 reply에 그대로 반영되는지 검증한다.
- `SM-B4`: `play-b`에 있는 actor로 후속 request를 보내 cross-node actor routing과 reply를
  검증한다.
- `SM-B5`: handler 없는 actor packet request가 client-visible error로 끝나고
  `HandlerMissing` dispatch error marker가 play 노드 로그에 남는지 검증한다.
- `SM-B6`: 명시적 leave request와 spot evidence를 검증한다.
- `SM-B8`: entry spot의 public `destroyActor`로 actor를 명시 파괴하고, mailbox 제거와
  post-destroy request 실패를 검증한다.
- `SM-C2`: spot handler가 외부 channel로 request/send를 내보내고, SPOT mesh publish를 수행하는
  흐름을 검증한다.
- `SM-D1`: 실제 `session-a` stream gateway에 붙어 `play-a` actor로 local stream relay를 보내고,
  actor가 bound session으로 보낸 push를 client 수신과 play/session evidence로 검증한다.
- `SM-D2`: `session-a` gateway에 붙은 상태에서 preferred가 아닌 `play-b` actor로 remote stream
  relay를 보내고, remote actor push가 session gateway route를 거쳐 돌아오는지 검증한다.

## 공개 API 대기

아래 항목은 C++ runtime 내부에는 spot route bridge 배선이 있지만, 외부 channel client가 특정
`spot_rid_t`를 공개 API로 지정하는 호출 표면이 아직 없다. 현재 `route_client_t` 공개 메서드는
`request(channel, target_node_rid, request)`와 `send(channel, target_node_rid, message)` 형태만
제공한다. 따라서 E2E 앱에서 `__zlink.spot.*` 내부 packet을 직접 조립하지 않는다.

- `SM-C1`: 외부 channel에서 특정 spot으로 request/send/publish를 보내는 방향.
- `SM-E1`: handler 없는 spot route request의 public negative path.
- `SM-F1`: client/server channel에서 target spot으로 직접 egress하는 경로.
- `SM-F2`: route mesh channel에서 target node와 target spot을 함께 지정하는 경로.
- `SM-F4`: route 없음, ingress 거부, malformed spot route packet의 public error 계약.

해당 시나리오를 구현하려면 C++ framework에도 spot target을 받는 공개 route client 호출이 필요하다.
예를 들어 `request(channel, target_node_rid, spot_rid, request)` 또는 같은 의미의 별도 메서드가
필요하다. 그 전까지는 내부 helper나 low-level relay packet으로 우회하지 않는다.

## 남은 구현 후보

- `SM-A8`: `spot_context_t::run_worker` 공개 API가 있어 worker offload 검증으로 확장할 수 있다.
- `SM-C3`: `spot_context_t::request_to`와 `send_to` 공개 API가 있어 spot-to-spot messaging으로
  확장할 수 있다.
- `SM-D4`, `SM-D5`, `SM-D6`, `SM-D7`, `SM-D12`: 실제 stream session client와 gateway 노드를 더
  확장해야 한다. 현재 C++ stream connector pending request는 framework stream response를 완료하지
  못하는 제한이 있어, `SM-D1`/`SM-D2`는 send relay와 bound-session push 수신/evidence 조합으로
  검증한다.
- `SM-G1`: harness가 play node를 kill/restart하고 재join/rebind를 검증해야 한다.
