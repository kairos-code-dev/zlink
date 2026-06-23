package systems.zlink.e2e.registrationcodec.handlers;

import com.google.protobuf.StringValue;
import systems.zlink.e2e.registrationcodec.ScenarioState;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;

public final class ProtobufSendHandler
    implements ZLinkSendHandler<StringValue> {
    private final ScenarioState state;

    public ProtobufSendHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public void handle(StringValue message, ZLinkSendContext context) {
        state.record("Send", "ProtobufEcho", message.getValue());
    }
}
