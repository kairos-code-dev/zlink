package systems.zlink.e2e.kotlin.runtimemonitoring.client

import com.fasterxml.jackson.databind.ObjectMapper
import systems.zlink.e2e.kotlin.runtimemonitoring.client.scenarios.MonA1SocketEventsScenario
import systems.zlink.e2e.kotlin.runtimemonitoring.client.scenarios.MonA2RegistryEventsScenario
import systems.zlink.e2e.kotlin.runtimemonitoring.client.scenarios.MonA3SpotEventsScenario
import systems.zlink.e2e.kotlin.runtimemonitoring.client.scenarios.MonA4AvailabilityTransitionScenario
import systems.zlink.e2e.kotlin.runtimemonitoring.client.scenarios.MonA5FixedKindsScenario
import systems.zlink.e2e.kotlin.runtimemonitoring.client.scenarios.MonB1KindFilterScenario
import systems.zlink.e2e.kotlin.runtimemonitoring.client.scenarios.MonB2RegistrationValidationScenario
import systems.zlink.e2e.kotlin.runtimemonitoring.client.scenarios.MonC1DispatchFailureScenario
import systems.zlink.e2e.kotlin.runtimemonitoring.client.scenarios.MonD1FailureRecoveryScenario

class ClientScenario(
    json: ObjectMapper,
) {
    private val options = ClientOptions()
    private val evidence = MonitoringEvidenceClient(json)
    private val monA1 = MonA1SocketEventsScenario(options, evidence)
    private val monA2 = MonA2RegistryEventsScenario(options, evidence)
    private val monA3 = MonA3SpotEventsScenario(options, evidence)
    private val monA4 = MonA4AvailabilityTransitionScenario(options, evidence)
    private val monA5 = MonA5FixedKindsScenario(options, evidence)
    private val monB1 = MonB1KindFilterScenario(options, evidence)
    private val monB2 = MonB2RegistrationValidationScenario(options, evidence)
    private val monC1 = MonC1DispatchFailureScenario(options, evidence)
    private val monD1 = MonD1FailureRecoveryScenario(options, evidence)

    fun run() {
        monA1.run()
        monA2.run()
        monA3.run()
        monA4.runDrainSubset()
        monA5.run()
        monB1.run()
        monB2.run()
        monC1.run()
        monD1.run()
    }
}
