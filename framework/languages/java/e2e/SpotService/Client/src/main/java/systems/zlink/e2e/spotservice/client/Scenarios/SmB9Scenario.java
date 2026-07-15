package systems.zlink.e2e.spotservice.client.Scenarios;

import java.util.UUID;

public final class SmB9Scenario extends SpotServiceScenarioContext {
    private SmB9Scenario(SpotServiceScenarioContext context) {
        super(context);
    }

    public static void run(SpotServiceScenarioContext context) {
        new SmB9Scenario(context).execute();
    }

    private void execute() {
        verifyActorJoinAdmission(
            options().httpAEndpoint(),
            "play-a",
            "spot-sm-b9-local-" + UUID.randomUUID().toString().replace("-", ""),
            "actor-sm-b9-local-" + UUID.randomUUID().toString().replace("-", ""));
        verifyActorJoinAdmission(
            options().httpBEndpoint(),
            "play-b",
            "spot-sm-b9-remote-" + UUID.randomUUID().toString().replace("-", ""),
            "actor-sm-b9-remote-" + UUID.randomUUID().toString().replace("-", ""));
        System.out.println("scenario SM-B9 passed");

    }
}
