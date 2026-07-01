package systems.zlink.e2e.registrationcodec.client.Scenarios;

import systems.zlink.e2e.registrationcodec.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioContext;
import systems.zlink.e2e.registrationcodec.shared.Contracts;

public final class CodecMismatchScenario {
    private CodecMismatchScenario() {
    }

    public static void run(ScenarioContext context) {
        Contracts.CodecMismatchProbeRes mismatch = context.codecRequester()
            .post("/codec/protobuf/request")
            .fetch(Contracts.CodecMismatchProbeRes.class);
        ScenarioAssert.ensure(mismatch.rejected(), "RC-B5 Protobuf mismatch was not rejected");
        Contracts.EchoRes jsonReply = context.codecRequester()
            .post("/codec/json/request")
            .fetch(Contracts.EchoRes.class);
        ScenarioAssert.ensure("echo:rc-b5-json".equals(jsonReply.value()),
            "RC-B5 JSON traffic did not recover after mismatch");
        System.out.println("scenario RC-B5 passed");
    }
}
