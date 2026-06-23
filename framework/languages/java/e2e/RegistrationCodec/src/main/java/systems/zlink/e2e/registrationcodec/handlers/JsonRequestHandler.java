package systems.zlink.e2e.registrationcodec.handlers;

import systems.zlink.e2e.registrationcodec.Contracts;
import systems.zlink.e2e.registrationcodec.ScenarioState;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class JsonRequestHandler
    implements ZLinkRequestHandler<Contracts.JsonEchoRequest, Contracts.EchoReply> {
    private final ScenarioState state;

    public JsonRequestHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public Contracts.EchoReply handle(
        Contracts.JsonEchoRequest request,
        ZLinkRequestContext context) {
        state.record("Request", "JsonEcho", request.value());
        return new Contracts.EchoReply("echo:" + request.value(), "json");
    }
}
