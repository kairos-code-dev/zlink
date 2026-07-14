# C++ Registration/Codec E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-4-registration-codec.ko.md`

이 문서는 공통 시나리오 중 C++ framework의 공개 API로 직접 검증하는 항목을 구분한다.

최신 proof는 `logs/20260708-124643-1329498`이다. Config-4는 위치 resolve를 다루지 않아 Redis
location store를 등록하지 않고 수동 endpoint 연결만 사용한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| `RC-A1` | 구현 | handler group 기반 request/send 등록을 검증한다. C++에서는 packet 타입의 `static constexpr packet_name`이 공개 packet 이름이 된다. 로그: `logs/20260708-124643-1329498`, 출력: `scenario RC-A1 passed`, `registration-codec e2e result=passed`. |
| `RC-A2` | 구현 | 공통 spec의 annotation 의미인 packet kind/name override를 C++에서는 handler 타입의 `request_type`/`message_type`, `topic_name`, DTO의 `packet_name` metadata로 표현한다. `EchoAttr` request와 `EchoAttrMsg` send가 같은 handler group에 노출되고 같은 evidence 의미를 남기는지 검증한다. 로그: `logs/20260708-124643-1329498`, 출력: `scenario RC-A2 passed`, `registration-codec e2e result=passed`. |
| `RC-A3` | 구현 | 수동 channel handler 등록의 명시 packet 이름으로 request와 send를 검증한다. 로그: `logs/20260708-124643-1329498`, 출력: `scenario RC-A3 passed`, `registration-codec e2e result=passed`. |
| `RC-A4` | 구현 | request handler마다 새 invocation scope가 만들어지고 scoped dependency가 요청 뒤 dispose되는지 검증한다. singleton dependency 유지도 함께 확인한다. 로그: `logs/20260708-124643-1329498`, 출력: `scenario RC-A4 passed`, `registration-codec e2e result=passed`. |
| `RC-A5` | 구현 | handler filter pipeline의 before/after 실행 순서를 검증한다. 로그: `logs/20260708-124643-1329498`, 출력: `scenario RC-A5 passed`, `registration-codec e2e result=passed`. |
| `RC-A6` | 구현 | client scenario가 invalid server process를 직접 관리하며 중복 handler 등록, 잘못된 handler group/channel 조합, handler group 없는 server 구성을 startup failure와 정확한 validation 오류로 검증한다. 출력: `scenario RC-A6 duplicate passed`, `scenario RC-A6 wrong-group passed`, `scenario RC-A6 unsupported-channel passed`, `scenario RC-A6 passed`. |
| `RC-B1` | 구현 | JSON request와 send를 실행하고 `application/json` content-type evidence를 확인한다. 로그: `logs/20260708-124643-1329498`, 출력: `scenario RC-B1 passed`, `registration-codec e2e result=passed`. |
| `RC-B2` | 구현 | C++ Protobuf codec extension으로 request와 send를 실행하고 `application/x-protobuf` content-type evidence를 확인한다. 로그: `logs/20260708-124643-1329498`, 출력: `scenario RC-B2 passed`, `registration-codec e2e result=passed`. |
| `RC-B3` | 구현 | C++ MessagePack codec extension으로 request와 send를 실행하고 `application/x-msgpack` content-type evidence를 확인한다. 로그: `logs/20260708-124643-1329498`, 출력: `scenario RC-B3 passed`, `registration-codec e2e result=passed`. |
| `RC-B4` | 구현 | 한 host의 global registry에서 JSON, Protobuf, MessagePack, custom serializer가 공존하는지 reply 값과 실제 inbound content-type으로 확인한다. 로그: `logs/20260708-124643-1329498`, 출력: `scenario RC-B4 passed`, `registration-codec e2e result=passed`. |
| `RC-B5` | 구현 | codec requester가 Protobuf content-type request를 JSON-only peer에 보내 handler context로 확인하고, payload는 JSON fallback 규칙으로 처리되며 이후 정상 JSON request가 계속 성공하는지 확인한다. 로그: `logs/20260708-124643-1329498`, 출력: `scenario RC-B5 passed`, `registration-codec e2e result=passed`. |

## C++ 언어 표면 차이

C++는 `.NET`의 attribute syntax나 assembly scan을 그대로 제공하지 않는다. 대신 공통 spec이 요구하는
packet kind/name override 의미를 타입 metadata로 표현한다. `RC-A2`는 이 공개 표면이 request와 send
handler를 같은 handler group에 노출하고, `EchoAttr` 계열 packet이 자동/수동 등록과 같은 reply와
evidence 의미를 내는지 검증한다.
