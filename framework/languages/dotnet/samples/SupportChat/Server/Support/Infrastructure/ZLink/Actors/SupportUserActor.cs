using Zlink.Framework.Contracts.Actors;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Actors;

internal sealed class SupportUserActor(
    string actorId,
    IZLinkActorContext context) : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public string DisplayName { get; private set; } = actorId;

    public string Role { get; private set; } = string.Empty;

    public string ConversationId { get; private set; } = string.Empty;

    public IZLinkActorContext Context { get; } = context;

    public void SetIdentity(string displayName, string role)
    {
        DisplayName = displayName;
        Role = role;
    }

    public void JoinConversation(string conversationId)
    {
        ConversationId = conversationId;
    }
}
