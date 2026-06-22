namespace SupportChat.Server.Support.Application.ConversationAssignment;

internal sealed class SupportConversationAllocator(IConversationStarter conversations)
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
            await conversations.StartAsync(
                conversationId,
                new ConversationStartRequest(
                    customerActorId,
                    customerDisplayName,
                    subject,
                    DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()),
                cancellationToken);

            return conversationId;
        }
        finally
        {
            _gate.Release();
        }
    }
}

internal sealed record ConversationStartRequest(
    string CustomerActorId,
    string CustomerDisplayName,
    string Subject,
    long CreatedAtUnixMs);

internal interface IConversationStarter
{
    ValueTask StartAsync(
        string conversationId,
        ConversationStartRequest request,
        CancellationToken cancellationToken);
}
