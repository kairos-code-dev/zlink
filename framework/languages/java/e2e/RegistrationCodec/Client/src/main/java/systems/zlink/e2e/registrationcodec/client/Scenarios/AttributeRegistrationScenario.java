package systems.zlink.e2e.registrationcodec.client.Scenarios;

import java.time.Duration;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioContext;
import systems.zlink.e2e.registrationcodec.shared.Contracts;

public final class AttributeRegistrationScenario {
    private static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(5);

    private AttributeRegistrationScenario() {
    }

    public static void run(ScenarioContext context) {
        Contracts.EchoReply attr = context.client().requestToChannel(
                Contracts.CHANNEL,
                new Contracts.EchoAttrRequest("attr-request"))
            .packetName("EchoAttr")
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.EchoReply.class);
        ScenarioAssert.ensure("echo:attr-request".equals(attr.value()) && "attr".equals(attr.handler()),
            "RC-A2 request mismatch");
        context.client().sendToChannel(Contracts.CHANNEL, new Contracts.EchoAttrCommand("attr-send"))
            .packetName("EchoAttr")
            .await();
        ScenarioAssert.waitForEvidence(context.evidence(), "Send", "EchoAttr", "attr-send");
        System.out.println("scenario RC-A2 passed");
    }
}
