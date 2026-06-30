namespace SupportChat.Server.Support.Infrastructure.ZLink.Spots.ConversationSpot;

internal sealed record ConversationCreateRequest(
    string CustomerActorId,
    string CustomerDisplayName,
    string Subject,
    long CreatedAtUnixMs);