package systems.zlink.e2e.registrationcodec.client.Scenarios;

import systems.zlink.e2e.registrationcodec.client.Support.ScenarioContext;
import systems.zlink.e2e.registrationcodec.shared.Contracts;

public final class RcB4CodecCoexistenceScenario {
    private RcB4CodecCoexistenceScenario() {
    }

    public static void run(ScenarioContext context) {
        context.server().post("/codec/roundtrip").fetch(Contracts.CodecScenarioRes.class);
        System.out.println("scenario RC-B4 passed");
    }
}
