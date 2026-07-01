package systems.zlink.e2e.pubsub.client.Scenarios;

import systems.zlink.e2e.pubsub.client.Support.ScenarioAssert;
import systems.zlink.e2e.pubsub.client.Support.ScenarioContext;
import systems.zlink.e2e.pubsub.shared.Contracts;

public final class PublisherRestartScenario {
    private PublisherRestartScenario() {
    }

    public static void run(ScenarioContext context) {
        try (var publisher = context.processes().startPublisher("publisher-baseline")) {
            context.publisher().publish("all", new Contracts.EventMsg("ps-b2", 1, "before-publisher-restart"));
            ScenarioAssert.waitForEvent(context.evidence(), "sub-1", "ps-b2", 1);
            ScenarioAssert.waitForEvent(context.evidence(), "sub-2", "ps-b2", 1);
            ScenarioAssert.waitForEvent(context.evidence(), "sub-3", "ps-b2", 1);
        }

        context.processes().waitStopped("publisher", context.options().publisherHttp());
        ScenarioAssert.expectPublishFailure(
            () -> context.publisher().publish("all", new Contracts.EventMsg("ps-b2", 2, "during-publisher-down")),
            "PS-B2 expected publish to fail while publisher process is down");

        try (var restarted = context.processes().startPublisher("publisher-restarted")) {
            for (int sequence = 3; sequence <= 42; sequence++) {
                context.publisher().publish(
                    "all",
                    new Contracts.EventMsg(
                        "ps-b2",
                        sequence,
                        "after-publisher-restart-" + sequence));
                ScenarioAssert.sleep(100);
            }
            for (int sequence = 20; sequence <= 42; sequence++) {
                if (hasRestartEvidence(context, sequence)) {
                    System.out.println("scenario PS-B2 passed");
                    return;
                }
            }
        }
        throw new IllegalStateException("PS-B2 subscribers did not receive post-restart publisher events");
    }

    private static boolean hasRestartEvidence(ScenarioContext context, int sequence) {
        return ScenarioAssert.hasEvent(context.evidence().snapshot("sub-1"), "ps-b2", sequence)
            && ScenarioAssert.hasEvent(context.evidence().snapshot("sub-2"), "ps-b2", sequence)
            && ScenarioAssert.hasEvent(context.evidence().snapshot("sub-3"), "ps-b2", sequence);
    }
}
