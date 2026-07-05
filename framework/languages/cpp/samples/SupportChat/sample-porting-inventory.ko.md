# C++ SupportChat .NET 기준 포팅 inventory

기준 구현: `framework/languages/dotnet/samples/SupportChat`

이 문서는 `.NET SupportChat` 샘플 책임이 C++ 샘플에서 어디에 대응되는지 기록한다.

## 파일 매핑

| .NET 기준 파일/책임 | C++ 대응 파일 | 상태 | 비고 |
|---------------------|---------------|------|------|
| `Shared/Contracts/Messages.cs` | `Shared/Contracts/messages.hpp` | done | 공통 문서의 request, response, notify 이름과 상태 필드를 둔다. |
| `Server/Support/Domain/SupportChat/*` | `Server/Support/Domain/SupportChat/conversation.hpp` | done | conversation 상태, 참여자, 메시지 순서, typing, idle, close 전이를 domain에 둔다. |
| `Server/Support/Application/ConversationAssignment/*` | `Server/Support/Application/ConversationAssignment/agent_assignment_service.hpp` | done | 상담원 availability와 capacity 기반 배정을 둔다. |
| `Server/Configuration/*` | `Server/Configuration/sample_topology.hpp`; `Server/Configuration/role_process.hpp` | partial | runner endpoint와 flow log 경로를 해석한다. |
| `Server/Api/Program.cs` | `Server/Api/main.cpp` | partial | role process와 flow evidence target이다. |
| `Server/Session/Program.cs`; `SupportChatSession.cs` | `Server/Session/main.cpp` | partial | role process와 flow evidence target이다. stream relay 구현은 아직 framework session adapter로 분리되지 않았다. |
| `Server/Support/Program.cs`; ZLink adapters | `Server/Support/main.cpp` | partial | role process와 flow evidence target이다. actor, Spot, bound session push adapter는 아직 별도 C++ framework adapter로 확장되지 않았다. |
| `Client/SupportChatClientScenario.cs` | `Client/supportchat_client_scenario.hpp`; `Client/main.cpp` | done | 고객/상담원 self-check 흐름과 marker를 검증한다. |
| `run_sample.sh` | `run_sample.sh` | done | build-redis-vcpkg target build, Redis 준비, role/probe/client 실행, flow trace marker를 검증한다. |
| `SupportChat.csproj`/role csproj | `framework/languages/cpp/CMakeLists.txt` | done | C++ role/client/probe executable과 `sample_smoke` ctest를 등록한다. |

## 남은 차이

- 현재 C++ role process는 flow evidence와 client self-check를 제공하지만, `.NET` 정본처럼
  Session 서버 stream packet을 actor gateway로 relay하고 Support 서버 conversation Spot에서
  bound session push를 보내는 full runtime adapter는 아직 분리되어 있지 않다.
- 이 차이는 완료 판정의 public contract parity 관점에서는 gap이다. 샘플 runner는 domain
  self-check와 role/flow target 검증을 통과하지만, 실제 stream connector 왕복과 actor
  binding 왕복을 증명하지 않는다.
