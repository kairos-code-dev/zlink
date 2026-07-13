package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.client.support.waitForEvidence

internal object SmE2Scenario {
    suspend fun run() {
        waitForEvidence(Env.get("ZLINK_KOTLIN_E2E_HTTP_A_ENDPOINT"), "SpotTimer")
        println("scenario SM-E2 passed")
    }
}
