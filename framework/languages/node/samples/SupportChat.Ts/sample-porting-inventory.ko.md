# SupportChat.Ts sample porting inventory

이 문서는 P-N1 SupportChat Node 포팅의 파일 대응표다. .NET 정본은 `framework/languages/dotnet/samples/SupportChat`이다.

| .NET 역할 | Node 대응 | 상태 |
|-----------|-----------|------|
| `Shared/Contracts/Messages.cs` | `Shared/Contracts/messages.ts` | done |
| `Client/SupportChatClientScenario.cs` | `Client/supportchat-client-scenario.ts` | done |
| `Client/Program.cs` | `Client/main.ts` | done |
| `Server/Api` | `Server/Api` | done |
| `Server/Support` | `Server/Support` | done |
| `Server/Session` | `Server/Session` | done |
| `Server/Support/Domain/SupportChat` | `Server/Support/Domain/SupportChat` | done |
| `Server/Support/Application/ConversationAssignment` | `Server/Support/Application/ConversationAssignment` | done |
| `Server/Support/Infrastructure/ZLink/Actors` | `Server/Support/Infrastructure/ZLink/Actors` | done |
| `Server/Support/Infrastructure/ZLink/Handlers` | `Server/Support/Infrastructure/ZLink/Handlers` | done |
| `Server/Support/Infrastructure/ZLink/Spots/EntrySpot` | `Server/Support/Infrastructure/ZLink/Spots/EntrySpot` | done |
| `Server/Support/Infrastructure/ZLink/Spots/ConversationSpot` | `Server/Support/Infrastructure/ZLink/Spots/ConversationSpot` | done |
| `run_sample.sh` | `run_sample.sh` | done |

## 시나리오 대응

- 상담원 availability 등록: done
- 고객 2명의 대화 생성과 같은 상담원 배정: done
- 대화별 메시지 순번 독립성: done
- typing one-way 알림: done
- 고객과 상담원 재접속 후 join: done
- 명시 close와 idle close: done
- 닫힌 대화의 메시지 거부와 typing 무시: done
- 상담원 부재 시 `WaitingForAgent`: done

## 검증 연결

- `samples/run_samples.sh`에서 `SupportChat.Ts/run_sample.sh`를 실행한다.
- `test/contract/sample-regression.test.js`의 required sample과 topology sample 목록에 `SupportChat.Ts`를 포함한다.
