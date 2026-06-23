using Systems.Zlink;
using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Server.Configuration;
using SupportChat.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Spots.Handlers;

internal sealed class OpenConversationActorHandler
    : IZLinkEntrySpotActorRequestHandler<SupportEntrySpot, SupportUserActor, OpenConversationReq, OpenConversationRes>
{
    public async ValueTask<OpenConversationRes> HandleAsync(
        SupportEntrySpot entrySpot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        OpenConversationReq message,
        CancellationToken cancellationToken)
    {
        _ = context;
        if (!string.Equals(actor.Role, SupportChatRoles.Customer, StringComparison.Ordinal))
        {
            throw new InvalidOperationException("Only customer actors can open a conversation.");
        }

        Console.Error.WriteLine($"support entry open: request actor={actor.ActorId} subject={message.Subject}");
        var opened = await entrySpot.Context.Outbound.RequestToChannel(
                SampleNames.ApiChannel,
                new OpenConversationApiReq(
                    actor.ActorId,
                    actor.DisplayName,
                    message.Subject))
            .Async<OpenConversationApiRes>(cancellationToken);
        var conversationRid = RoutingId.From(opened.ConversationId);
        var joined = await actor.Context.JoinSpot(
                conversationRid,
                new JoinConversationReq(opened.ConversationId))
            .Async(cancellationToken);
        var state = joined.Reply.Decode<JoinConversationRes>().State;
        Console.Error.WriteLine($"support entry open: completed conversation={opened.ConversationId} status={state.Status}");
        return new OpenConversationRes(opened.ConversationId, state);
    }
}
