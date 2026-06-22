using Zlink.Framework.Contracts.Codecs.Json;
using SupportChat.Server.Support.Adapters.ZLink.Spots;
using Systems.Zlink;
using Zlink.Framework.Contracts.Spots;

namespace SupportChat.Server.Support.Application.ConversationAssignment;

internal sealed class SupportConversationAllocator(IZLinkSpotManager spots)
{
    private readonly SemaphoreSlim _gate = new(1, 1);
    private long _conversationSeq;

    public async ValueTask<string> AllocateAsync(
        string customerActorId,
        string customerDisplayName,
        string subject,
        CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var conversationId = $"supportchat-conversation-{Interlocked.Increment(ref _conversationSeq)}";
            using var create = new ConversationCreateRequest(
                    customerActorId,
                    customerDisplayName,
                    subject,
                    DateTimeOffset.UtcNow.ToUnixTimeMilliseconds())
                .ToJson();
            var conversation = await spots.GetOrCreateAsync<ConversationSpot>(
                RoutingId.From(conversationId),
                create,
                cancellationToken);
            if (conversation.State != ZLinkSpotCreateState.Created)
            {
                throw new InvalidOperationException("Support conversation creation was rejected.");
            }

            return conversationId;
        }
        finally
        {
            _gate.Release();
        }
    }
}
