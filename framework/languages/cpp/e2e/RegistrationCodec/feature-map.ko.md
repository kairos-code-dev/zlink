# C++ Registration/Codec E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-4-registration-codec.ko.md`

이 문서는 공통 시나리오 중 C++ framework의 공개 API로 직접 검증하는 항목과 C++ 언어 표면 차이로
제외한 항목을 구분한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| `RC-A1` | 구현 | handler group 기반 request/send 등록을 검증한다. C++에서는 packet 타입의 `static constexpr packet_name`이 공개 packet 이름이 된다. 로그: `logs/20260630-182337-725659`, 출력: `scenario RC-A1 passed`, `registration-codec e2e result=passed`. |
| `RC-A2` | 제외 | attribute 기반 handler discovery는 `.NET`의 attribute 표면이다. C++ public API는 명시 registry 등록만 제공한다. |
| `RC-A3` | 구현 | route mesh builder의 명시 packet 이름 등록으로 request handler를 검증한다. 로그: `logs/20260630-182337-725659`, 출력: `scenario RC-A3 passed`, `registration-codec e2e result=passed`. |
| `RC-A4` | 구현 | request handler마다 새 invocation scope가 만들어지고 scoped dependency가 요청 뒤 dispose되는지 검증한다. singleton dependency 유지도 함께 확인한다. 로그: `logs/20260630-182337-725659`, 출력: `scenario RC-A4 passed`, `registration-codec e2e result=passed`. |
| `RC-A5` | 구현 | handler filter pipeline의 before/after 실행 순서를 검증한다. 로그: `logs/20260630-182337-725659`, 출력: `scenario RC-A5 passed`, `registration-codec e2e result=passed`. |
| `RC-A6` | 구현 | 중복 handler 등록, 잘못된 handler group/channel 조합, handler group 없는 server 구성을 startup failure로 검증한다. 로그: `logs/20260630-182337-725659`, 출력: `scenario RC-A6 duplicate passed`, `scenario RC-A6 wrong-group passed`, `scenario RC-A6 unsupported-channel passed`, `scenario RC-A6 passed`. |
| `RC-B1` | 구현 | JSON request와 send를 실행하고 `application/json` content-type evidence를 확인한다. 로그: `logs/20260630-182337-725659`, 출력: `scenario RC-B1 passed`, `registration-codec e2e result=passed`. |
| `RC-B2` | 구현 | C++ Protobuf codec extension으로 request와 send를 실행하고 `application/x-protobuf` content-type evidence를 확인한다. 로그: `logs/20260630-182337-725659`, 출력: `scenario RC-B2 passed`, `registration-codec e2e result=passed`. |
| `RC-B3` | 구현 | C++ MessagePack codec extension으로 request와 send를 실행하고 `application/x-msgpack` content-type evidence를 확인한다. 로그: `logs/20260630-182337-725659`, 출력: `scenario RC-B3 passed`, `registration-codec e2e result=passed`. |
| `RC-B4` | 구현 | 한 host의 global registry에서 JSON, Protobuf, MessagePack, custom serializer가 공존하는지 reply 값과 실제 inbound content-type으로 확인한다. 로그: `logs/20260630-182337-725659`, 출력: `scenario RC-B4 passed`, `registration-codec e2e result=passed`. |
| `RC-B5` | 구현 | Protobuf content-type request가 JSON-only peer에 도착했음을 handler context로 확인하고, payload는 JSON fallback 규칙으로 처리되며 이후 정상 JSON request가 계속 성공하는지 확인한다. 로그: `logs/20260630-182337-725659`, 출력: `scenario RC-B5 passed`, `registration-codec e2e result=passed`. |

## C++ 언어 표면 차이

C++는 `.NET`의 attribute/assembly scan 등록 표면을 제공하지 않는다. 따라서 `RC-A2`는 C++에서
새 public API를 만들지 않고 제외한다. C++의 대응 등록 표면은 `RC-A1`과 `RC-A3`에서 명시 registry와
route mesh 등록으로 검증한다.
