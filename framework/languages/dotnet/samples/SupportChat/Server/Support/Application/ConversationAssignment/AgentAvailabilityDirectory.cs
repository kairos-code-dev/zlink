namespace SupportChat.Server.Support.Application.ConversationAssignment;

internal sealed class AgentAvailabilityDirectory
{
    private readonly Queue<AvailableAgent> _available = [];
    private readonly HashSet<string> _actorIds = new(StringComparer.Ordinal);

    public void SetAvailable(string actorId, string displayName, bool isAvailable)
    {
        if (!isAvailable)
        {
            _actorIds.Remove(actorId);
            return;
        }

        if (_actorIds.Add(actorId))
        {
            _available.Enqueue(new AvailableAgent(actorId, displayName));
        }
    }

    public AvailableAgent? TakeNext()
    {
        while (_available.TryDequeue(out var candidate))
        {
            if (_actorIds.Remove(candidate.ActorId))
            {
                return candidate;
            }
        }

        return null;
    }
}

internal sealed record AvailableAgent(
    string ActorId,
    string DisplayName);
