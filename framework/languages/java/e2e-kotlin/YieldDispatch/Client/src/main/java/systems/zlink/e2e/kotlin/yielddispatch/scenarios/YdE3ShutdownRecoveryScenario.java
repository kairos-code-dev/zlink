package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.Env;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdE3ShutdownRecoveryScenario {
    private YdE3ShutdownRecoveryScenario() {
    }

    public static void run(
        ZLinkStreamConnector connector,
        ZLinkStreamConnector recoveryConnector) {
        String requestId = "YD-E3-" + UUID.randomUUID();
        Path controlDir = Path.of(Env.get("ZLINK_KOTLIN_E2E_CONTROL_DIR"));
        CompletableFuture<Contracts.ScenarioRes> pending = connector
            .request(new Contracts.ShutdownYieldReq(requestId, 5000))
            .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
            .metadata(Contracts.SPOT_RID_METADATA, "room-a")
            .timeout(Duration.ofSeconds(20))
            .submit(Contracts.ScenarioRes.class)
            .toCompletableFuture();

        ClientStreamSupport.waitForEvidence(connector, requestId, "shutdown-yield-released");
        touch(controlDir.resolve("yd-e3-ready-to-stop-play"));
        expectPublicFailure(pending);
        waitForFile(controlDir.resolve("yd-e3-play-restarted"));

        Contracts.YieldShutdownRecoveryRes recovery = requestRecovery(recoveryConnector, requestId);
        ScenarioAssert.that(
            "yield.e3-shutdown-recovery".equals(recovery.operation()),
            "YD-E3 recovery operation mismatch");
        ScenarioAssert.that("room-a".equals(recovery.spotRid()), "YD-E3 recovery spot mismatch");
        ScenarioAssert.that(
            recovery.evidence().stream().anyMatch(entry ->
                entry.startsWith("probe-completed|") && entry.contains("marker=shutdown-recovery-probe")),
            "YD-E3 recovery probe marker missing: " + recovery.evidence());
        System.out.println("scenario YD-E3 passed");
    }

    private static Contracts.YieldShutdownRecoveryRes requestRecovery(
        ZLinkStreamConnector recovery,
        String requestId) {
        return ClientStreamSupport.await(
            recovery.request(new Contracts.YieldShutdownRecoveryReq(requestId, "room-a"))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .timeout(Duration.ofSeconds(95)),
            Contracts.YieldShutdownRecoveryRes.class);
    }

    private static void expectPublicFailure(CompletableFuture<Contracts.ScenarioRes> pending) {
        try {
            pending.get(20, TimeUnit.SECONDS);
        } catch (Exception expected) {
            return;
        }
        throw new IllegalStateException("YD-E3 pending yield request unexpectedly succeeded");
    }

    private static void touch(Path path) {
        try {
            Files.createDirectories(path.getParent());
            Files.writeString(path, "ready\n");
        } catch (Exception error) {
            throw new IllegalStateException("failed to write YD-E3 control file " + path, error);
        }
    }

    private static void waitForFile(Path path) {
        long deadline = System.nanoTime() + Duration.ofSeconds(20).toNanos();
        while (System.nanoTime() < deadline) {
            if (Files.exists(path)) {
                return;
            }
            ClientStreamSupport.sleep(100);
        }
        throw new IllegalStateException("timed out waiting for YD-E3 control file " + path);
    }
}
