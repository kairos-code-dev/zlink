using System.Collections.Concurrent;

namespace TicTacToe.SessionGateway;

public sealed class SessionGatewaySampleScenario
{
    public async ValueTask<SessionGatewaySmokeResult> RunAsync(CancellationToken cancellationToken = default)
    {
        var log = new GatewayLog();
        var store = new InMemoryLocationStore(log);
        var routed = new InMemoryRoutedChannel(log);
        var api = new GatewayApiServer(store, log);
        var play = new GatewayPlayServer("play-1", store, routed, log);
        var session1 = new GatewaySessionServer("session-1", api, store, routed, log);
        var session2 = new GatewaySessionServer("session-2", api, store, routed, log);
        api.AttachPlayServer(play);

        var clientA = session1.ConnectClient("client-a");
        var clientB = session1.ConnectClient("client-b");
        await clientA.RequestAsync<AuthenticateReq, AuthenticateRes>(
            new AuthenticateReq("player-a-token"),
            cancellationToken);
        await clientB.RequestAsync<AuthenticateReq, AuthenticateRes>(
            new AuthenticateReq("player-b-token"),
            cancellationToken);

        var created = await clientA.RequestAsync<CreateMatchReq, CreateMatchRes>(
            new CreateMatchReq("gateway-match"),
            cancellationToken);
        await clientA.RequestAsync<JoinMatchReq, JoinMatchRes>(
            new JoinMatchReq(created.MatchId),
            cancellationToken);
        await clientB.RequestAsync<JoinMatchReq, JoinMatchRes>(
            new JoinMatchReq(created.MatchId),
            cancellationToken);

        session1.Disconnect("player-a");
        var reconnectedA = session2.ConnectClient("client-a-reconnected");
        await reconnectedA.RequestAsync<AuthenticateReq, AuthenticateRes>(
            new AuthenticateReq("player-a-token"),
            cancellationToken);

        var moves = new[]
        {
            (Client: reconnectedA, Cell: 0),
            (Client: clientB, Cell: 3),
            (Client: reconnectedA, Cell: 1),
            (Client: clientB, Cell: 4),
            (Client: reconnectedA, Cell: 2)
        };

        PlaceMarkRes? finalMove = null;
        foreach (var move in moves)
        {
            finalMove = await move.Client.RequestAsync<PlaceMarkReq, PlaceMarkRes>(
                new PlaceMarkReq(created.MatchId, move.Cell),
                cancellationToken);
        }

        var result = new SessionGatewaySmokeResult(
            created.MatchId,
            finalMove?.State ?? throw new InvalidOperationException("No moves were executed."),
            log.Lines,
            routed.CompletedRequestReplies,
            store.GetSessionNode("player-a") == "session-2",
            log.Lines.Any(static line => line.Contains("session-2 -> client: ", StringComparison.Ordinal)
                && line.Contains("actor=player-a", StringComparison.Ordinal)));
        result.AssertPassed();
        return result;
    }
}

internal sealed class GatewayApiServer(InMemoryLocationStore store, GatewayLog log)
{
    private GatewayPlayServer? _play;
    private readonly Dictionary<string, string> _tokens = new(StringComparer.Ordinal)
    {
        ["player-a-token"] = "player-a",
        ["player-b-token"] = "player-b"
    };

    public void AttachPlayServer(GatewayPlayServer play)
    {
        _play = play;
    }

    public ValueTask<AuthenticatePlayerRes> AuthenticatePlayerAsync(AuthenticatePlayerReq request)
    {
        log.Add($"session -> api: {nameof(AuthenticatePlayerReq)}");
        return _tokens.TryGetValue(request.AccessToken, out var playerId)
            ? ValueTask.FromResult(new AuthenticatePlayerRes(true, playerId, null))
            : ValueTask.FromResult(new AuthenticatePlayerRes(false, null, "Unknown access token."));
    }

    public async ValueTask<CreateMatchRes> CreateMatchAsync(
        string ownerActorId,
        CreateMatchReq request,
        CancellationToken cancellationToken)
    {
        var play = _play ?? throw new InvalidOperationException("Play server is not attached.");
        log.Add($"session -> api: {nameof(CreateMatchReq)} owner={ownerActorId}");
        var room = await play.CreateMatchRoomAsync(
            new CreateMatchRoomReq(request.MatchName ?? "gateway-match"),
            cancellationToken);
        store.SaveMatch(room.MatchId, play.NodeRid);
        return new CreateMatchRes(room.MatchId, ownerActorId);
    }
}

internal sealed class GatewaySessionServer
{
    private readonly ConcurrentDictionary<string, GatewaySessionBinding> _bindings = new(StringComparer.Ordinal);
    private readonly string _nodeRid;
    private readonly GatewayApiServer _api;
    private readonly InMemoryLocationStore _store;
    private readonly InMemoryRoutedChannel _routed;
    private readonly GatewayLog _log;

    public GatewaySessionServer(
        string nodeRid,
        GatewayApiServer api,
        InMemoryLocationStore store,
        InMemoryRoutedChannel routed,
        GatewayLog log)
    {
        _nodeRid = nodeRid;
        _api = api;
        _store = store;
        _routed = routed;
        _log = log;
        routed.Register(
            nodeRid,
            static (_, packet, _) => throw new InvalidOperationException(
                $"Session node does not accept routed requests for {packet.GetType().Name}."),
            DeliverToClient);
    }

    public GatewayStreamClient ConnectClient(string clientId)
    {
        _log.Add($"client -> session: connected node={_nodeRid} client={clientId}");
        return new GatewayStreamClient(new GatewaySessionConnection(clientId, this, _api, _store, _routed, _log));
    }

    public void BindActor(string actorId, Action<object> notify)
    {
        _bindings[actorId] = new GatewaySessionBinding(actorId, notify);
        _store.SaveActorSession(actorId, _nodeRid);
        _log.Add($"session: bind actor={actorId} node={_nodeRid}");
    }

    public void Disconnect(string actorId)
    {
        _bindings.TryRemove(actorId, out _);
        _log.Add($"session: disconnect actor={actorId} node={_nodeRid}");
    }

    public void DeliverToClient(string actorId, object packet)
    {
        if (!_bindings.TryGetValue(actorId, out var binding))
        {
            throw new InvalidOperationException($"Actor {actorId} is not bound on {_nodeRid}.");
        }

        _log.Add($"{_nodeRid} -> client: {packet.GetType().Name} actor={actorId}");
        binding.Notify(packet);
    }
}

internal sealed class GatewayStreamClient(GatewaySessionConnection connection)
{
    private long _nextRequestSeq;

    public event Action<object>? NotifyReceived;

    public async ValueTask<TReply> RequestAsync<TRequest, TReply>(
        TRequest request,
        CancellationToken cancellationToken)
    {
        var seq = Interlocked.Increment(ref _nextRequestSeq);
        var reply = await connection.DispatchAsync(seq, request!, Notify, cancellationToken);
        return reply is TReply typed
            ? typed
            : throw new InvalidOperationException(
                $"Request sequence {seq} returned {reply.GetType().Name}, expected {typeof(TReply).Name}.");
    }

    private void Notify(object packet)
    {
        NotifyReceived?.Invoke(packet);
    }
}

internal sealed class GatewaySessionConnection(
    string clientId,
    GatewaySessionServer server,
    GatewayApiServer api,
    InMemoryLocationStore store,
    InMemoryRoutedChannel routed,
    GatewayLog log)
{
    private string? _actorId;

    public async ValueTask<object> DispatchAsync(
        long requestSeq,
        object packet,
        Action<object> notify,
        CancellationToken cancellationToken)
    {
        log.Add($"client -> session: seq={requestSeq} packet={packet.GetType().Name}");
        object reply = packet switch
        {
            AuthenticateReq request => await AuthenticateAsync(request, notify),
            CreateMatchReq request => await CreateMatchAsync(request, cancellationToken),
            JoinMatchReq request => await RelayToPlayAsync(request, cancellationToken),
            PlaceMarkReq request => await RelayToPlayAsync(request, cancellationToken),
            _ => throw new InvalidOperationException($"Unsupported packet {packet.GetType().Name}.")
        };
        log.Add($"session -> client: seq={requestSeq} reply={reply.GetType().Name}");
        return reply;
    }

    private async ValueTask<AuthenticateRes> AuthenticateAsync(
        AuthenticateReq request,
        Action<object> notify)
    {
        var auth = await api.AuthenticatePlayerAsync(new AuthenticatePlayerReq(request.AccessToken));
        if (!auth.Accepted || auth.PlayerId is null)
        {
            throw new InvalidOperationException(auth.Reason ?? "Authentication failed.");
        }

        _actorId = auth.PlayerId;
        server.BindActor(auth.PlayerId, notify);
        log.Add($"session: authenticated client={clientId} actor={auth.PlayerId}");
        return new AuthenticateRes(auth.PlayerId, auth.PlayerId);
    }

    private ValueTask<CreateMatchRes> CreateMatchAsync(
        CreateMatchReq request,
        CancellationToken cancellationToken)
        => api.CreateMatchAsync(RequireActor(), request, cancellationToken);

    private async ValueTask<object> RelayToPlayAsync(object packet, CancellationToken cancellationToken)
    {
        var actorId = RequireActor();
        var playNode = packet switch
        {
            JoinMatchReq join => store.GetMatchNode(join.MatchId),
            PlaceMarkReq place => store.GetMatchNode(place.MatchId),
            _ => throw new InvalidOperationException($"Unsupported relay packet {packet.GetType().Name}.")
        };
        var relay = new ActorRelay(routed, playNode, actorId);
        return await relay.RequestAsync(packet, cancellationToken);
    }

    private string RequireActor()
        => _actorId ?? throw new InvalidOperationException("AuthenticateReq is required before gateway packets.");
}

internal sealed class GatewayPlayServer
{
    private readonly InMemoryLocationStore _store;
    private readonly SessionGatewaySender _gateway;
    private readonly GatewayLog _log;
    private readonly ConcurrentDictionary<string, GatewayGameRoom> _rooms = new(StringComparer.Ordinal);
    private int _matchSerial;

    public GatewayPlayServer(
        string nodeRid,
        InMemoryLocationStore store,
        InMemoryRoutedChannel routed,
        GatewayLog log)
    {
        NodeRid = nodeRid;
        _store = store;
        _gateway = new SessionGatewaySender(routed, store);
        _log = log;
        routed.Register(nodeRid, HandleRoutedRequestAsync, HandleRoutedSend);
    }

    public string NodeRid { get; }

    public ValueTask<CreateMatchRoomRes> CreateMatchRoomAsync(
        CreateMatchRoomReq request,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        var matchId = $"gateway-{Interlocked.Increment(ref _matchSerial):0000}";
        _rooms[matchId] = new GatewayGameRoom(matchId, _store, _gateway, _log);
        _log.Add($"api -> play: {nameof(CreateMatchRoomReq)} match={request.MatchName}");
        return ValueTask.FromResult(new CreateMatchRoomRes(matchId));
    }

    private ValueTask<object> HandleRoutedRequestAsync(
        string actorId,
        object packet,
        CancellationToken cancellationToken)
    {
        _store.SaveActorPlay(actorId, NodeRid);
        object reply = packet switch
        {
            JoinMatchReq request => Room(request.MatchId).Join(actorId),
            PlaceMarkReq request => Room(request.MatchId).Place(actorId, request.Cell),
            _ => throw new InvalidOperationException($"Unsupported actor relay packet {packet.GetType().Name}.")
        };
        _log.Add($"actor relay -> play: actor={actorId} packet={packet.GetType().Name}");
        return ValueTask.FromResult(reply);
    }

    private void HandleRoutedSend(string actorId, object packet)
    {
        _log.Add($"routed send -> play: actor={actorId} packet={packet.GetType().Name}");
    }

    private GatewayGameRoom Room(string matchId)
        => _rooms.GetValueOrDefault(matchId)
           ?? throw new InvalidOperationException($"Unknown match {matchId}.");
}

internal sealed class ActorRelay(InMemoryRoutedChannel routed, string targetPlayNodeRid, string actorId)
{
    public ValueTask<object> RequestAsync(object packet, CancellationToken cancellationToken)
        => routed.RequestAsync(targetPlayNodeRid, actorId, packet, cancellationToken);
}

internal sealed class SessionGatewaySender(
    InMemoryRoutedChannel routed,
    InMemoryLocationStore store)
{
    public void SendToActor(string actorId, object packet)
    {
        var sessionNode = store.GetSessionNode(actorId);
        routed.Send(sessionNode, actorId, packet);
    }
}

internal sealed class GatewayGameRoom(
    string matchId,
    InMemoryLocationStore store,
    SessionGatewaySender gateway,
    GatewayLog log)
{
    private readonly List<(string ActorId, string Mark)> _players = [];
    private readonly char[] _board = "---------".ToCharArray();
    private string? _winner;
    private bool _draw;
    private string _turn = "player-a";

    public JoinMatchRes Join(string actorId)
    {
        var existing = _players.FirstOrDefault(player => player.ActorId == actorId);
        var mark = existing.ActorId is not null ? existing.Mark : _players.Count == 0 ? "X" : "O";
        if (existing.ActorId is null)
        {
            if (_players.Count == 2)
            {
                throw new InvalidOperationException("Match already has two players.");
            }

            _players.Add((actorId, mark));
        }

        store.SaveActorPlay(actorId, "play-1");
        var state = Snapshot();
        foreach (var player in _players.Where(player => player.ActorId != actorId))
        {
            gateway.SendToActor(player.ActorId, new OpponentJoinedNotify(matchId, actorId));
        }

        log.Add($"play room: join actor={actorId} mark={mark}");
        return new JoinMatchRes(matchId, actorId, mark, state);
    }

    public PlaceMarkRes Place(string actorId, int cell)
    {
        var player = _players.FirstOrDefault(player => player.ActorId == actorId);
        if (player.ActorId is null)
        {
            throw new InvalidOperationException("Actor has not joined the match.");
        }

        if (_winner is not null || _draw)
        {
            throw new InvalidOperationException("Match already ended.");
        }

        if (_turn != actorId)
        {
            throw new InvalidOperationException($"It is {_turn}'s turn.");
        }

        if ((uint)cell >= 9 || _board[cell] != '-')
        {
            throw new InvalidOperationException($"Cell {cell} is not available.");
        }

        _board[cell] = player.Mark[0];
        _winner = HasWon(player.Mark[0]) ? actorId : null;
        _draw = _winner is null && _board.All(static value => value != '-');
        _turn = player.Mark == "X" ? "player-b" : "player-a";
        var state = Snapshot();
        object notification = _winner is not null || _draw
            ? new GameEndedNotify(matchId, _winner, _draw, state)
            : new TurnChangedNotify(matchId, _turn, state);
        foreach (var target in _players)
        {
            gateway.SendToActor(target.ActorId, notification);
        }

        log.Add($"play room: move actor={actorId} cell={cell} board={state.Board}");
        return new PlaceMarkRes(state);
    }

    private TicTacToeState Snapshot()
        => new(matchId, new string(_board), _turn, _winner, _draw);

    private bool HasWon(char mark)
    {
        int[] lines =
        [
            0, 1, 2,
            3, 4, 5,
            6, 7, 8,
            0, 3, 6,
            1, 4, 7,
            2, 5, 8,
            0, 4, 8,
            2, 4, 6
        ];
        for (var i = 0; i < lines.Length; i += 3)
        {
            if (_board[lines[i]] == mark
                && _board[lines[i + 1]] == mark
                && _board[lines[i + 2]] == mark)
            {
                return true;
            }
        }

        return false;
    }
}

internal sealed class InMemoryRoutedChannel(GatewayLog log)
{
    private readonly ConcurrentDictionary<string, RoutedNode> _nodes = new(StringComparer.Ordinal);
    private long _nextSequence;

    public int CompletedRequestReplies { get; private set; }

    public void Register(
        string nodeRid,
        Func<string, object, CancellationToken, ValueTask<object>> requestHandler,
        Action<string, object> sendHandler)
    {
        _nodes[nodeRid] = new RoutedNode(requestHandler, sendHandler);
    }

    public async ValueTask<object> RequestAsync(
        string targetNodeRid,
        string actorId,
        object packet,
        CancellationToken cancellationToken)
    {
        var sequence = Interlocked.Increment(ref _nextSequence);
        log.Add($"routed: request seq={sequence} target={targetNodeRid} actor={actorId} packet={packet.GetType().Name}");
        var node = Resolve(targetNodeRid);
        var reply = await node.RequestHandler(actorId, packet, cancellationToken);
        CompletedRequestReplies++;
        log.Add($"routed: reply seq={sequence} target={targetNodeRid} reply={reply.GetType().Name}");
        return reply;
    }

    public void Send(string targetNodeRid, string actorId, object packet)
    {
        log.Add($"routed: send target={targetNodeRid} actor={actorId} packet={packet.GetType().Name}");
        Resolve(targetNodeRid).SendHandler(actorId, packet);
    }

    private RoutedNode Resolve(string targetNodeRid)
        => _nodes.GetValueOrDefault(targetNodeRid)
           ?? throw new InvalidOperationException($"Routed node {targetNodeRid} is not registered.");

    private sealed record RoutedNode(
        Func<string, object, CancellationToken, ValueTask<object>> RequestHandler,
        Action<string, object> SendHandler);
}

internal sealed class InMemoryLocationStore(GatewayLog log)
{
    private readonly ConcurrentDictionary<string, string> _actorPlay = new(StringComparer.Ordinal);
    private readonly ConcurrentDictionary<string, string> _actorSession = new(StringComparer.Ordinal);
    private readonly ConcurrentDictionary<string, string> _matchPlay = new(StringComparer.Ordinal);

    public void SaveActorPlay(string actorId, string playNodeRid)
    {
        _actorPlay[actorId] = playNodeRid;
        log.Add($"location: actor-play actor={actorId} node={playNodeRid}");
    }

    public void SaveActorSession(string actorId, string sessionNodeRid)
    {
        _actorSession[actorId] = sessionNodeRid;
        log.Add($"location: actor-session actor={actorId} node={sessionNodeRid}");
    }

    public void SaveMatch(string matchId, string playNodeRid)
    {
        _matchPlay[matchId] = playNodeRid;
        log.Add($"location: match-play match={matchId} node={playNodeRid}");
    }

    public string GetSessionNode(string actorId)
        => _actorSession.GetValueOrDefault(actorId)
           ?? throw new InvalidOperationException($"No session location for actor {actorId}.");

    public string GetMatchNode(string matchId)
        => _matchPlay.GetValueOrDefault(matchId)
           ?? throw new InvalidOperationException($"No play location for match {matchId}.");
}

internal sealed record GatewaySessionBinding(string ActorId, Action<object> Notify);

internal sealed class GatewayLog
{
    private readonly List<string> _lines = [];

    public IReadOnlyList<string> Lines => _lines;

    public void Add(string line)
    {
        _lines.Add(line);
    }
}
