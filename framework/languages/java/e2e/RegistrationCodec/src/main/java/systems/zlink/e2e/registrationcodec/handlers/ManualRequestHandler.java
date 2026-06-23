package systems.zlink.e2e.registrationcodec.handlers;

import systems.zlink.e2e.registrationcodec.Contracts;
import systems.zlink.e2e.registrationcodec.ScenarioState;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class ManualRequestHandler
    implements ZLinkRequestHandler<Contracts.EchoManualRequest, Contracts.EchoReply> {
    private final ScenarioState state;

    public ManualRequestHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public Contracts.EchoReply handle(
        Contracts.EchoManualRequest request,
        ZLinkRequestContext context) {
        state.record("Request", context.packetName().orElse("EchoManual"), request.value());
        return new Contracts.EchoReply("echo:" + request.value(), "manual");
    }
}
