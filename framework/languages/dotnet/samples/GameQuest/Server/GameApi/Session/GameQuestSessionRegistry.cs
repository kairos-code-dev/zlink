using GameQuest.Shared;
using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Streams;

namespace GameQuest.GameApi.Session;

internal sealed class GameQuestSessionRegistry(ILogger<GameQuestSessionRegistry> logger)
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

    public async ValueTask<bool> NotifyAsync(
        NotifyQuestProgressReq request,
        CancellationToken cancellationToken)
    {
        IZLinkSessionContext? session;
        lock (_gate)
        {
            _sessionsByPlayer.TryGetValue(request.PlayerId, out session);
        }

        if (session is null)
        {
            return false;
        }

        try
        {
            foreach (var progress in request.Projection)
            {
                await session.Client.Send(new QuestProgressNotify(request.PlayerId, session.SessionId, progress))
                    .PacketName(SampleNames.ProgressPacket)
                    .Async();
            }

            if (!string.IsNullOrWhiteSpace(request.CompletedQuestId))
            {
                var completed = request.Projection.First(progress => progress.QuestId == request.CompletedQuestId);
                await session.Client.Send(new QuestCompletedNotify(request.PlayerId, session.SessionId, completed, RewardGranted: true))
                        .PacketName(SampleNames.CompletedPacket)
                        .Async();
            }
        }
        catch (ZlinkSubmitException error)
        {
            Remove(session);
            logger.LogInformation(error,
                "gamequest stream notify skipped because the session is no longer connected. player={PlayerId} session={SessionId}",
                request.PlayerId,
                session.SessionId);
            return false;
        }

        return true;
    }
}
