package systems.zlink.e2e.kotlin.registrationcodec

import com.fasterxml.jackson.databind.ObjectMapper
import systems.zlink.e2e.kotlin.registrationcodec.client.scenarios.AutoRegistrationScenario
import systems.zlink.e2e.kotlin.registrationcodec.client.scenarios.AttributeRegistrationScenario
import systems.zlink.e2e.kotlin.registrationcodec.client.scenarios.CodecMismatchScenario
import systems.zlink.e2e.kotlin.registrationcodec.client.scenarios.ManualRegistrationScenario
import systems.zlink.e2e.kotlin.registrationcodec.client.scenarios.RcA4DiLifecycleScenario
import systems.zlink.e2e.kotlin.registrationcodec.client.scenarios.RcA5FilterOrderingScenario
import systems.zlink.e2e.kotlin.registrationcodec.client.scenarios.RcB1JsonCodecScenario
import systems.zlink.e2e.kotlin.registrationcodec.client.scenarios.RcB2ProtobufCodecScenario
import systems.zlink.e2e.kotlin.registrationcodec.client.scenarios.RcB3MessagePackCodecScenario
import systems.zlink.e2e.kotlin.registrationcodec.client.scenarios.RcB4CodecCoexistenceScenario
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ClientOptions
import systems.zlink.e2e.kotlin.registrationcodec.client.support.EvidenceText
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ProcessSupport
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ScenarioAssert
import systems.zlink.framework.channels.ZLinkClient

class ClientScenario(
    private val client: ZLinkClient,
    private val json: ObjectMapper,
    private val options: ClientOptions,
) {
    fun run() {
        val assert = ScenarioAssert()
        val evidence = EvidenceText(options.httpEndpoint, json, assert)

        if (options.mode == ProcessSupport.CODEC_MISMATCH_MODE) {
            CodecMismatchScenario(client, assert).run()
            return
        }

        AutoRegistrationScenario(client, evidence, assert).run()
        AttributeRegistrationScenario(client, evidence, assert).run()
        ManualRegistrationScenario(client, evidence, assert).run()
        RcA4DiLifecycleScenario(client, evidence, assert).run()
        RcA5FilterOrderingScenario(client, evidence, assert).run()
        RcB1JsonCodecScenario(client, evidence, assert).run()
        RcB2ProtobufCodecScenario(client, evidence, assert).run()
        RcB3MessagePackCodecScenario(client, evidence, assert).run()
        RcB4CodecCoexistenceScenario().run()
    }
}
