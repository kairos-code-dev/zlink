package systems.zlink.e2e.registrationcodec.handlers;

import systems.zlink.e2e.registrationcodec.Contracts;
import systems.zlink.e2e.registrationcodec.ScenarioState;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.handlers.ZLinkSend;

@ZLinkHandlerGroup(Contracts.ATTR_GROUP)
public final class AttrEchoHandler {
    private final ScenarioState state;

    public AttrEchoHandler(ScenarioState state) {
        this.state = state;
    }

    @ZLinkRequest(packetName = "EchoAttr")
    public Contracts.EchoReply request(
        Contracts.EchoAttrRequest request,
        ZLinkRequestContext context) {
        state.record("Request", "EchoAttr", request.value());
        return new Contracts.EchoReply("echo:" + request.value(), "attr");
    }

    @ZLinkSend(packetName = "EchoAttr")
    public void send(
        Contracts.EchoAttrCommand message,
        ZLinkSendContext context) {
        state.record("Send", "EchoAttr", message.value());
    }
}
