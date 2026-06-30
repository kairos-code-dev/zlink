package systems.zlink.e2e.registrationcodec.client.Scenarios;

import java.time.Duration;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioContext;
import systems.zlink.e2e.registrationcodec.shared.Contracts;

public final class CodecMismatchScenario {
    private static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(5);

    private CodecMismatchScenario() {
    }

    public static void run(ScenarioContext context) {
        Contracts.EchoReply jsonReply = context.client().requestToChannel(
                Contracts.CHANNEL,
                new Contracts.JsonEchoRequest("json-after-mismatch"))
            .packetName("JsonEcho")
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.EchoReply.class);
        ScenarioAssert.ensure("echo:json-after-mismatch".equals(jsonReply.value()),
            "RC-B5 JSON baseline failed");

        try {
            Contracts.PackedEchoReply mismatchReply = context.client().requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.PackedEchoRequest("msgpack-mismatch"))
                .packetName("MsgpackEcho")
                .timeout(REQUEST_TIMEOUT)
                .await(Contracts.PackedEchoReply.class);
            ScenarioAssert.ensure("echo:msgpack-mismatch".equals(mismatchReply.value()),
                "RC-B5 fallback reply mismatch: " + mismatchReply.value());
        } catch (RuntimeException expected) {
            System.out.println("scenario RC-B5 mismatch ended with public error: "
                + expected.getClass().getSimpleName());
        }

        Contracts.EchoReply secondJson = context.client().requestToChannel(
                Contracts.CHANNEL,
                new Contracts.JsonEchoRequest("json-still-ok"))
            .packetName("JsonEcho")
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.EchoReply.class);
        ScenarioAssert.ensure("echo:json-still-ok".equals(secondJson.value()),
            "RC-B5 JSON traffic did not recover after mismatch");
        System.out.println("scenario RC-B5 passed");
    }
}
