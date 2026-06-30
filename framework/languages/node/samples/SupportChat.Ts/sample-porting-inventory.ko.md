# SupportChat.Ts .NET 기준 포팅 Inventory

이 문서는 `framework/doc/plan/framework-node-sample-dotnet-porting-plan.ko.md`의
샘플 단위 절차에 따라 현재 Node SupportChat 샘플을 공통 샘플 문서와 `.NET` 기준 구현에
매핑한다. `gap`은 완료 판정이 아니라 다음 수정 대상이다.

## 파일과 역할 매핑

| 기준 | Node 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `.NET: Client/SupportChatClientScenario.cs` | `Client/supportchat-client-scenario.ts` | client-scenario | done | customer/agent 인증, 상담 생성, 배정, 메시지, typing, reconnect, idle, close를 검증한다. |
| `.NET: Client/Program.cs` | `Client/main.ts` | client-entry | done | Session stream endpoint를 읽고 scenario를 실행한다. |
| `.NET: Client/Configuration/SampleNames.cs` | `Client/Configuration/sample-names.ts`, `Shared/Contracts/messages.ts` | configuration | done | sample actor id와 packet 이름을 공유한다. |
| `.NET: Probe/Program.cs` | `Server/Probe/main.ts` | probe | done | registry topology에서 API, Support, notification channel endpoint readiness를 확인하고 `topology=ready`를 출력한다. |
| `.NET: Server/Configuration/SampleTopology.cs` | `Server/Configuration/sample-config.ts` | configuration | done | registry, API, Session, Support endpoint를 읽는다. |
| `.NET: Server/Configuration/SampleFlowLog.cs` | role module의 message-flow trace 설정 | logging | not-needed | Node는 module별 trace log file과 label을 직접 설정한다. |
| `.NET: Server/Registry/RegistryHostFactory.cs` | `Server/Registry/registry-server-host.ts` | server-role | done | registry host를 독립 role로 실행한다. |
| `.NET: Server/Registry/Program.cs` | `Server/Registry/main.ts` | server-entry | done | registry role entry point다. |
| `.NET: Server/Api/ApiServerHostFactory.cs` | `Server/Api/supportchat-api-module.ts` | server-role | done | API channel server와 Support channel client를 구성한다. |
| `.NET: Server/Api/Handlers/AuthenticateUserHandler.cs` | `Server/Api/Handlers/authenticate-user-handler.ts` | handler | done | token을 support user identity로 바꾼다. |
| `.NET: Server/Api/Handlers/OpenConversationHandler.cs` | `Server/Api/Handlers/open-conversation-handler.ts` | handler | done | actor가 보낸 open request를 support allocation으로 연결한다. |
| `.NET: Server/Api/Program.cs` | `Server/Api/main.ts` | server-entry | done | API role entry point다. |
| `.NET: Server/Session/SessionServerHostFactory.cs` | `Server/Session/supportchat-session-module.ts` | server-role | done | stream server, API client, support relay, session Spot node를 구성한다. |
| `.NET: Server/Session/Sessions/SupportChatSession.cs` | `Server/Session/Sessions/supportchat-session.ts` | stream-session | done | 인증 이후 conversation packet을 bound actor로 relay한다. |
| `.NET: Server/Session/Sessions/Handlers/AuthenticateSupportChatSessionHandler.cs` | `Server/Session/Sessions/Handlers/authenticate-session-handler.ts` | stream-handler | done | stream 인증, actor 생성, session binding을 처리한다. |
| `.NET: Server/Session/Program.cs` | `Server/Session/main.ts` | server-entry | done | Session role entry point다. |
| `.NET: Server/Support/SupportServerHostFactory.cs` | `Server/Support/supportchat-support-module.ts` | server-role | done | actor runtime, Entry Spot, Conversation Spot, channel handlers, notifications를 구성한다. |
| `.NET: Server/Support/Application/ConversationAssignment/*` | `Server/Support/Application/ConversationAssignment/*` | application | done | conversation allocation, agent availability, assignment를 맡는다. |
| `.NET: Server/Support/Domain/SupportChat/*` | `Server/Support/Domain/SupportChat/*` | domain | done | conversation state, participant, message, typing, close rule을 framework 타입 없이 표현한다. |
| `.NET: Server/Support/Infrastructure/ZLink/Actors/*` | `Server/Support/Infrastructure/ZLink/Actors/*` | actor | done | support user actor와 factory를 제공한다. |
| `.NET: Server/Support/Infrastructure/ZLink/ConversationStarter.cs` | `Server/Support/Infrastructure/ZLink/Handlers/open-conversation-channel-handler.ts` | adapter | done | open conversation flow를 support actor/channel path로 연결한다. |
| `.NET: Server/Support/Infrastructure/ZLink/Handlers/*` | `Server/Support/Infrastructure/ZLink/Handlers/*` | handler | done | channel request를 application/actor/Spot 흐름으로 연결한다. |
| `.NET: Server/Support/Infrastructure/ZLink/Spots/EntrySpot/*` | `Server/Support/Infrastructure/ZLink/Spots/EntrySpot/*` | entry-spot | done | open conversation과 agent availability actor request를 처리한다. |
| `.NET: Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/*` | `Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/*` | conversation-spot | done | participant join, message, typing, idle timer, close 상태를 소유한다. |
| `.NET: Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/Notifications/*` | `Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/Notifications/*` | notification | done | domain event를 bound session push message로 변환한다. |
| `.NET: Shared/Contracts/Messages.cs` | `Shared/Contracts/messages.ts` | shared-contract | done | stream, channel, actor, Spot payload 계약을 정의한다. |
| `.NET: run_sample.sh` | `run_sample.sh` | runner | done | registry, API, Session, Support, client self-check를 실행한다. |
| `.NET: run_sample.ps1` | `run_sample.ps1` | runner | done | Unix PowerShell에서는 검증된 Linux runner를 호출해 같은 topology와 self-check marker를 사용한다. |

## 공통 요구 매핑

| 기준 | Node 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `common: client는 Session stream endpoint 하나만 안다` | `Client/main.ts`, `Client/supportchat-client-scenario.ts` | validation | done | customer와 agent 모두 Session stream으로 접속한다. |
| `common: Registry/Discovery 자동 연결` | role module의 `.useDiscovery()` 설정 | topology | done | Session, API, Support role이 registry endpoint를 사용한다. |
| `common: actor binding과 bound session push` | `authenticate-session-handler.ts`, `support-notification-publisher.ts` | stream-actor | done | 인증 후 actor를 session에 bind하고 conversation event를 push한다. |
| `common: conversation Spot 상태 소유` | `conversation-spot.ts`, `conversation.ts` | spot-domain | done | message sequence, typing, idle, close 상태를 conversation/domain에서 처리한다. |
| `common: reconnect 유지` | `supportchat-client-scenario.ts`, `supportchat-session.ts` | validation | done | 새 stream session이 같은 actor/conversation 상태를 이어 받는지 검증한다. |
| `common: idle timer와 close` | `conversation-idle-timer-handler.ts`, `close-conversation-handler.ts` | validation | done | idle 전이와 close notify를 검증한다. |
| `common: stream connector public wait` | `Client/supportchat-client-scenario.ts` | validation | done | push notify를 connector wait interface로 검증한다. |
| `common: success marker PASS SupportChat.Ts` | `run_sample.sh` | validation | done | runner 성공 시 출력한다. |

## 남은 확인

- PowerShell runner의 Windows 전용 경로는 별도 Windows 환경에서 확인해야 한다.
