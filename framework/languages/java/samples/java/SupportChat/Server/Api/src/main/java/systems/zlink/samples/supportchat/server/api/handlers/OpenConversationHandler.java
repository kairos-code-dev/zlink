package systems.zlink.samples.supportchat.server.api.handlers;

import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.configuration.SampleTimings;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

@ZLinkHandlerGroup("api")
public final class OpenConversationHandler
    implements ZLinkRequestHandler<
        Messages.OpenConversationApiReq,
        Messages.OpenConversationApiRes> {
    private final ZLinkClient client;

    public OpenConversationHandler(ZLinkClient client) {
        this.client = client;
    }

    @Override
    public Messages.OpenConversationApiRes handle(
        Messages.OpenConversationApiReq request,
        ZLinkRequestContext context) {
        Messages.AllocateConversationRes allocated = client.requestToChannel(
                SampleNames.SupportChannel,
                new Messages.AllocateConversationReq(
                    request.customerActorId(),
                    request.customerDisplayName(),
                    request.subject()))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.AllocateConversationRes.class);
        Messages.AssignAgentRes assigned = client.requestToChannel(
                SampleNames.SupportChannel,
                new Messages.AssignAgentReq(allocated.conversationId(), null))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.AssignAgentRes.class);
        return new Messages.OpenConversationApiRes(
            allocated.conversationId(),
            assigned.status());
    }
}
