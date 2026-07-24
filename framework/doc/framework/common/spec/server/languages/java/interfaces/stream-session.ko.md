# Java STREAM session 공개 인터페이스

[인터페이스 목차](README.ko.md) · [STREAM session](../../../../30-stream-session.ko.md)

STREAM session, Actor binding과 relay는 JVM service runtime이 소유한다. Java binding의 public stream socket과
raw frame API만 사용하며 Core session service나 private JNI SPI를 public 또는 internal dependency로 삼지 않는다.

Session bind는 exact `ActorRef`를 한 번 제출한다. Active forwarding mapping이 없으면 `ACTOR_LOCATION_STALE`,
generation이 다르면 `ACTOR_GENERATION_STALE`, pre-commit seal 중이면 `ACTOR_MOVING`이다. Framework는 Store에서
새 ref를 찾아 hidden retry하지 않는다. `enableActorDispatch()`는 MeshName을 받지 않으며 startup에 Object
Client 또는 Server role과 Location Store가 필요하다.

Actor가 다른 MeshNode에 있으면 JVM runtime은 Java binding의 public raw ROUTER request·send·reply만 사용해
command 38 bind, bound-session tail이 있는 command 24 ingress와 command 36 push를 처리한다. Public interface는
변하지 않는다. Runtime은 Actor ObjectGeneration, source·target NodeGeneration, AuthorityOwnerGeneration,
binding generation과 session sequence를 application callback 전에 검사한다. Rebind와 close는 exact binding
identity transition이다. Identity는 session owner Node RID·lifecycle generation·owner-local [binding generation](../../../../01-glossary.ko.md#binding-generation)을
함께 사용하므로 다른 [MeshNode](../../../../01-glossary.ko.md#meshnode)나 재시작한 [owner](../../../../01-glossary.ko.md#owner)의 작은 local counter도 새 binding으로 등록할 수 있다. 이전 owner
lifecycle의 push·ingress·tombstone은 current session에 적용하지 않는다.

JVM backend socket close는 remote unbind completion을 bounded lifecycle deadline 안에서 관찰한다. Timeout이나
terminal failure를 무시하지 않고 close failure로 반환하며, 성공·실패와 관계없이 local binding과 raw STREAM
socket을 정리한다. 이 동작을 위한 추가 public member는 제공하지 않는다.

## Exact public member inventory

아래 선언은 이 category의 Java public type과 member를 고정한다.

```java
public interface systems.zlink.framework.streams.ZLinkSession {
  public abstract systems.zlink.framework.streams.ZLinkSessionContext context();
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> onConnected();
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> onDisconnected();
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> onError(systems.zlink.framework.streams.ZLinkStreamError);
  public default java.util.concurrent.CompletionStage<java.lang.Void> onDispatch(systems.zlink.framework.streams.ZLinkSessionDispatchContext, systems.zlink.framework.messaging.ZLinkMessage);
}
public interface systems.zlink.framework.streams.ZLinkSessionActor {
  public abstract java.lang.String actorId();
  public abstract systems.zlink.framework.actors.ActorRef ref();
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> relay(systems.zlink.framework.messaging.ZLinkMessage);
  public default java.util.concurrent.CompletionStage<java.lang.Void> relay(systems.zlink.framework.streams.ZLinkSessionDispatchContext, systems.zlink.framework.messaging.ZLinkMessage);
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> notifyDisconnected();
}
public interface systems.zlink.framework.streams.ZLinkSessionActors {
  public abstract java.util.List<systems.zlink.framework.streams.ZLinkSessionActor> bound();
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.streams.ZLinkSessionActor> bind(systems.zlink.framework.actors.ActorRef);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.streams.ZLinkSessionActor> bindOrGet(systems.zlink.framework.actors.ActorRef);
  public abstract java.util.Optional<systems.zlink.framework.streams.ZLinkSessionActor> find(java.lang.String);
}
public interface systems.zlink.framework.streams.ZLinkSessionClient {
  public abstract systems.zlink.framework.streams.ZLinkSessionSendCall send(java.lang.Object);
  public abstract systems.zlink.framework.streams.ZLinkSessionReplyCall reply(java.lang.Object);
}
public interface systems.zlink.framework.streams.ZLinkSessionContext {
  public abstract java.lang.String sessionId();
  public abstract java.util.Optional<systems.zlink.contracts.core.RoutingId> routingId();
  public abstract java.util.Optional<java.lang.String> localAddr();
  public abstract java.util.Optional<java.lang.String> remoteAddr();
  public abstract systems.zlink.framework.streams.ZLinkSessionClient client();
  public abstract systems.zlink.framework.streams.ZLinkSessionActors actors();
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> close();
}
public interface systems.zlink.framework.streams.ZLinkSessionPacketDispatcher<TSessionContext extends systems.zlink.framework.streams.ZLinkSessionContext> {
  public abstract java.util.concurrent.CompletionStage<java.lang.Boolean> tryHandle(TSessionContext, systems.zlink.framework.streams.ZLinkSessionDispatchContext, systems.zlink.framework.messaging.ZLinkMessage);
}
public interface systems.zlink.framework.streams.ZLinkSessionReplyCall {
  public abstract systems.zlink.framework.streams.ZLinkSessionReplyCall compress();
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> submit();
}
public interface systems.zlink.framework.streams.ZLinkSessionSendCall {
  public abstract systems.zlink.framework.streams.ZLinkSessionSendCall metadata(java.lang.String, java.lang.String);
  public abstract systems.zlink.framework.streams.ZLinkSessionSendCall compress();
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> submit();
}
public interface systems.zlink.framework.streams.ZLinkStreamCompressionCodec {
  public abstract byte[] compress(byte[]);
  public abstract byte[] decompress(byte[], int);
}
public final class systems.zlink.framework.streams.ZLinkStreamDiagnostic extends java.lang.Record {
  public systems.zlink.framework.streams.ZLinkStreamDiagnostic(int, java.lang.String);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public int nativeCode();
  public java.lang.String message();
}
public final class systems.zlink.framework.streams.ZLinkStreamError extends java.lang.Record {
  public systems.zlink.framework.streams.ZLinkStreamError(systems.zlink.framework.streams.ZLinkStreamSessionError, java.util.Optional<systems.zlink.framework.streams.ZLinkStreamDiagnostic>);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.streams.ZLinkStreamSessionError error();
  public java.util.Optional<systems.zlink.framework.streams.ZLinkStreamDiagnostic> diagnostic();
}
public final class systems.zlink.framework.streams.ZLinkStreamSessionError extends java.lang.Enum<systems.zlink.framework.streams.ZLinkStreamSessionError> {
  public static final systems.zlink.framework.streams.ZLinkStreamSessionError INTERNAL;
  public static final systems.zlink.framework.streams.ZLinkStreamSessionError TRANSPORT_ERROR;
  public static systems.zlink.framework.streams.ZLinkStreamSessionError[] values();
  public static systems.zlink.framework.streams.ZLinkStreamSessionError valueOf(java.lang.String);
  public int value();
}
public interface systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler<TSessionContext extends systems.zlink.framework.streams.ZLinkSessionContext, TMessage> {
  public abstract java.lang.Class<TMessage> messageType();
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> handle(TSessionContext, systems.zlink.framework.streams.ZLinkSessionDispatchContext, TMessage);
}
```

Payload만 받는 `relay(...)`는 one-way admission이다. Dispatch context를 받는 overload는 explicit current
STREAM request reply capability를 호출 즉시 runtime에 이전한다. Submitted면 Actor typed reply가 original
STREAM correlation을 terminal-once로 완료하고 admission failure면 Framework가 같은 correlation을 typed
failure로 완료한다. Caller는 별도 reply·retry를 하지 않는다. One-way dispatch context는 reply
capability가 없으므로 admission만 반환한다. Handshake failure는 session 생성 전 runtime monitoring에만
기록되며 `onError(...)`에 전달하지 않는다.

## STREAM codec public signature

```java
public final class systems.zlink.framework.streams.ZLinkSessionDispatchContext extends java.lang.Record {
  public systems.zlink.framework.streams.ZLinkSessionDispatchContext(java.lang.String, java.util.Map<java.lang.String, java.lang.String>, boolean);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String packetName();
  public java.util.Map<java.lang.String, java.lang.String> metadata();
  public boolean canReply();
}
public final class systems.zlink.framework.streams.ZLinkStreamCodec extends java.lang.Enum<systems.zlink.framework.streams.ZLinkStreamCodec> {
  public static final systems.zlink.framework.streams.ZLinkStreamCodec RAW;
  public static final systems.zlink.framework.streams.ZLinkStreamCodec JSON;
  public static final systems.zlink.framework.streams.ZLinkStreamCodec MESSAGE_PACK;
  public static final systems.zlink.framework.streams.ZLinkStreamCodec PROTOBUF;
  public static systems.zlink.framework.streams.ZLinkStreamCodec[] values();
  public static systems.zlink.framework.streams.ZLinkStreamCodec valueOf(java.lang.String);
  public int value();
  public static systems.zlink.framework.streams.ZLinkStreamCodec fromValue(int);
}
public final class systems.zlink.framework.streams.ZLinkStreamCompressionCodecs {
  public static systems.zlink.framework.streams.ZLinkStreamCompressionCodec lz4();
}
public final class systems.zlink.framework.streams.ZLinkStreamMessageKind extends java.lang.Enum<systems.zlink.framework.streams.ZLinkStreamMessageKind> {
  public static final systems.zlink.framework.streams.ZLinkStreamMessageKind SEND;
  public static final systems.zlink.framework.streams.ZLinkStreamMessageKind REQUEST;
  public static final systems.zlink.framework.streams.ZLinkStreamMessageKind RESPONSE;
  public static final systems.zlink.framework.streams.ZLinkStreamMessageKind ERROR;
  public static final systems.zlink.framework.streams.ZLinkStreamMessageKind CONTROL;
  public static systems.zlink.framework.streams.ZLinkStreamMessageKind[] values();
  public static systems.zlink.framework.streams.ZLinkStreamMessageKind valueOf(java.lang.String);
  public int value();
  public static systems.zlink.framework.streams.ZLinkStreamMessageKind fromValue(int);
}
```
