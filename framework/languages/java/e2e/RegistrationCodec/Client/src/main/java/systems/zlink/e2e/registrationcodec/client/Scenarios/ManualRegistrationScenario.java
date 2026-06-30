package systems.zlink.e2e.registrationcodec.client.Scenarios;

import java.time.Duration;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioContext;
import systems.zlink.e2e.registrationcodec.shared.Contracts;

public final class ManualRegistrationScenario {
    private static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(5);

    private ManualRegistrationScenario() {
    }

    public static void run(ScenarioContext context) {
        Contracts.EchoReply manual = context.client().requestToChannel(
                Contracts.CHANNEL,
                new Contracts.EchoManualRequest("manual-request"))
            .packetName("EchoManual")
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.EchoReply.class);
        ScenarioAssert.ensure("echo:manual-request".equals(manual.value()) && "manual".equals(manual.handler()),
            "RC-A3 request mismatch");
        context.client().sendToChannel(Contracts.CHANNEL, new Contracts.EchoManualCommand("manual-send"))
            .packetName("EchoManual")
            .await();
        ScenarioAssert.waitForEvidence(context.evidence(), "Send", "EchoManual", "manual-send");
        System.out.println("scenario RC-A3 passed");
    }
}
