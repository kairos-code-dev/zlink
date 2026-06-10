namespace SupportChat.Server.Support.Adapters.ZLink.Spots;

internal sealed record ConversationCreateRequest(
    string CustomerActorId,
    string CustomerDisplayName,
    string Subject,
    long CreatedAtUnixMs);
