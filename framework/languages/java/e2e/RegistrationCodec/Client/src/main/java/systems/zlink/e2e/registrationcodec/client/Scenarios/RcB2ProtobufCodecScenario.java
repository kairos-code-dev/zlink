package systems.zlink.e2e.registrationcodec.client.Scenarios;

import com.google.protobuf.StringValue;
import java.time.Duration;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioContext;
import systems.zlink.e2e.registrationcodec.shared.Contracts;

public final class RcB2ProtobufCodecScenario {
    private static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(5);

    private RcB2ProtobufCodecScenario() {
    }

    public static void run(ScenarioContext context) {
        StringValue protobufReply = context.client().requestToChannel(
                Contracts.CHANNEL,
                StringValue.of("protobuf-request"))
            .packetName("ProtobufEcho")
            .timeout(REQUEST_TIMEOUT)
            .await(StringValue.class);
        ScenarioAssert.ensure("echo:protobuf-request".equals(protobufReply.getValue()),
            "RC-B2 request mismatch");
        context.client().sendToChannel(Contracts.CHANNEL, StringValue.of("protobuf-send"))
            .packetName("ProtobufEcho")
            .await();
        ScenarioAssert.waitForEvidence(context.evidence(), "Send", "ProtobufEcho", "protobuf-send");
        System.out.println("scenario RC-B2 passed");
    }
}
