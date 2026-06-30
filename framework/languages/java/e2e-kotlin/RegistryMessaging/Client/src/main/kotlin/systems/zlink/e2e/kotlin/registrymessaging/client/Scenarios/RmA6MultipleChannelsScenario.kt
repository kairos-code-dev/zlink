package systems.zlink.e2e.kotlin.registrymessaging.client.Scenarios

import systems.zlink.e2e.kotlin.registrymessaging.client.Support.HttpJson
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.ScenarioAssert
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileReply
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileRequest
import systems.zlink.e2e.kotlin.registrymessaging.shared.WorkflowReply
import systems.zlink.e2e.kotlin.registrymessaging.shared.WorkflowRequest

object RmA6MultipleChannelsScenario {
    fun run(providerA: HttpJson, providerB: HttpJson, workflow: HttpJson) {
        val api = providerA.post<ProfileReply>("/profile/request", ProfileRequest("rm-a6-api"))
        ScenarioAssert.that(api.providerRid == "api-a" || api.providerRid == "api-b", "RM-A6 api resolved wrong provider.")

        val workflowReply = workflow.post<WorkflowReply>("/workflow/request", WorkflowRequest("rm-a6-workflow"))
        ScenarioAssert.that(workflowReply.providerRid == "workflow-a", "RM-A6 workflow resolved wrong provider.")
        ScenarioAssert.that(workflowReply.value == "workflow:rm-a6-workflow", "RM-A6 workflow reply mismatch.")

        val providerEvidence = providerA.get<List<String>>("/evidence") + providerB.get<List<String>>("/evidence")
        val workflowEvidence = workflow.get<List<String>>("/evidence")
        ScenarioAssert.that(providerEvidence.any { it.contains("rm-a6-api") }, "RM-A6 api evidence missing.")
        ScenarioAssert.that(workflowEvidence.any { it.contains("rm-a6-workflow") }, "RM-A6 workflow evidence missing.")
        println("scenario RM-A6 passed")
    }
}
