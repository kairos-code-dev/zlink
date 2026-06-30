package systems.zlink.e2e.pubsub.client.Scenarios;

import systems.zlink.e2e.pubsub.client.Support.ScenarioAssert;
import systems.zlink.e2e.pubsub.client.Support.ScenarioContext;
import systems.zlink.e2e.pubsub.shared.Contracts;

public final class PublisherRestartScenario {
    private PublisherRestartScenario() {
    }

    public static void run(ScenarioContext context) {
        context.publisher().publish("all", new Contracts.EventMsg("ps-b2", 1, "after-publisher-restart"));
        ScenarioAssert.waitForEvent(context.evidence(), "sub-1", "ps-b2", 1);
        ScenarioAssert.waitForEvent(context.evidence(), "sub-2", "ps-b2", 1);
        ScenarioAssert.waitForEvent(context.evidence(), "sub-3", "ps-b2", 1);
        System.out.println("scenario PS-B2 passed");
    }
}
