package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.UUID;
import java.util.concurrent.CompletionStage;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamMessage;

public final class YdD4SessionRelayActorYieldScenario {
    private YdD4SessionRelayActorYieldScenario() {
    }

    public static void run(
        ZLinkStreamConnector connector,
        String actorId,
        ZLinkStreamConnector unbound) throws Exception {
        String requestId = "YD-D4-" + UUID.randomUUID().toString().replace("-", "");
        CompletionStage<ZLinkStreamMessage<Contracts.ActorPushNotify>> push = connector
            .waitFor(Contracts.ActorPushNotify.class)
            .timeout(ClientStreamSupport.REQUEST_TIMEOUT)
            .submit(Contracts.ActorPushNotify.class);
        Contracts.ActorRes reply = ClientStreamSupport.await(
            connector.request(new Contracts.ActorPushYieldReq(requestId, 350, "bound-session-push"))
                .metadata("actor-id", actorId)
                .timeout(ClientStreamSupport.REQUEST_TIMEOUT),
            Contracts.ActorRes.class);
        Contracts.ActorPushNotify notify = connector.await(push).payload();
        ScenarioAssert.that("YD-D4".equals(reply.scenarioId()), "YD-D4 reply scenario mismatch");
        ScenarioAssert.that(actorId.equals(reply.actorId()), "YD-D4 reply actor mismatch");
        ScenarioAssert.that("actor-push-yield-completed".equals(reply.marker()), "YD-D4 reply marker mismatch");
        ScenarioAssert.that(actorId.equals(notify.actorId()), "YD-D4 push actor mismatch");
        ScenarioAssert.that(requestId.equals(notify.requestId()), "YD-D4 push request mismatch");
        ScenarioAssert.that("bound-session-push".equals(notify.value()), "YD-D4 push value mismatch");
        ClientStreamSupport.sleep(150);
        ScenarioAssert.that(unbound.receivedCount("ActorPushNotify") == 0,
            "YD-D4 unbound session received actor push");
        Contracts.EvidenceRes evidence = ClientStreamSupport.evidence(connector, requestId);
        ScenarioAssert.containsMarkersInOrder(evidence.markers(),
            "actor-push-yield-started",
            "actor-push-yield-released",
            "actor-push-yield-resumed",
            "actor-push-yield-completed");
        System.out.println("scenario YD-D4 passed");
    }
}
