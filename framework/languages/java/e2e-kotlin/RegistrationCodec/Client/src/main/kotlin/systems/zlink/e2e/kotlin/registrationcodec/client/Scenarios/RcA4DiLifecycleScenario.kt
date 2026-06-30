package systems.zlink.e2e.kotlin.registrationcodec.client.scenarios

import systems.zlink.e2e.kotlin.registrationcodec.Contracts
import systems.zlink.e2e.kotlin.registrationcodec.DiLifecycleReply
import systems.zlink.e2e.kotlin.registrationcodec.DiLifecycleRequest
import systems.zlink.e2e.kotlin.registrationcodec.client.support.EvidenceText
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ScenarioAssert
import systems.zlink.framework.channels.ZLinkClient

class RcA4DiLifecycleScenario(
    private val client: ZLinkClient,
    private val evidence: EvidenceText,
    private val assert: ScenarioAssert,
) {
    fun run() {
        val diReplies = (0 until 3).map { index ->
            client.requestToChannel(Contracts.CHANNEL, DiLifecycleRequest("di-$index"))
                .packetName("DiLifecycle")
                .timeout(requestTimeout)
                .await(DiLifecycleReply::class.java)
        }
        assert.that(diReplies.map { it.scopedId }.distinct().size == 3, "RC-A4 scoped dependency was not recreated")
        assert.that(diReplies.map { it.singletonId }.distinct().size == 1, "RC-A4 singleton dependency changed")
        assert.that(diReplies[2].disposedCount == 3, "RC-A4 dispose count mismatch")
        evidence.waitForEvidenceValueSuffix("DI", "DiLifecycle", ":di-2")
        println("scenario RC-A4 passed")
    }
}
