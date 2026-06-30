namespace SupportChat.Server.Support.Infrastructure.ZLink.Spots.ConversationSpot;

internal sealed record ConversationCreateReq(
    string CustomerActorId,
    string CustomerDisplayName,
    string Subject,
    long CreatedAtUnixMs);