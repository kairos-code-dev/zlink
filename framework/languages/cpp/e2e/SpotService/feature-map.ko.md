# C++ SpotService E2E feature map

이 파일은 Config 2 SpotService 시나리오 중 C++ framework 공개 API로 바로 검증한 항목과,
현재 공개 표면이 없어 E2E 앱에서 내부 패킷을 직접 만들지 않기로 한 항목을 구분한다.

## 구현됨

- `SM-A1`: entry spot join으로 user spot 생성과 reply spot id를 검증한다.
- `SM-A2`: 같은 user spot에 연속 상태 변경 request를 보내 누적 상태와 순서를 검증한다.
- `SM-A3`: route client가 `target_node_rid`와 특정 `spot_rid_t`를 함께 지정해 원격 user spot으로
  직접 request를 보내고, 해당 owner node와 spot id의 reply가 오는지 검증한다.
- `SM-A4`: 같은 key가 같은 owner node와 같은 spot rid로 매핑되는지 검증한다.
- `SM-A6`: actor 없는 user spot을 생성한 뒤 public `close_spot`으로 닫아 initialize/closing
  lifecycle evidence를 검증한다.
- `SM-A7`: 같은 spot rid를 다른 spot 타입으로 다시 `get_or_create_spot`할 때
  `spot_type_mismatch`로 거부되고 기존 user spot 상태가 유지되는지 검증한다.
- `SM-A8`: actor handler가 public `run_worker`로 무거운 작업을 spot 직렬 루프 밖에
  offload하고, 같은 spot의 다른 request가 worker 완료 전에 처리되며 worker 결과가 다시 spot
  상태에 반영되는지 검증한다.
- `SM-B1`: `play-a` local actor join과 user spot dispatch를 검증한다.
- `SM-B2`: `play-b` remote actor join과 cross-node dispatch를 검증한다.
- `SM-B3`: join request의 문자열, 숫자, 배열 payload가 reply에 그대로 반영되는지 검증한다.
- `SM-B4`: `play-b`에 있는 actor로 후속 request를 보내 cross-node actor routing과 reply를
  검증한다.
- `SM-B5`: handler 없는 actor packet request가 client-visible error로 끝나고
  `HandlerMissing` dispatch error marker가 play 노드 로그에 남는지 검증한다.
- `SM-B6`: 명시적 leave는 actor leave reply와 evidence로 검증하고, 비정상 disconnect 통지는
  `SM-D5` stream disconnect 시나리오에서 선택한 actor에만 callback이 실행되는지 함께 검증한다.
- `SM-B8`: entry spot의 public `destroyActor`로 actor를 명시 파괴하고, mailbox 제거와
  post-destroy request 실패를 검증한다.
- `SM-C1`: 외부 route client가 특정 target node와 spot id로 request/send를 보내면 해당 spot이
  처리하고, handler-missing/timeout 이후 정상 request가 오염되지 않는지 검증한다.
- `SM-C2`: spot handler가 외부 channel로 request/send를 내보내고, SPOT mesh publish를 수행하는
  흐름을 검증한다.
- `SM-C3`: 한 user spot이 다른 user spot으로 public `request_to`/`send_to`를 수행하고,
  SPOT mesh publish, missing handler request 실패, slow target timeout을 함께 검증한다.
- `SM-C4`: local spot을 등록하지 않은 client node가 attached publisher client로 SPOT mesh에
  publish하고, play 노드의 구독 spot들이 이벤트를 받는지 검증한다.
- `SM-D1`: 실제 `session-a` stream gateway에 붙어 `play-a` actor로 local stream relay를 보내고,
  actor가 bound session으로 보낸 push를 client 수신과 play/session evidence로 검증한다.
- `SM-D2`: `session-a` gateway에 붙은 상태에서 preferred가 아닌 `play-b` actor로 remote stream
  relay를 보내고, remote actor push가 session gateway route를 거쳐 돌아오는지 검증한다.
- `SM-D4`: 한 stream session에 두 actor를 bind하고 `actor-id` metadata로 각각 다른 actor에
  relay/push가 전달되며, metadata 없는 request가 실패하는지 검증한다.
- `SM-D5`: stream session 종료 시 session handler가 선택한 actor에만 public
  `notify_disconnected`를 호출하고, 해당 actor의 disconnect callback만 실행되는지 검증한다.
- `SM-D6`: actor push가 bound stream session으로만 전달되고, 연결만 하고 bind하지 않은 consumer는
  같은 push를 받지 않는지 검증한다.
- `SM-D7`: stream auth 전 packet dispatch가 실패하고, 잘못된 auth request가 public error로
  끝나며, auth 성공 후 request dispatch가 정상 동작하는지 검증한다.
- `SM-D12`: `session-a`에서 join/state/push를 수행한 actor가 연결을 끊은 뒤 `session-b`로
  다시 auth/rebind해 play 노드의 기존 state snapshot과 후속 push를 이어받는지 검증한다.
- `SM-E1`: handler 없는 spot route request가 client-visible error로 끝나고, 이후 정상 spot route
  request가 같은 route channel에서 계속 성공하는지 검증한다.
- `SM-F1`: route client가 target spot id를 지정해 request/send를 보내는 public API 경로를
  검증한다.
- `SM-F2`: route mesh channel에서 target node와 target spot을 함께 지정해 cross-node spot
  request/send가 동작하는지 검증한다.
- `SM-F3`: 같은 route mesh channel에서 일반 route packet과 spot route packet이 함께 오가도 각각
  올바른 dispatcher로 분기되는지 검증한다.
- `SM-F4`: handler 없는 spot route request와 slow spot timeout이 client-visible failure로 끝나고
  후속 정상 spot route request가 복구되는지 검증한다. 존재하지 않는 target spot route 주입은 현재
  core bridge가 장기 프로세스 상태를 오염시키므로 별도 core 결함으로 분리한다.
- `SM-F5`: spot route negative 이후에도 같은 route channel의 정상 spot route와 이후 stream/crash
  시나리오가 계속 통과해 channel socket lifecycle이 유지되는지 검증한다.
- `SM-G1`: actor와 stream session이 붙은 `play-a`를 실제 SIGKILL하고, 같은 gateway에 bind된
  `play-b` actor/session은 계속 동작하는지 확인한 뒤 살아 있는 `play-b`에 재auth/rebind해
  상태를 복구한다.

## 남은 시나리오

- `SM-A5`: C++ framework에는 Stage wrapper 공개 계층이 아직 없다.
- `SM-B7`: actor lifecycle callback과 packet handler 순서를 독립적으로 단언하는 scenario marker가
  아직 없다. local/remote join 자체는 `SM-B1`/`SM-B2`가 검증한다.
- `SM-D3`: entry spot에 actor를 bind하는 공개 예제/runner 경로가 아직 없다. user spot actor bind는
  `SM-D1`/`SM-D2`가 검증한다.
- `SM-D8`: stream reconnect 중 pending request 실패와 재auth/rebind를 분리해 고정하는 stream
  client harness 단계가 필요하다. 다른 gateway 재접속은 `SM-D12`가 검증한다.
- `SM-D9`: stream inbound observer를 evidence로 노출하는 C++ public observer 표면이 아직 없다.
- `SM-D10`: stream backpressure 정책을 public contract로 고정하고 부하를 주입하는 harness가
  필요하다.
- `SM-D11`: 같은 consumer에서 stream request와 channel request를 동시에 섞는 dedicated runner가
  아직 없다.
- `SM-D13`: stream heartbeat 중단을 제어하는 public harness knob이 아직 없다.
- `SM-D14`: C++ stream E2E runner에 TLS endpoint/certificate 구성이 아직 없다.
- `SM-E2`: spot timer handler 등록과 발화 evidence를 검증하는 C++ E2E scenario가 아직 없다.
- `SM-E3`: idle timer가 public `close_spot`을 호출하는 흐름을 고정하는 scenario가 아직 없다.
- `SM-E4`: timer overrun policy별 tick 패턴을 public evidence로 확인하는 scenario가 아직 없다.
- `SM-G2`: owner 재배치는 framework 자동 기능이 아니라 앱의 key-to-routing-id remap이므로,
  scale-out 중 remap을 안정적으로 고정하는 harness가 필요하다.
- `SM-G3`: 다수 client의 동시 join/leave/request 경합을 재현하는 부하 harness가 아직 없다.
- `SM-G4`: 다수 bound session push 부하와 오배달 없음을 확인하는 부하 harness가 아직 없다.

## 남은 구현 후보

- `SM-B7`, `SM-E2`, `SM-E3`, `SM-E4`, `SM-G2`, `SM-G3`, `SM-G4`
