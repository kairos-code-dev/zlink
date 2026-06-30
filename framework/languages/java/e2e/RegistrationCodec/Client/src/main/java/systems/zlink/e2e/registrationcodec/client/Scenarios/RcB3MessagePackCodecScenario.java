package systems.zlink.e2e.registrationcodec.client.Scenarios;

import java.time.Duration;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioContext;
import systems.zlink.e2e.registrationcodec.shared.Contracts;

public final class RcB3MessagePackCodecScenario {
    private static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(5);

    private RcB3MessagePackCodecScenario() {
    }

    public static void run(ScenarioContext context) {
        Contracts.PackedEchoRes packedReply = context.client().requestToChannel(
                Contracts.CHANNEL,
                new Contracts.PackedEchoReq("msgpack-request"))
            .packetName("MsgpackEcho")
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.PackedEchoRes.class);
        ScenarioAssert.ensure("echo:msgpack-request".equals(packedReply.value()),
            "RC-B3 request mismatch");
        context.client().sendToChannel(Contracts.CHANNEL, new Contracts.PackedEchoMsg("msgpack-send"))
            .packetName("MsgpackEcho")
            .await();
        ScenarioAssert.waitForEvidence(context.evidence(), "Send", "MsgpackEcho", "msgpack-send");
        System.out.println("scenario RC-B3 passed");
    }
}
