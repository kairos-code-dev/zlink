package systems.zlink.e2e.registrationcodec.client;

import systems.zlink.e2e.registrationcodec.client.Scenarios.AttributeRegistrationScenario;
import systems.zlink.e2e.registrationcodec.client.Scenarios.AutoRegistrationScenario;
import systems.zlink.e2e.registrationcodec.client.Scenarios.ManualRegistrationScenario;
import systems.zlink.e2e.registrationcodec.client.Scenarios.RcA4DiLifecycleScenario;
import systems.zlink.e2e.registrationcodec.client.Scenarios.RcA5FilterOrderingScenario;
import systems.zlink.e2e.registrationcodec.client.Scenarios.RcB1JsonCodecScenario;
import systems.zlink.e2e.registrationcodec.client.Scenarios.RcB2ProtobufCodecScenario;
import systems.zlink.e2e.registrationcodec.client.Scenarios.RcB3MessagePackCodecScenario;
import systems.zlink.e2e.registrationcodec.client.Scenarios.RcB4CodecCoexistenceScenario;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioContext;

public final class ScenarioRunner {
    private final ScenarioContext context;

    public ScenarioRunner(ScenarioContext context) {
        this.context = context;
    }

    public void run() {
        AutoRegistrationScenario.run(context);
        AttributeRegistrationScenario.run(context);
        ManualRegistrationScenario.run(context);
        RcA4DiLifecycleScenario.run(context);
        RcA5FilterOrderingScenario.run(context);
        RcB1JsonCodecScenario.run(context);
        RcB2ProtobufCodecScenario.run(context);
        RcB3MessagePackCodecScenario.run(context);
        RcB4CodecCoexistenceScenario.run(context);
    }
}
