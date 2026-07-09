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
| `Client` | `Client/src/main/java/.../client/Program.java` | implemented: caller response와 actor evidence를 함께 검증 |
| Track A TA-A1..TA-A4 | Java client scenario assertions | implemented |
| Track B TA-B1..TA-B3 | Java client failure/success assertions | implemented: TA-B2/TA-B3는 public `ActorRef` direct call 경로와 actor evidence 부재 확인으로 실패가 actor handler에 도달하지 않았음을 검증 |

## 검증

- `nice -n 10 timeout 600s ./run_e2e.sh all`
- `logs/20260707-221746-3642522`: `to-actor-messaging e2e result=passed`.
- `logs/20260707-185917-3176484`: `to-actor-messaging e2e result=passed`.
