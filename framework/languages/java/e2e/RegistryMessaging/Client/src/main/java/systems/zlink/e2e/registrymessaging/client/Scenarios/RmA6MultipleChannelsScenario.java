package systems.zlink.e2e.registrymessaging.client.Scenarios;

import systems.zlink.e2e.registrymessaging.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class RmA6MultipleChannelsScenario {
    private RmA6MultipleChannelsScenario() {
    }

    public static void run(
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB,
        ZLinkHttpClient workflow) {
        String profileMarker = "a6-api-" + java.util.UUID.randomUUID();
        String workflowMarker = "a6-workflow-" + java.util.UUID.randomUUID();
        Contracts.ProfileRes api = providerA.post("/profile/request")
            .body(new Contracts.ProfileReq(profileMarker))
            .fetch(Contracts.ProfileRes.class);
        ScenarioAssert.that(("profile:" + profileMarker).equals(api.value()), "RM-A6 api reply mismatch");
        ScenarioAssert.that(api.providerRid().startsWith("api-"), "RM-A6 api resolved wrong provider");

        Contracts.WorkflowRes workflowReply = workflow.post("/workflow/request")
            .body(new Contracts.WorkflowReq(workflowMarker))
            .fetch(Contracts.WorkflowRes.class);
        ScenarioAssert.that(("workflow:" + workflowMarker).equals(workflowReply.value()),
            "RM-A6 workflow reply mismatch");
        ScenarioAssert.that("workflow-a".equals(workflowReply.providerRid()), "RM-A6 workflow resolved wrong provider");
        String[] providerEvidence = ScenarioAssert.waitAnyEvidence(providerA, providerB, profileMarker);
        String[] workflowEvidence = ScenarioAssert.waitEvidence(workflow, workflowMarker);
        ScenarioAssert.that(java.util.Arrays.stream(providerEvidence)
                .anyMatch(line -> line.contains("ProfileReq") && line.contains(profileMarker)),
            "RM-A6 profile evidence missing");
        ScenarioAssert.that(java.util.Arrays.stream(workflowEvidence)
                .anyMatch(line -> line.contains("workflow-request") && line.contains(workflowMarker)),
            "RM-A6 workflow evidence missing");
        String[] allProviderEvidence = ScenarioAssert.concat(
            ScenarioAssert.evidence(providerA),
            ScenarioAssert.evidence(providerB));
        ScenarioAssert.that(java.util.Arrays.stream(allProviderEvidence)
                .noneMatch(line -> line.contains("workflow-request") && line.contains(workflowMarker)),
            "RM-A6 workflow request was recorded on profile providers");
        System.out.println("scenario RM-A6 passed");
    }
}
