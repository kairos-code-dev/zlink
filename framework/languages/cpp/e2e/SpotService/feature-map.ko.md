# C++ SpotService E2E feature map

이 파일은 Config 2 SpotService 시나리오 중 C++ framework 공개 API로 검증한 항목과,
현재 공개 표면이 없어 E2E 앱에서 내부 패킷을 직접 만들지 않기로 한 항목을 구분한다.
외부 C++ client는 `.NET` 기준과 같이 HTTP client와 stream connector만 사용한다. route client와
spot route 요청은 server HTTP endpoint 뒤에서 public framework API로 수행한다.

## 최신 검증

- 2026-07-16 `SM-D2`를 route mesh 없이 실행한 forward·reverse·`shuffle:20260709` 기동 순서가 모두 통과했다.
  - 로그: `logs/20260716-111123-3627029`, `logs/20260716-111548-3634712`, `logs/20260716-111550-3635089`
  - 분리된 Session·Play 역할에서 전체 actor snapshot bind, 원격 actor request, bound-session push, unbound client negative를 검증했다.
- 2026-07-07 `CMAKE_BUILD_PARALLEL_LEVEL=1 nice -n 10 cmake --build framework/languages/cpp/build-redis-vcpkg --target zlink_cpp_e2e_spot_service_client zlink_cpp_e2e_spot_service_play zlink_cpp_e2e_spot_service_gateway zlink_cpp_e2e_spot_service_multinode zlink_cpp_e2e_spot_service_session -- -j1` 통과.
  - `SM-B9`, `SM-C5`, `SM-D15`, `SM-F6` 구현을 포함한 SpotService 관련 C++ target build 검증이다.
- 2026-07-07 `E2E_START_ORDER=reverse ZLINK_CPP_E2E_BUILD_DIR=/home/hep7/project/kairos/zlink/framework/languages/cpp/build-redis-vcpkg ZLINK_CPP_E2E_SKIP_BUILD=1 CMAKE_BUILD_PARALLEL_LEVEL=1 nice -n 10 timeout 240s framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-F6` 통과.
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260707-163439-2654857`
  - RouteMesh를 끈 MultiNode SpotMesh-only scenario에서 서버 시작 순서를 reverse로 바꿔도 client readiness와 evidence polling으로 수렴함을 검증했다.
- 2026-07-03 Redis location store 전환 후 `ZLINK_CPP_E2E_BUILD_DIR=/home/hep7/project/kairos/zlink/framework/languages/cpp/build-redis-vcpkg timeout 900s framework/languages/cpp/e2e/SpotService/run_e2e.sh all` 통과.
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260703-194121-30065`
  - runner는 registry role 없이 Redis location store와 scenario별 key prefix를 사용한다.

## 구현됨

- `SM-A1`: entry spot join과 `.NET`식 lifecycle context group의 spot create로 user spot 생성과
  reply spot id를 검증한다. 독립 실행에서는 공개 `location_runtime_query_t`가 조회한 row의
  mesh, spot id와 타입, owner node, kind, owner, generation도 함께 검증한다.
- `SM-A2`: 같은 user spot에 연속 상태 변경 request를 보내 누적 상태와 순서를 검증하고,
  `.NET`식 lifecycle context group에서는 앞선 SM-A4/F1/F2 state evidence가 보존되는지 확인한다.
- `SM-A3`: route client가 `target_node_rid`와 특정 `spot_rid_t`를 함께 지정해 원격 user spot으로
  직접 request를 보내고, 해당 owner node와 spot id의 reply가 오는지 검증한다.
- `SM-A4`: 같은 key가 같은 owner node와 같은 spot rid로 매핑되는지 검증하고, `.NET`식 lifecycle
  context group에서는 같은 context spot rid가 play-a owner에 유지되는지 확인한다.
- `SM-A5`: `.NET`의 app-level `ScenarioStage` wrapper에 대응해 C++ user spot이 public spot
  request handler와 `spot_context_t::add_timer<THandler>`를 사용하고, stage request, stage timer,
  spot close lifecycle evidence를 검증한다.
- `SM-A6`: actor 없는 user spot을 생성한 뒤 public `close_spot`으로 닫아 initialize/closing
  lifecycle evidence를 검증한다.
- `SM-A7`: 같은 spot rid를 다른 spot 타입으로 다시 `get_or_create_spot`할 때
  `spot_type_mismatch`로 거부되고 기존 user spot 상태가 유지되는지 검증한다.
- `SM-A8`: `/spot/worker/start`가 public `run_worker`로 무거운 작업을 spot 직렬 루프 밖에
  offload하고, 같은 spot의 다른 request가 worker 완료 전에 처리되며 `/spot/worker/complete`가
  worker 결과 evidence를 관측하는지 검증한다.
- `SM-B1`: `play-a` local actor join과 user spot dispatch를 검증한다.
- `SM-B2`: `play-b` remote actor join과 cross-node dispatch를 검증한다.
- `SM-B3`: join request의 문자열, 숫자, 배열 payload가 reply에 그대로 반영되는지 검증한다.
- `SM-B4`: `play-b`에 있는 actor로 후속 request를 보내 cross-node actor routing과 reply를
  검증한다.
- `SM-B5`: handler 없는 actor packet request가 client-visible error로 끝나고
  `HandlerMissing` dispatch error marker가 play 노드 로그에 남는지 검증한다.
- `SM-B6`: 명시적 leave는 actor leave reply와 evidence로 검증하고, 비정상 disconnect 통지는
  `SM-D5` stream disconnect 시나리오에서 선택한 actor에만 callback이 실행되는지 함께 검증한다.
- `SM-B7`: HTTP evidence snapshot에서 `ActorCreated`, entry spot packet handler, user spot join,
  join callback, 후속 actor packet handler의 순서를 검증한다.
- `SM-B8`: stream auth로 actor를 붙인 뒤 entry spot의 public `destroyActor`로 actor를 명시 파괴하고,
  destroy evidence와 post-destroy request 실패를 검증한다.
- `SM-B9`: stream-bound entry actor가 public `actor_context_t::join_spot`으로 user spot admission을
  수행하고, 허용된 actor만 user spot에 commit되며 거부된 actor는 `ActorJoinRejected` reply와
  reject evidence로 끝나는지 검증한다.
- `SM-C1`: HTTP client가 Play role endpoint를 호출하고, server-owned route client가 특정 target
  node와 spot id로 request/send를 보내면 해당 spot이 처리하는지 검증한다. handler-missing/timeout
  이후 정상 request가 오염되지 않는지도 함께 확인한다.
- `SM-C2`: spot handler가 외부 channel로 request/send를 내보내고, SPOT mesh publish를 수행하는
  흐름을 검증한다.
- `SM-C3`: 한 user spot이 다른 user spot으로 public `request_to`/`send_to`를 수행하고,
  SPOT mesh publish, missing handler request 실패, slow target timeout을 함께 검증한다.
- `SM-C4`: local spot을 등록하지 않은 client node가 attached publisher client로 SPOT mesh에
  publish하고, play 노드의 구독 spot들이 이벤트를 받는지 검증한다.
- `SM-C5`: `play-a` spot handler가 cross-node user spot으로 request/send를 수행한 뒤 같은 spot
  handler에서 publish한 SpotMesh 이벤트가 `play-b`의 target spot subscriber evidence에 남는지
  검증한다. 두 Spot location row를 공개 조회로 먼저 관측하고, cross-node 의미 request는 재시도 없이
  한 번만 보내 수렴 직후 결과를 그대로 검증한다.
- `SM-D1`: 실제 `session-a` stream gateway에 붙어 `play-a` actor로 local stream relay를 보내고,
  actor가 bound session으로 보낸 push를 client 수신과 play/session evidence로 검증한다.
- `SM-D2`: route mesh를 등록하지 않은 `session-a` gateway와 분리된 `play-b` actor 사이에서 remote
  stream relay를 보내고, actor push가 SpotMesh의 bound-session 경로를 거쳐 반환되는지 검증한다.
- `SM-D3`: entry spot actor와 user spot actor를 각각 실제 stream session에 bind하고,
  public stream connector request/push로 entry/user spot 경로의 relay와 bound-session push를
  검증한다.
- `SM-D4`: 한 stream session에 두 actor를 bind하고 `actor-id` metadata로 각각 다른 actor에
  relay/push가 전달되며, metadata 없는 request가 실패하는지 검증한다.
- `SM-D5`: stream session 종료 시 session handler가 선택한 actor에만 public
  `notify_disconnected`를 호출하고, 해당 actor의 disconnect callback만 실행되는지 검증한다.
- `SM-D6`: actor push가 bound stream session으로만 전달되고, 연결만 하고 bind하지 않은 consumer는
  같은 push를 받지 않는지 검증한다.
- `SM-D7`: stream auth 전 packet dispatch가 실패하고, 잘못된 auth request가 public error로
  끝나며, auth 성공 후 request dispatch가 정상 동작하는지 검증한다.
- `SM-D8`: stream 연결 종료 시 pending request가 실패하고 자동 재전송되지 않으며, 새 stream
  session에서 재auth/rebind한 뒤 actor messaging이 정상 재개되는지 검증한다.
- `SM-D9`: stream inbound observer가 auth/join/state response의 kind/name/request-seq를
  관측하는지 검증한다.
- `SM-D10`: `max_received_messages`로 stream push 수신 queue를 제한하고, 느린 push callback에서도
  같은 session request와 다른 session push가 계속 정상 동작하는지 검증한다.
- `SM-D11`: 같은 client process에서 stream actor request와 일반 channel request를 동시에 보내도
  각각 stream dispatcher와 channel dispatcher에서 reply를 받는지 검증한다.
- `SM-D12`: `session-a`에서 join/state/push를 수행한 actor가 연결을 끊은 뒤 `session-b`로
  다시 auth/rebind해 play 노드의 기존 state snapshot과 후속 push를 이어받는지 검증한다.
- `SM-D13`: `.NET`과 같이 heartbeat-enabled stream이 유지되는지 확인하고, 같은 stream에서
  후속 actor request가 성공하는지 검증한다.
- `SM-D14`: public stream node TLS server 설정으로 `tls://` endpoint를 열고, stream connector가
  self-signed certificate를 strict mode에서 거부한 뒤 skip-validation mode에서 bind, relay, push를
  평문 stream과 같은 의미로 수행하는지 검증한다.
- `SM-D15`: gateway role의 HTTP endpoint가 public `actor_client_t::request_to_actor`로 actor
  handler를 호출하고, actor가 bound stream session으로 push한 notify를 client가 수신하는지 검증한다.
- `SM-E1`: handler 없는 spot route request가 client-visible error로 끝나고, 이후 정상 spot route
  request가 같은 route channel에서 계속 성공하는지 검증한다.
- `SM-E2`: user spot이 public `spot_context_t::add_timer<THandler>`로 timer를 등록하고,
  timer tick handler가 같은 spot evidence에 tick marker를 남기는지 검증한다.
- `SM-E3`: public spot create lifecycle에서 idle timer를 등록하고, timer handler가 public
  `spot_context_t::close()`로 같은 spot을 닫으며 이후 닫힌 spot request가 실패하는지 검증한다.
- `SM-E4`: public `timer_options_t`의 overrun policy별로 지연된 timer handler를 실행하고,
  `timer_tick_t`의 delivery/scheduled/skipped evidence로 skip, bounded catch-up, delayed next tick
  동작을 검증한다.
- `SM-F1`: HTTP client가 Play role endpoint를 호출하고, server-owned route client가 target spot
  id를 지정해 request/send를 보내는 public API 경로를 검증한다. `.NET`식 lifecycle context group에서는
  같은 context spot에 state request와 command를 보낸다.
- `SM-F2`: HTTP client가 Play role endpoint를 호출하고, server-owned route client가 route mesh
  channel에서 target node와 target spot을 함께 지정해 cross-node spot request/send를 수행하는지
  검증한다. `.NET`식 lifecycle context group에서는 같은 context spot에 후속 state request와 command를
  보낸다.
- `SM-F3`: `Client/Scenarios/sm_f3_scenario.hpp`가 같은 route mesh channel에서 일반 route packet과
  spot route packet이 함께 오가도 각각 올바른 dispatcher로 분기되는지 검증한다.
- `SM-F4`: 존재하지 않는 target spot, handler 없는 spot route request, slow spot timeout이
  client-visible failure로 끝나고 후속 정상 spot route request가 복구되는지 검증한다. v1 implicit
  route에는 ingress opt-out이 없으므로 ingress 거부는 적용 대상이 아니다. malformed relay packet은
  public typed route client로 만들 수 없어 raw-frame harness가 필요하다.
- `SM-F5`: `Client/Scenarios/sm_f5_scenario.hpp`가 spot route negative 이후에도 같은 route channel의
  일반 route request와 정상 spot route request가 계속 성공하는지 검증한다.
- `SM-F6`: MultiNode role을 RouteMesh 없이 SpotMesh node만 등록한 상태로 띄우고, 서버 간 구동
  순서와 무관하게 client readiness 뒤 remote spot request/send와 actor join이 target spot evidence에
  남는지 검증한다.
- `SM-G1`: `session-a`/`session-b`를 각각 `play-a`/`play-b`에 bind하고, actor와 stream session이
  붙은 `play-a`를 실제 SIGKILL한다. 이후 `play-b` actor/session은 계속 동작하는지 확인하고
  살아 있는 `play-b`에 재auth/rebind해 상태를 복구한다.
- `SM-G2`: 앱이 같은 logical key의 owner spot RoutingId를 `play-a`에서 `play-b`로 remap한
  흐름을 `/spot/create`와 target node가 명시된 spot route request로 표현하고, remap 전후
  request evidence가 새 owner에만 남는지 검증한다.
- `SM-G3`: 다수 stream client가 같은 user spot에 동시에 join/request/leave를 보내고, actor별
  `ActorJoined`/`ActorLeft` evidence가 정확히 1회씩 남는지 검증한다.
- `SM-G4`: 다수 stream client가 각각 다른 actor에 bind된 상태에서 동시에 push를 트리거하고,
  각 client가 자기 actor의 `ActorPushNotify`만 수신하는지 검증한다.
- `SM-Q9`: `.NET`의 multi-node route-to-spot scaffold에 대응해 multi-node A/B role을 실제로 띄운다.
  외부 client는 HTTP만 호출하고, C++ requester role이 server-side public route client와 bridge-only
  spot node를 소유한다. requester가 target node와 target spot을 지정해 `StateReq`를 보내면 같은 spot
  state와 evidence가 유지되는지 검증한다.

## 남은 시나리오

- 없음

## 남은 구현 후보

- 없음
