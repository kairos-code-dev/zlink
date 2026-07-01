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
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ProcessSupport
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ScenarioAssert
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ScenarioHttpClient

class ClientScenario(
    private val json: ObjectMapper,
    private val options: ClientOptions,
) {
    fun run() {
        val assert = ScenarioAssert()
        val server = ScenarioHttpClient(options.httpEndpoint, json)
        val codecRequester = ScenarioHttpClient(options.codecRequesterHttpEndpoint, json)

        if (options.mode == ProcessSupport.CODEC_MISMATCH_MODE) {
            CodecMismatchScenario(codecRequester, assert).run()
            return
        }

        AutoRegistrationScenario(server, assert).run()
        AttributeRegistrationScenario(server, assert).run()
        ManualRegistrationScenario(server, assert).run()
        RcA4DiLifecycleScenario(server, assert).run()
        RcA5FilterOrderingScenario(server, assert).run()
        RcB1JsonCodecScenario(server, assert).run()
        RcB2ProtobufCodecScenario(server, assert).run()
        RcB3MessagePackCodecScenario(server, assert).run()
        RcB4CodecCoexistenceScenario(server, assert).run()
    }
}
