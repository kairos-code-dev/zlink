package systems.zlink.e2e.registrationcodec.handlers;

import systems.zlink.e2e.registrationcodec.Contracts;
import systems.zlink.e2e.registrationcodec.ScenarioState;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class MsgpackRequestHandler
    implements ZLinkRequestHandler<Contracts.PackedEchoRequest, Contracts.PackedEchoReply> {
    private final ScenarioState state;

    public MsgpackRequestHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public Contracts.PackedEchoReply handle(
        Contracts.PackedEchoRequest request,
        ZLinkRequestContext context) {
        state.record("Request", "MsgpackEcho", request.value());
        return new Contracts.PackedEchoReply("echo:" + request.value());
    }
}
