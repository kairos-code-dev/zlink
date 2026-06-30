package systems.zlink.e2e.pubsub.client.Scenarios;

import systems.zlink.e2e.pubsub.client.Support.ScenarioAssert;
import systems.zlink.e2e.pubsub.client.Support.ScenarioContext;
import systems.zlink.e2e.pubsub.shared.Contracts;

public final class SubscriberReconnectScenario {
    private SubscriberReconnectScenario() {
    }

    public static void run(ScenarioContext context) {
        context.publisher().publish("all", new Contracts.EventNotify("ps-a4-down", 1, "while-sub-1-down"));
        ScenarioAssert.waitForEvent(context.evidence(), "sub-2", "ps-a4-down", 1);

        ScenarioAssert.waitForFile(context.options().lateContinueFile());

        context.publisher().publish("all", new Contracts.EventNotify("ps-a4-after", 2, "after-sub-1-restart"));
        ScenarioAssert.waitForEvent(context.evidence(), "sub-1", "ps-a4-after", 2);
        ScenarioAssert.waitForEvent(context.evidence(), "sub-2", "ps-a4-after", 2);
        Contracts.EvidenceSnapshot restarted = context.evidence().snapshot("sub-1");
        ScenarioAssert.ensure(
            !ScenarioAssert.hasEvent(restarted, "ps-a4-down", 1),
            "PS-A4 restarted subscriber received event from disconnected interval");
        System.out.println("scenario PS-A4 passed");
    }
}
