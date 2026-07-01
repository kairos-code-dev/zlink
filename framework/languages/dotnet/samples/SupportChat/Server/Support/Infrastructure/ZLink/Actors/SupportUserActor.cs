using Zlink.Framework.Contracts.Actors;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Actors;

internal sealed class SupportUserActor(
    string actorId,
    IZLinkActorContext context) : IZLinkActor
{
    public string DisplayName { get; private set; } = actorId;

    public string Role { get; private set; } = string.Empty;

    // Conversation-domain identity: the customer's ActorId, or the agent's roster id
    // for a per-conversation agent actor. Defaults to ActorId.
    public string ParticipantId { get; private set; } = actorId;

    public string ConversationId { get; private set; } = string.Empty;
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;

    public void SetIdentity(string displayName, string role, string participantId)
    {
        DisplayName = displayName;
        Role = role;
        ParticipantId = participantId;
    }

    public void JoinConversation(string conversationId)
    {
        ConversationId = conversationId;
    }
}