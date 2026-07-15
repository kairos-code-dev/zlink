package systems.zlink.e2e.spotservice.client.Scenarios;

import java.time.Duration;
import java.util.List;
import java.util.UUID;
import systems.zlink.e2e.spotservice.shared.Contracts;
import systems.zlink.e2e.spotservice.shared.Env;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class SmB5Scenario extends SpotServiceScenarioContext {
    private SmB5Scenario(SpotServiceScenarioContext context) {
        super(context);
    }

    public static void run(SpotServiceScenarioContext context) {
        new SmB5Scenario(context).execute();
    }

    private void execute() {
        String actorId = "actor-sm-b5-missing-" + UUID.randomUUID().toString().replace("-", "");
        ZLinkStreamConnector connector = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        Contracts.ActorProfile profile = new Contracts.ActorProfile("Missing", 5, List.of("missing"));
        try {
            connector.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes auth = connector
                .request(new Contracts.ActorAuthReq(actorId, profile))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(actorId.equals(auth.actorId()), "SM-B5 auth actor mismatch");

            expectFailure(() -> {
                try {
                    connector
                        .request(new Contracts.MissingActorReq("missing-handler"))
                        .metadata("actor-id", actorId)
                        .timeout(Duration.ofSeconds(2))
                        .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
                } catch (Exception error) {
                    throw new RuntimeException(error);
                }
            });

            Contracts.EvidenceSnapshot evidence = waitForPlayAEvidence(List.of(
                "HANDLER_MISSING/REPLY_ERROR/MissingActorReq"));
            ensure(evidence.entries().stream().anyMatch(entry ->
                    "DispatchError".equals(entry.marker())
                        && "HANDLER_MISSING/REPLY_ERROR/MissingActorReq".equals(entry.value())),
                "SM-B5 missing actor dispatch error evidence mismatch");

            System.out.println("scenario SM-B5 passed");
        } catch (Exception error) {
            throw new IllegalStateException("actor missing handler scenario failed", error);
        } finally {
            closeQuietly(connector);
        }

    }
}
