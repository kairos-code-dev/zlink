using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Streams;

namespace GameQuest.GameApi.Session;

internal sealed class GameQuestSessionRegistry
{
    private readonly object _gate = new();
    private readonly Dictionary<string, IZLinkSessionContext> _sessionsByPlayer = new(StringComparer.Ordinal);

    public BindQuestSessionReq Bind(string playerId, IZLinkSessionContext context)
    {
        var binding = new BindQuestSessionReq(
            playerId,
            context.SessionId,
            Environment.GetEnvironmentVariable("GAMEQUEST_API_NAME") ?? "api");
        lock (_gate)
        {
            _sessionsByPlayer[playerId] = context;
        }

        return binding;
    }

    public UnbindQuestSessionReq[] Remove(IZLinkSessionContext context)
    {
        var removed = new List<UnbindQuestSessionReq>();
        lock (_gate)
        {
            foreach (var pair in _sessionsByPlayer.Where(pair => ReferenceEquals(pair.Value, context)).ToArray())
            {
                _sessionsByPlayer.Remove(pair.Key);
                removed.Add(new UnbindQuestSessionReq(pair.Key, context.SessionId));
            }
        }

        return removed.ToArray();
    }

    public ValueTask<bool> NotifyAsync(
        NotifyQuestProgressReq request,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        IZLinkSessionContext? session;
        lock (_gate)
        {
            _sessionsByPlayer.TryGetValue(request.PlayerId, out session);
        }

        if (session is null) return ValueTask.FromResult(false);

        foreach (var progress in request.Projection)
            session.Client.Send(new QuestProgressNotify(request.PlayerId, session.SessionId, progress))
                .Submit();

        if (!string.IsNullOrWhiteSpace(request.CompletedQuestId))
        {
            var completed = request.Projection.First(progress => progress.QuestId == request.CompletedQuestId);
            session.Client
                .Send(new QuestCompletedNotify(request.PlayerId, session.SessionId, completed, true))
                .Submit();
        }

        return ValueTask.FromResult(true);
    }
}
