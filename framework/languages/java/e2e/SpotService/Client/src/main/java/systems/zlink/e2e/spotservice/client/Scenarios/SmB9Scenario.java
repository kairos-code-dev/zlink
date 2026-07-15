package systems.zlink.e2e.spotservice.client.Scenarios;

import java.util.UUID;
import systems.zlink.e2e.spotservice.shared.Env;

public final class SmB9Scenario extends SpotServiceScenarioContext {
    private SmB9Scenario(SpotServiceScenarioContext context) {
        super(context);
    }

    public static void run(SpotServiceScenarioContext context) {
        new SmB9Scenario(context).execute();
    }

    private void execute() {
        verifyActorJoinAdmission(
            Env.get("ZLINK_JAVA_E2E_HTTP_A_ENDPOINT"),
            "play-a",
            "spot-sm-b9-local-" + UUID.randomUUID().toString().replace("-", ""),
            "actor-sm-b9-local-" + UUID.randomUUID().toString().replace("-", ""));
        verifyActorJoinAdmission(
            Env.get("ZLINK_JAVA_E2E_HTTP_B_ENDPOINT"),
            "play-b",
            "spot-sm-b9-remote-" + UUID.randomUUID().toString().replace("-", ""),
            "actor-sm-b9-remote-" + UUID.randomUUID().toString().replace("-", ""));
        System.out.println("scenario SM-B9 passed");

    }
}
