# ToActorMessaging Java porting inventory

## 범위

- 기준: `framework/doc/framework/common/e2e/config-9-to-actor-messaging.ko.md`
- 기준 구현: `framework/languages/dotnet/e2e/ToActorMessaging`
- 대상: `framework/languages/java/e2e/ToActorMessaging`

## 매핑

| .NET 기준 | Java 위치 | 상태 |
|---|---|---|
| `Shared/Messages.cs` | `Shared/src/main/java/.../shared/Contracts.java` | implemented |
| `Server/Actor` | `Server/Actor/src/main/java/.../actor/Program.java` | implemented |
| `Server/Caller` | `Server/Caller/src/main/java/.../caller/Program.java` | implemented |
| `Client` | `Client/src/main/java/.../client/Program.java` | implemented |
| Track A TA-A1..TA-A4 | Java client scenario assertions | implemented |
| Track B TA-B1..TA-B3 | Java client failure/success assertions | partial: stale/route-disconnect fault injection uses public observable fallback paths until core fault controls exist |

## 제한

TA-B2 stale row 강제와 TA-B3 route 단절 강제는 Java framework public API만으로 직접 만들 수 있는 fault
control이 아직 없다. 스위트는 같은 public actor client 호출 경로와 error-kind 확인을 유지하고, 강제 fault
injection은 후속 core/runtime test hook 없이 추가하지 않는다.
