package systems.zlink.e2e.kotlin.yielddispatch.support;

import java.net.URI;
import java.time.Duration;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.Env;
import systems.zlink.stream.connector.ZLinkStreamCompression;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.stream.connector.ZLinkStreamRequestCall;
import systems.zlink.stream.connector.ZLinkStreamSendCall;

public final class ClientStreamSupport {
    public static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(15);

    private ClientStreamSupport() {
    }

    public static ZLinkStreamConnector createConnector() {
        return ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
            URI.create(Env.get("ZLINK_KOTLIN_E2E_STREAM_ENDPOINT")),
            ZLinkStreamDispatchMode.AUTO,
            REQUEST_TIMEOUT,
            2,
            Duration.ofSeconds(5),
            64 * 1024,
            true,
            Duration.ofSeconds(1),
            Duration.ofSeconds(5),
            false,
            Duration.ofMillis(250),
            Duration.ofSeconds(5),
            2.0,
            false,
            ZLinkStreamCompression.LZ4));
    }

    public static Contracts.ActorJoinReply joinActor(
        ZLinkStreamConnector connector,
        String actorId,
        String spotRid,
        String value) {
        return joinActor(connector, actorId, spotRid, value, 0);
    }

    public static Contracts.ActorJoinReply joinActor(
        ZLinkStreamConnector connector,
        String actorId,
        String spotRid,
        String value,
        long millis) {
        Contracts.ActorAuthReply auth = await(
            connector.request(new Contracts.ActorAuthRequest(actorId)),
            Contracts.ActorAuthReply.class);
        ScenarioAssert.that(actorId.equals(auth.actorId()), "actor auth mismatch");
        return await(
            connector.request(new Contracts.ActorJoinRequest(spotRid, value, millis))
                .metadata("actor-id", actorId),
            Contracts.ActorJoinReply.class);
    }

    public static Contracts.ProbeReply request(
        ZLinkStreamConnector connector,
        String actorId,
        String op,
        long millis) {
        return await(
            connector.request(new Contracts.ProbeRequest(op, millis))
                .metadata("actor-id", actorId)
                .timeout(REQUEST_TIMEOUT),
            Contracts.ProbeReply.class);
    }

    public static void send(
        ZLinkStreamConnector connector,
        Object message) {
        send(connector.send(message));
    }

    public static void send(ZLinkStreamSendCall call) {
        try {
            call.await();
        } catch (Exception error) {
            throw new IllegalStateException("stream send failed", error);
        }
    }

    public static Contracts.EvidenceReply evidence(
        ZLinkStreamConnector connector,
        String requestId) {
        return await(
            connector.request(new Contracts.EvidenceRequest(requestId))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, "room-a")
                .timeout(REQUEST_TIMEOUT),
            Contracts.EvidenceReply.class);
    }

    public static Contracts.EvidenceReply waitForEvidence(
        ZLinkStreamConnector connector,
        String requestId,
        String marker) {
        long deadline = System.nanoTime() + Duration.ofSeconds(10).toNanos();
        Contracts.EvidenceReply latest = new Contracts.EvidenceReply(requestId, java.util.List.of());
        while (System.nanoTime() < deadline) {
            latest = evidence(connector, requestId);
            if (latest.markers().stream().anyMatch(entry -> entry.startsWith(marker + "|"))) {
                return latest;
            }
            sleep(50);
        }
        throw new IllegalStateException(
            "timed out waiting for evidence marker " + marker + " for request " + requestId
                + ": " + latest.markers());
    }

    public static <T> T await(ZLinkStreamRequestCall call, Class<T> replyType) {
        try {
            return call.await(replyType);
        } catch (Exception error) {
            throw new IllegalStateException("stream request failed", error);
        }
    }

    public static void awaitLifecycle(ScenarioAssert.CheckedRunnable action) {
        ScenarioAssert.lifecycle(action);
    }

    public static void sleep(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted", error);
        }
    }
}
