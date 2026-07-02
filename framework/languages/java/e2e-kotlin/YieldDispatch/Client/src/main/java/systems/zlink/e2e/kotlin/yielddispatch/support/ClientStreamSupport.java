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

    public static Contracts.ActorJoinRes joinActor(
        ZLinkStreamConnector connector,
        String actorId,
        String spotRid,
        String value) {
        return joinActor(connector, actorId, spotRid, value, 0);
    }

    public static Contracts.ActorJoinRes joinActor(
        ZLinkStreamConnector connector,
        String actorId,
        String spotRid,
        String value,
        long millis) {
        return await(
            connector.request(new Contracts.ActorJoinReq(spotRid, value, millis))
                .metadata("actor-id", actorId),
            Contracts.ActorJoinRes.class);
    }

    public static Contracts.BindActorsRes bindActors(
        ZLinkStreamConnector connector,
        String spotRid,
        String actorA,
        String actorB) {
        return await(
            connector.request(new Contracts.BindActorsReq(spotRid, actorA, actorB))
                .timeout(REQUEST_TIMEOUT),
            Contracts.BindActorsRes.class);
    }

    public static Contracts.ProbeRes request(
        ZLinkStreamConnector connector,
        String actorId,
        String op,
        long millis) {
        return request(connector, actorId, "room-a", op, millis);
    }

    public static Contracts.ProbeRes request(
        ZLinkStreamConnector connector,
        String actorId,
        String spotRid,
        String op,
        long millis) {
        return await(
            connector.request(new Contracts.ProbeReq(op, millis))
                .metadata("actor-id", actorId)
                .metadata(Contracts.SPOT_RID_METADATA, spotRid)
                .timeout(REQUEST_TIMEOUT),
            Contracts.ProbeRes.class);
    }

    public static void send(
        ZLinkStreamConnector connector,
        Object message) {
        send(connector.send(message));
    }

    public static void send(ZLinkStreamSendCall call) {
        try {
            call.submit().toCompletableFuture().join();
        } catch (Exception error) {
            throw new IllegalStateException("stream send failed", error);
        }
    }

    public static Contracts.EvidenceRes evidence(
        ZLinkStreamConnector connector,
        String requestId) {
        return evidence(connector, requestId, "room-a");
    }

    public static Contracts.EvidenceRes evidence(
        ZLinkStreamConnector connector,
        String requestId,
        String spotRid) {
        return await(
            connector.request(new Contracts.EvidenceReq(requestId))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, spotRid)
                .timeout(REQUEST_TIMEOUT),
            Contracts.EvidenceRes.class);
    }

    public static Contracts.EvidenceRes waitForEvidence(
        ZLinkStreamConnector connector,
        String requestId,
        String marker) {
        return waitForEvidence(connector, requestId, "room-a", marker);
    }

    public static Contracts.EvidenceRes waitForEvidence(
        ZLinkStreamConnector connector,
        String requestId,
        String spotRid,
        String marker) {
        long deadline = System.nanoTime() + Duration.ofSeconds(10).toNanos();
        Contracts.EvidenceRes latest = new Contracts.EvidenceRes(requestId, java.util.List.of());
        while (System.nanoTime() < deadline) {
            latest = evidence(connector, requestId, spotRid);
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
