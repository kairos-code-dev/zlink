using SupportChat.Shared.Contracts;

namespace SupportChat.Server.Configuration;

public sealed record EnsureSupportUserActorReq(
    string ActorId,
    string DisplayName,
    string Role,
    string ParticipantId);

public sealed record ActorRefSnapshot(
    byte[] NodeRid,
    string ActorId,
    ulong Generation);

public sealed record EnsureSupportUserActorRes(ActorRefSnapshot Actor);

// One agent serves many conversations, so each assigned conversation gets its own
// conversation actor (ParticipantId = the agent's roster id). The session asks the
// Support server to create that actor and join it into the ConversationSpot.
public sealed record EnsureAgentConversationReq(
    string RosterActorId,
    string DisplayName,
    string ConversationId);

public sealed record EnsureAgentConversationRes(
    ActorRefSnapshot Actor,
    ConversationState State);
