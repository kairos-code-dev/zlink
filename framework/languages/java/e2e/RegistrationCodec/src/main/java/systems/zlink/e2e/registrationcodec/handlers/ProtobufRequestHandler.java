package systems.zlink.e2e.registrationcodec.handlers;

import com.google.protobuf.StringValue;
import systems.zlink.e2e.registrationcodec.ScenarioState;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class ProtobufRequestHandler
    implements ZLinkRequestHandler<StringValue, StringValue> {
    private final ScenarioState state;

    public ProtobufRequestHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public StringValue handle(StringValue request, ZLinkRequestContext context) {
        state.record("Request", "ProtobufEcho", request.getValue());
        return StringValue.of("echo:" + request.getValue());
    }
}
