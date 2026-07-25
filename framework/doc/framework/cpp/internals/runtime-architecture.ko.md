<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [다음: Backend Dependency Policy](backend-dependency-policy.ko.md)
<!-- framework-adapter-nav:end -->

[C++ 묶음](../README.ko.md) | [공개 인터페이스](../../common/spec/server/languages/cpp/interfaces/README.ko.md)

# ZLink Framework C++ Runtime Architecture

## 1. 목적

이 문서는 C++ framework 유지보수자가 공개 header와 private runtime의 경계, runtime
소유권과 실행 흐름을 코드 전에 파악할 수 있도록 설명한다. 공개 타입과 사용법은
spec과 guide가 소유한다.

## 2. 계층

```text
+--------------------------------------------------------------+
| Application and Samples                                      |
+--------------------------------------------------------------+
| Public Contracts: framework/include/zlink/framework           |
+--------------------------------------------------------------+
| Private Runtime: framework/src/runtime                        |
+--------------------------------------------------------------+
| Backend Adapters and zlink C++ Binding                        |
+--------------------------------------------------------------+
| Core Runtime                                                  |
+--------------------------------------------------------------+
```

설치 대상에는 `framework/include/zlink/framework`의 public header만 포함한다.
`framework/src/runtime`의 클래스, backend socket wrapper와 실행 queue는 설치 header에서
참조하지 않는다.

## 3. runtime 소유권

- `app_t`는 configuration과 host 조립의 진입점이다.
- framework runtime은 backend context, channel bundle, route channel, SpotNode,
  stream node와 location runtime의 수명을 소유한다.
- 각 subsystem은 자신이 만든 listener, pending operation과 callback registration을
  스스로 정리한다.
- sample과 application은 runtime 객체를 직접 만들지 않고 public host와 builder를
  사용한다.

## 4. 실행과 종료

registration validation을 먼저 끝낸 뒤 backend context, location, channel/route,
Spot, stream과 monitoring 순서로 시작한다. 종료할 때는 stop signal을 전달하고
monitoring, Spot, route, stream, channel, location, context 순서로 정리한다.

transport callback은 application handler를 직접 오래 실행하지 않는다. session과
Spot의 serial queue, coroutine executor와 worker pool이 handler 실행을 맡는다.
pending request와 submit은 timeout, cancellation 또는 runtime dispose로 반드시
완료되며 caller thread를 blocking wait로 점유하지 않는다.

## 5. CMake 경계

- public contract compile test는 설치 header만 include해 빌드한다.
- private runtime source는 framework library target에만 연결한다.
- HTTP client, stream connector와 framework extension은 독립 target과 install
  component를 유지한다.
- sample target은 public framework target만 링크하며 private source 경로를 include하지
  않는다.

## 6. 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `test_cpp_framework_layout_contract` | public header와 private runtime 경계, 실제 source tree와 sample public API 사용을 검사한다. |
| `test_cpp_framework_contract_headers` | 설치 대상 public header가 독립적으로 compile된다. |
| `test_cpp_framework_runtime` | runtime 시작·종료와 subsystem 소유권을 검증한다. |

## 7. Public raw binding 경계

C++ service runtime은 설치된 C++ binding의 public raw socket API만 사용한다. RouteMesh와 ClientServer는
ROUTER·DEALER, classic fanout은 PUB·SUB, 외부 session은 STREAM을 사용한다. Multipart, connection monitor,
send readiness와 shutdown도 binding public contract를 통해서만 사용한다.

Core service header, C handle, claim·receive batch, reply token, MeshNode monitor와 binding의 private native
accessor는 참조하지 않는다. Raw socket option을 Framework public builder로 그대로 전달하는 pass-through도
두지 않는다.

## 8. Service runtime과 mailbox

`framework_runtime_t`가 process 안의 RouteMesh, ClientServer, fanout, Spot, Actor, STREAM, Location과
monitoring subsystem을 조정한다. 각 subsystem은 transport callback에서 application handler를 직접 실행하지
않고 다음 owner mailbox에 immutable work를 제출한다.

| Work | 직렬화 owner |
|---|---|
| Node direct와 Channel handler | 해당 node 또는 channel application mailbox |
| Spot packet·timer·subscription | Spot mailbox |
| Actor packet과 lifecycle | Actor가 속한 Spot의 Actor turn |
| STREAM lifecycle·packet | session mailbox |
| reply·timeout·send-ready | infrastructure completion mailbox |

Application mailbox가 실행 중이어도 infrastructure completion mailbox는 진행한다. Request reply, timeout,
cancellation, disconnect와 shutdown 경쟁은 pending operation의 단일 terminal winner가 정리한다. One-way
admission은 signal 기반으로 대기하며 busy polling이나 timeout 증가로 지연을 숨기지 않는다.

## 9. Transport liveness

Service runtime의 기본 liveness timing은 idle 5초, inbound deadline 15초다. RouteMesh와 ClientServer runtime은
Framework service protocol의 `livenessProbe`와 `livenessAck`을 처리한다. Fanout subscriber runtime은
publisher마다 전용 SUB socket과 receive loop를 두고, 첫 valid application record 또는
[exact two-frame beacon](../../common/internals/service-wire-protocol.ko.md#411-classic-fanout-liveness-frame)을 받은
뒤에만 해당 publisher를 ready로 만든다. 이 정책을 Framework public API로 노출하지 않는다. Core raw socket은
disconnect·error monitor와 reconnect primitive만 제공한다. Orderly disconnect와 transport monitor failure는
즉시 ready index에서 제거하고, 마지막 valid receive부터 15초가 지난 connection만 not-ready로 바꾸고 닫는다.

Reconnect는 service admission, RID와 lifecycle generation 검증을 다시 수행한다. 이전 connection의 ready,
reply correlation, session binding과 callback을 재사용하지 않는다. Fanout timeout은 해당 publisher의 전용
socket만 닫고 해당 publisher만 not-ready로 바꾼다. Location Store 장애가 발생해도 이미 연결된 peer의 transport
liveness는 진행하며, service liveness ACK와 fanout beacon이 만료된 owner lease를 복구하지 않는다.

## 10. Authority와 relocation payload

Location runtime은 provider가 발급한 opaque store version으로 owner·relocation authority를 compare-exchange한다.
Framework가 authority payload 안의 owner, fence, phase, coordinator lease와 recovery cursor를 encode한다.
Provider와 application adapter는 이 내부 schema를 해석하지 않는다.

Snapshot relocation은 application adapter가 반환한 opaque bytes를 accepted journal·timer state와 함께
Relocation Store에 immutable payload로 먼저 저장한다. Location Store는 reference와 checksum을 authority CAS로
공개한다. Relocation reference, retention, journal sequence와 phase는 application callback에 전달하지 않는다.
Reference 사용을 Location Store CAS로 끝낸 뒤 payload를 삭제하거나 recovery retention까지 유지한다. CAS 전에
연결되지 않은 orphan은 provider TTL이 정리한다.

## 11. Retire와 Shutdown

Host maintenance coordinator 하나가 `Retire`와 `Shutdown`을 직렬화한다. `Retire`는 admission을 바꾸기 전에
target, relocation policy, provider와 capacity를 모두 preflight한다. 실패하면 state를 `Serving`으로 유지한다.
`Shutdown`은 새 relocation을 시작하지 않고 admission seal, accepted work, STREAM barrier와 resource cleanup을
deadline까지 수행한다.

기존 `drain`, `await_drained`, `stop`과 `request_stop` public member는 coordinator의 Shutdown 경로에 연결한다.
Compatibility를 위해 같은 책임의 두 번째 상태 기계를 만들지 않는다. Terminal cleanup은 service
liveness·reconnect timer, monitor subscription과 pending callback을 socket보다 늦게 남기지 않는다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [다음: Backend Dependency Policy](backend-dependency-policy.ko.md)
<!-- framework-adapter-nav:bottom:end -->

## 공개 계약 문서에서 이관한 내부 dispatch 구조

공개 계약 스펙을 3문서로 압축하면서, 기능별 계약 문서가 서술하던 **내부 런타임 클래스**를
이 절로 옮겼다. 이 클래스들은 **public header에 없다** — 공개 계약이 아니라 구현 구조다.

## 11-channel-messaging — 4. Dispatch 기준

- 일반 request/send dispatch는 local server 역할 ingress 기준이다.
- outbound client 역할 수신은 pending request의 reply correlation 경로다.
- pending request correlation은 `channel_pending_requests_t`가 맡는다. request sequence와
  pending table은 public call object에 노출하지 않는다.
- server ingress envelope dispatch는 `channel_packet_dispatcher_t`가 맡는다. request는
  reply writer를 통해 response/error envelope로 변환하고 command/send는 reply 없이
  handler dispatch만 수행한다.
- channel 역할 runtime bundle은 `channel_runtime_bundle_t`가 맡는다. manual
  connection set, channel pending request owner, receive gate는 한 역할의 내부
  상태로 묶고 public builder나 call object에 노출하지 않는다.
- channel 역할 생성과 조회는 `channel_bundle_factory_t`와
  `channel_runtime_manager_t`가 맡는다. manager는 `.NET`처럼 client/publisher bundle을
  lazy creation으로 만들고 inbound, client, publisher, route channel 초기화를 runtime
  state 안에서 정리한다.
- server ingress는 channel host service가 수신한 envelope parts를
  `channel_packet_dispatcher_t`로 넘겨 처리한다. receive gate와 connection 상태는
  `channel_runtime_bundle_t`가 소유하고, 별도 pump 타입을 public 또는 production runtime
  구조로 노출하지 않는다.
- route channel은 `route_channel_runtime_t`와 `route_connection_set_t`가 맡는다.
  route channel id, manual connection snapshot, target node/Spot routing id, outbound
  envelope parts, request sequence correlation을 runtime 내부에 둔다. public API는 route
  channel 이름과 typed send/request 표면만 드러내고 native router socket과 receive pump는
  노출하지 않는다.
- route channel handler 등록은 `route_channel_registration_t`와
  `route_channel_initializer_t`가 맡는다. `.NET`은 reflection scanner와 assembly marker로
  descriptor를 수집하지만, C++는 typed handler installer를 registration에 저장한 뒤
  initializer가 `route_handler_registry_t`로 변환한다. 프레임워크 사용자는
  `options.add_route_mesh(name)`으로 server endpoint, routing id, client endpoint,
  handler group을 설정한다. route handler 수신이나 SPOT route ingress가 필요한
  runtime은 `enable_server(endpoint)`로 local ROUTER endpoint를 열고, 다른 node로만
  보내는 runtime은 `enable_client()` 또는 `enable_client(endpoint)`만 선언할 수 있다.
  SPOT route ingress는 같은 프로세스에 RouteMesh와 SpotMesh가 함께 있을 때 자동으로
  연결된다. 외부에서 Spot으로 들어오는 routed 호출은 RouteMesh만 사용한다.
  `zlink_builder_t::route_channel(name, configure)`와 `route_channel_builder_t`는 framework
  내부와 고급 확장용 낮은 수준 표면으로 남긴다.
- client/server channel은 server 또는 client 역할 중 하나 이상이 필요하고, fanout
  channel은 publisher 또는 subscriber 역할 중 하나 이상이 필요하다. 아무 역할도 없는
  channel 선언은 framework options 적용 시점에 실패한다.
- route receive path는 route channel host service가 받은 routed packet을
  `route_packet_dispatcher_t`로 넘겨 처리한다. route handler가 있으면
  `route_handler_registry_t`와 `route_handler_invoker_t`를 통해 typed payload를 호출하고,
  handler가 없으면 request에 `route_handler_not_found` error envelope를 반환한다.
  framework 내부 routed packet은 `route_internal_packet_dispatcher_t`와 composite
  dispatcher가 먼저 처리한다.
- 같은 역할에서 Discovery와 manual 연결을 같이 섞지 않는다. endpoint 인자 없는
  `enable_client()` 또는 `enable_subscriber()`는 discovery mode를 뜻하고, endpoint를 받는 overload는
  manual endpoint를 추가한다.
- runtime 연결 제어가 필요하면 framework core의 역할 단위 connection manager가
  담당한다. 사용자는 raw socket이 아니라 channel 역할 표면으로 연결을 다룬다.

등록된 request handler 가 없거나 request payload decode, handler 실행 중 예외, invalid request frame 이
발생하면 server runtime 은 error reply 를 반환한다. 같은 사건은 Error 로그, counter,
`outcome=error` 메시지 흐름 이벤트로도 남긴다.

send 또는 publish 에서 handler 를 찾지 못하면 reply 를 만들지 않고 drop 한다. send 는 Warning 로그와
counter, publish 는 Debug 로그 또는 counter 와 message-flow event 를 남긴다. observer 가 없더라도
기본 로그와 counter 는 생략하지 않는다. observer callback 예외는 dispatch 결과를 바꾸지 않는다.

## 30-stream — 4. Dispatch 기준

- framework host가 binding의 `zlink::stream_socket_t` lifecycle을 관리한다.
- packet callback은 framework가 packet 수신과 header 검증을 마친 뒤 호출한다. 별도
  실행기로 넘기는 것이 기본은 아니다.
- CPU-bound 또는 blocking 가능성이 있는 stream handler는 offload 실행 정책을 명시한다.
- 같은 stream session의 lifecycle callback과 packet callback은 직렬로 처리한다.
- Header 검증에 실패한 packet은 application handler로 넘기지 않는다.
- `stream_t::write_packet(...)`과 `reply_packet(...)`은 one-way write로 본다. 반환된
  `stream_write_call_t`에서 `metadata(...)`, `packet_name(...)`, `compress()`를 설정할 수 있고,
  실제 제출은 `submit()`에서 시작한다. write 제출은 응답을 기다리지 않는다.
- `stream_t::close()`는 session을 닫고 이후 write submit을 연결 끊김 경계 오류
  (`framework_exception_t`, `code() == std::errc::not_connected` — public enum 값이 아니라
  §8.1의 경계 의미)로 처리하게 한다. 이미 닫힌 stream을 다시 닫는 것은 성공으로 처리해
  cleanup 호출자가 중복 close를 특별히 구분하지 않아도 되게 한다.
- session actor dispatch는 STREAM session에서 route mesh channel로 직접 packet을 만들지
  않고, ActorGateway와 `session_actor_t::relay(...)`를 사용한다.
- session callback 동안 받은 `payload`는 framework가 빌려준 값이므로 relay 호출자가
  해제하거나 move로 소비하지 않는다.
