package systems.zlink.e2e.registrationcodec.client.Scenarios;

import java.time.Duration;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioContext;
import systems.zlink.e2e.registrationcodec.shared.Contracts;

public final class RcA5FilterOrderingScenario {
    private static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(5);

    private RcA5FilterOrderingScenario() {
    }

    public static void run(ScenarioContext context) {
        Contracts.EchoReply filtered = context.client().requestToChannel(
                Contracts.CHANNEL,
                new Contracts.EchoManualRequest("filter-order-request"))
            .packetName("EchoManual")
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.EchoReply.class);
        ScenarioAssert.ensure("echo:filter-order-request".equals(filtered.value()), "RC-A5 request mismatch");
        ScenarioAssert.waitForFilterOrder(context.evidence(), "filter-order-request");
        System.out.println("scenario RC-A5 passed");
    }
}
