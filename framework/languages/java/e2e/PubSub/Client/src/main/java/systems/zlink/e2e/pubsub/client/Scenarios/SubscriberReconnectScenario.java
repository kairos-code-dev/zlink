package systems.zlink.e2e.pubsub.client.Scenarios;

import systems.zlink.e2e.pubsub.client.Support.ScenarioAssert;
import systems.zlink.e2e.pubsub.client.Support.ScenarioContext;
import systems.zlink.e2e.pubsub.shared.Contracts;

public final class SubscriberReconnectScenario {
    private SubscriberReconnectScenario() {
    }

    public static void run(ScenarioContext context) {
        try (var subscriber = context.processes().startSubscriber(
            "sub-1",
            "alpha",
            context.options().sub1Http())) {
            context.publisher().publish("all", new Contracts.EventMsg("ps-a4-before", 0, "before-disconnect"));
            ScenarioAssert.waitForEvent(context.evidence(), "sub-1", "ps-a4-before", 0);
        }

        context.processes().waitStopped("sub-1", context.options().sub1Http());
        context.publisher().publish("all", new Contracts.EventMsg("ps-a4-down", 1, "while-sub-1-down"));
        ScenarioAssert.waitForEvent(context.evidence(), "sub-2", "ps-a4-down", 1);

        try (var restarted = context.processes().startSubscriber(
            "sub-1",
            "alpha",
            context.options().sub1Http())) {
            context.publisher().publish("all", new Contracts.EventMsg("ps-a4-after", 2, "after-sub-1-restart"));
            ScenarioAssert.waitForEvent(context.evidence(), "sub-1", "ps-a4-after", 2);
            ScenarioAssert.waitForEvent(context.evidence(), "sub-2", "ps-a4-after", 2);
            Contracts.EvidenceSnapshot restartedEvidence = context.evidence().snapshot("sub-1");
            ScenarioAssert.ensure(
                !ScenarioAssert.hasEvent(restartedEvidence, "ps-a4-down", 1),
                "PS-A4 restarted subscriber received event from disconnected interval");
        }
        System.out.println("scenario PS-A4 passed");
    }
}
