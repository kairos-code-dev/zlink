package systems.zlink.e2e.registrationcodec.client.Scenarios;

import java.time.Duration;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioContext;
import systems.zlink.e2e.registrationcodec.shared.Contracts;

public final class AutoRegistrationScenario {
    private static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(5);

    private AutoRegistrationScenario() {
    }

    public static void run(ScenarioContext context) {
        Contracts.EchoRes auto = context.client().requestToChannel(
                Contracts.CHANNEL,
                new Contracts.EchoAutoReq("auto-request"))
            .packetName("EchoAuto")
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.EchoRes.class);
        ScenarioAssert.ensure("echo:auto-request".equals(auto.value()) && "auto".equals(auto.handler()),
            "RC-A1 request mismatch");
        context.client().sendToChannel(Contracts.CHANNEL, new Contracts.EchoAutoMsg("auto-send"))
            .packetName("EchoAuto")
            .await();
        ScenarioAssert.waitForEvidence(context.evidence(), "Send", "EchoAuto", "auto-send");
        System.out.println("scenario RC-A1 passed");
    }
}
