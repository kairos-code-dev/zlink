namespace SupportChat.Server.Support.Infrastructure.ZLink.Spots;

internal sealed record ConversationCreateRequest(
    string CustomerActorId,
    string CustomerDisplayName,
    string Subject,
    long CreatedAtUnixMs);
