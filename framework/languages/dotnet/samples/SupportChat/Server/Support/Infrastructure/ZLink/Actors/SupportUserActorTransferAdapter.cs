using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Actors;

internal sealed class SupportUserActorTransferAdapter : IZLinkActorTransferAdapter<SupportUserActor>
{
    public ValueTask<ZLinkMessage> TransferOutAsync(
        SupportUserActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(ZLinkMessage.From(new SupportUserActorTransferState(
            actor.DisplayName,
            actor.Role,
            actor.ParticipantId,
            actor.ConversationId)));
    }

    public ValueTask<SupportUserActor> TransferInAsync(
        string actorId,
        IZLinkActorContext context,
        ZLinkMessage state,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var transferred = state.Decode<SupportUserActorTransferState>();
        var actor = new SupportUserActor(actorId, context);
        actor.SetIdentity(transferred.DisplayName, transferred.Role, transferred.ParticipantId);
        if (!string.IsNullOrEmpty(transferred.ConversationId))
            actor.JoinConversation(transferred.ConversationId);
        return ValueTask.FromResult(actor);
    }

    private sealed record SupportUserActorTransferState(
        string DisplayName,
        string Role,
        string ParticipantId,
        string ConversationId);
}
