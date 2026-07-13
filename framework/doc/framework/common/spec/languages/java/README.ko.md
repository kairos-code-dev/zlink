# ZLink Framework Java 공개 계약

이 디렉토리는 Java framework가 제공해야 하는 정식 public contract를 설명한다.
구현과 regression test는 이 계약을 따라야 한다. Kotlin이 Java 계약을 그대로
사용하는 경우 이 문서를 따르며, Kotlin 전용 `suspend`와 `Flow` 표면은
[Kotlin 공개 계약](../kotlin/README.ko.md)에서 별도로 고정한다.

| 문서 | 범위 |
|------|------|
| [handler-interfaces](handler-interfaces.ko.md) | interface, annotation, context, options |
| [spring-boot-channel-messaging](system-structure.ko.md) | channel 등록, outbound client, dispatch |
| [spring-boot-spot](system-structure.ko.md) | Spot lifecycle, Entry Spot, timer |
| [spring-boot-actor-session](system-structure.ko.md) | actor factory, SessionRelay, bound session |
| [spring-boot-stream](system-structure.ko.md) | stream node, header session |
| [stream-connector](stream-connector.ko.md) | Java/Kotlin Stream Connector |
| [spring-boot-registry](system-structure.ko.md) | embedded registry, remote query |
| [spring-boot-monitoring](system-structure.ko.md) | runtime event, typed handler |
| [stage-wrapper-on-spot](system-structure.ko.md) | Spot 위에 상위 stage 모델(playhouse 등)을 얹는 조건 — 기본 공개 API 계약이 아니라 상위 모델 가이드 |

## 취소 인자

Java public interface에는 framework `CancellationToken`이나 같은 목적의 별도 취소
인자를 두지 않는다. `CompletionStage`를 반환한다는 사실만으로 작업 취소나 thread
interruption을 보장하지 않는다. timeout, host shutdown과 resource cleanup은 각 기능
계약을 따른다.
