package systems.zlink.e2e.registrationcodec.client.Scenarios;

import java.time.Duration;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioContext;
import systems.zlink.e2e.registrationcodec.shared.Contracts;

public final class RcB1JsonCodecScenario {
    private static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(5);

    private RcB1JsonCodecScenario() {
    }

    public static void run(ScenarioContext context) {
        Contracts.EchoReply jsonReply = context.client().requestToChannel(
                Contracts.CHANNEL,
                new Contracts.JsonEchoRequest("json-request"))
            .packetName("JsonEcho")
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.EchoReply.class);
        ScenarioAssert.ensure("echo:json-request".equals(jsonReply.value()) && "json".equals(jsonReply.handler()),
            "RC-B1 request mismatch");
        context.client().sendToChannel(Contracts.CHANNEL, new Contracts.JsonEchoCommand("json-send"))
            .packetName("JsonEcho")
            .await();
        ScenarioAssert.waitForEvidence(context.evidence(), "Send", "JsonEcho", "json-send");
        System.out.println("scenario RC-B1 passed");
    }
}
