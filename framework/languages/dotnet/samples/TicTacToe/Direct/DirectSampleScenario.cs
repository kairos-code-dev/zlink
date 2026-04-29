using System.Collections.Concurrent;

namespace TicTacToe.Direct;

public sealed class DirectSampleScenario
{
    public async ValueTask<DirectSmokeResult> RunAsync(CancellationToken cancellationToken = default)
    {
        var log = new SampleLog();
        var api = new DirectApiServer(log);
        var play = new DirectPlayServer(api, log);
        api.AttachPlayServer(play);

        var created = await api.CreateMatchAsync(new CreateMatchReq("direct-match"), cancellationToken);
        var clientA = play.ConnectClient("client-a");
        var clientB = play.ConnectClient("client-b");

        var notifications = new DirectNotificationCounter();
        clientA.NotifyReceived += notifications.Record;
        clientB.NotifyReceived += notifications.Record;

        await clientA.RequestAsync<AuthenticateReq, AuthenticateRes>(
            new AuthenticateReq(created.PlayerAToken),
            cancellationToken);
        await clientB.RequestAsync<AuthenticateReq, AuthenticateRes>(
            new AuthenticateReq(created.PlayerBToken),
            cancellationToken);
        await clientA.RequestAsync<JoinMatchReq, JoinMatchRes>(
            new JoinMatchReq(created.MatchId),
            cancellationToken);
        await clientB.RequestAsync<JoinMatchReq, JoinMatchRes>(
            new JoinMatchReq(created.MatchId),
            cancellationToken);

        var moves = new[]
        {
            (Client: clientA, Cell: 0),
            (Client: clientB, Cell: 3),
            (Client: clientA, Cell: 1),
            (Client: clientB, Cell: 4),
            (Client: clientA, Cell: 2)
        };

        PlaceMarkRes? finalMove = null;
        foreach (var move in moves)
        {
            finalMove = await move.Client.RequestAsync<PlaceMarkReq, PlaceMarkRes>(
                new PlaceMarkReq(created.MatchId, move.Cell),
                cancellationToken);
        }

        var result = new DirectSmokeResult(
            created.MatchId,
            finalMove?.State ?? throw new InvalidOperationException("No moves were executed."),
            log.Lines,
            clientA.CompletedReplies + clientB.CompletedReplies,
            notifications.OpponentJoined,
            notifications.TurnChanged,
            notifications.GameEnded);
        result.AssertPassed();
        return result;
    }
}

internal sealed class DirectApiServer(SampleLog log)
{
    private DirectPlayServer? _play;
    private int _tokenSerial;
    private readonly ConcurrentDictionary<string, string> _tokens = new(StringComparer.Ordinal);

    public void AttachPlayServer(DirectPlayServer play)
    {
        _play = play;
    }

    public async ValueTask<CreateMatchRes> CreateMatchAsync(
        CreateMatchReq request,
        CancellationToken cancellationToken)
    {
        var play = _play ?? throw new InvalidOperationException("Play server is not attached.");
        var playerA = IssueToken();
        var playerB = IssueToken();
        log.Add($"client -> api: {nameof(CreateMatchReq)}");
        var created = await play.CreateMatchAsync(request.MatchName ?? "direct-match", cancellationToken);
        log.Add($"api -> client: {nameof(CreateMatchRes)} match={created.MatchId}");
        return created with
        {
            PlayerAToken = playerA,
            PlayerBToken = playerB
        };
    }

    public ValueTask<AuthenticatePlayerRes> AuthenticatePlayerAsync(
        AuthenticatePlayerReq request,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        log.Add($"play -> api: {nameof(AuthenticatePlayerReq)}");
        return _tokens.TryGetValue(request.AccessToken, out var playerId)
            ? ValueTask.FromResult(new AuthenticatePlayerRes(true, playerId, null))
            : ValueTask.FromResult(new AuthenticatePlayerRes(false, null, "Unknown access token."));
    }

    private string IssueToken()
    {
        var playerId = Interlocked.Increment(ref _tokenSerial) == 1 ? "player-a" : "player-b";
        var token = $"{playerId}-token";
        _tokens[token] = playerId;
        return token;
    }
}

internal sealed class DirectPlayServer(DirectApiServer api, SampleLog log)
{
    private readonly ConcurrentDictionary<string, DirectGameRoom> _rooms = new(StringComparer.Ordinal);
    private int _matchSerial;

    public ValueTask<CreateMatchRes> CreateMatchAsync(string matchName, CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        var matchId = $"direct-{Interlocked.Increment(ref _matchSerial):0000}";
        _rooms[matchId] = new DirectGameRoom(matchId, log);
        log.Add($"api -> play: {nameof(CreateMatchReq)} match={matchName}");
        return ValueTask.FromResult(new CreateMatchRes(
            matchId,
            "inproc://direct/play",
            string.Empty,
            string.Empty));
    }

    public DirectStreamClient ConnectClient(string clientId)
    {
        var session = new DirectPlaySession(clientId, api, _rooms, log);
        log.Add($"client -> play stream: connected client={clientId}");
        return new DirectStreamClient(session);
    }
}

internal sealed class DirectStreamClient(DirectPlaySession session)
{
    private long _nextRequestSeq;

    public event Action<object>? NotifyReceived;

    public int CompletedReplies { get; private set; }

    public async ValueTask<TReply> RequestAsync<TRequest, TReply>(
        TRequest request,
        CancellationToken cancellationToken)
    {
        var seq = Interlocked.Increment(ref _nextRequestSeq);
        var reply = await session.DispatchAsync(seq, request!, Notify, cancellationToken);
        CompletedReplies++;
        return reply is TReply typed
            ? typed
            : throw new InvalidOperationException(
                $"Request sequence {seq} returned {reply.GetType().Name}, expected {typeof(TReply).Name}.");
    }

    private void Notify(object message)
    {
        NotifyReceived?.Invoke(message);
    }
}

internal sealed class DirectPlaySession(
    string clientId,
    DirectApiServer api,
    ConcurrentDictionary<string, DirectGameRoom> rooms,
    SampleLog log)
{
    private DirectPlayerActor? _actor;

    public async ValueTask<object> DispatchAsync(
        long requestSeq,
        object packet,
        Action<object> notify,
        CancellationToken cancellationToken)
    {
        log.Add($"client -> play stream: seq={requestSeq} packet={packet.GetType().Name}");
        object reply = packet switch
        {
            AuthenticateReq request => await AuthenticateAsync(request, notify, cancellationToken),
            JoinMatchReq request => RequireActor().Join(request, cancellationToken),
            PlaceMarkReq request => RequireActor().Place(request, cancellationToken),
            _ => throw new InvalidOperationException($"Unsupported packet {packet.GetType().Name}.")
        };
        log.Add($"play stream -> client: seq={requestSeq} reply={reply.GetType().Name}");
        return reply;
    }

    private async ValueTask<AuthenticateRes> AuthenticateAsync(
        AuthenticateReq request,
        Action<object> notify,
        CancellationToken cancellationToken)
    {
        var auth = await api.AuthenticatePlayerAsync(
            new AuthenticatePlayerReq(request.AccessToken),
            cancellationToken);
        if (!auth.Accepted || auth.PlayerId is null)
        {
            throw new InvalidOperationException(auth.Reason ?? "Authentication failed.");
        }

        _actor = new DirectPlayerActor(auth.PlayerId, notify, rooms, log);
        log.Add($"play session: actor created actorId={auth.PlayerId} client={clientId}");
        return new AuthenticateRes(auth.PlayerId, auth.PlayerId);
    }

    private DirectPlayerActor RequireActor()
        => _actor ?? throw new InvalidOperationException("AuthenticateReq is required before play packets.");
}

internal sealed class DirectPlayerActor(
    string actorId,
    Action<object> notify,
    ConcurrentDictionary<string, DirectGameRoom> rooms,
    SampleLog log)
{
    public string ActorId => actorId;

    public JoinMatchRes Join(JoinMatchReq request, CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        var room = rooms.GetValueOrDefault(request.MatchId)
            ?? throw new InvalidOperationException($"Unknown match {request.MatchId}.");
        return room.Join(this);
    }

    public PlaceMarkRes Place(PlaceMarkReq request, CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        var room = rooms.GetValueOrDefault(request.MatchId)
            ?? throw new InvalidOperationException($"Unknown match {request.MatchId}.");
        return room.Place(this, request.Cell);
    }

    public void Send(object packet)
    {
        log.Add($"play actor -> client: actor={actorId} packet={packet.GetType().Name}");
        notify(packet);
    }
}

internal sealed class DirectGameRoom(string matchId, SampleLog log)
{
    private readonly List<(DirectPlayerActor Actor, string Mark)> _players = [];
    private readonly char[] _board = "---------".ToCharArray();
    private string? _winner;
    private bool _draw;
    private string _turn = "player-a";

    public JoinMatchRes Join(DirectPlayerActor actor)
    {
        var existing = _players.FirstOrDefault(player => player.Actor.ActorId == actor.ActorId);
        var mark = existing.Actor is not null ? existing.Mark : _players.Count == 0 ? "X" : "O";
        if (existing.Actor is null)
        {
            if (_players.Count == 2)
            {
                throw new InvalidOperationException("Match already has two players.");
            }

            _players.Add((actor, mark));
        }

        var state = Snapshot();
        foreach (var player in _players.Where(player => player.Actor.ActorId != actor.ActorId))
        {
            player.Actor.Send(new OpponentJoinedNotify(matchId, actor.ActorId));
        }

        log.Add($"play room: join actor={actor.ActorId} mark={mark}");
        return new JoinMatchRes(matchId, actor.ActorId, mark, state);
    }

    public PlaceMarkRes Place(DirectPlayerActor actor, int cell)
    {
        var player = _players.FirstOrDefault(player => player.Actor.ActorId == actor.ActorId);
        if (player.Actor is null)
        {
            throw new InvalidOperationException("Player has not joined the match.");
        }

        if (_winner is not null || _draw)
        {
            throw new InvalidOperationException("Match already ended.");
        }

        if (_turn != actor.ActorId)
        {
            throw new InvalidOperationException($"It is {_turn}'s turn.");
        }

        if ((uint)cell >= 9 || _board[cell] != '-')
        {
            throw new InvalidOperationException($"Cell {cell} is not available.");
        }

        _board[cell] = player.Mark[0];
        _winner = HasWon(player.Mark[0]) ? actor.ActorId : null;
        _draw = _winner is null && _board.All(static cell => cell != '-');
        _turn = player.Mark == "X" ? "player-b" : "player-a";
        var state = Snapshot();
        object notification = _winner is not null || _draw
            ? new GameEndedNotify(matchId, _winner, _draw, state)
            : new TurnChangedNotify(matchId, _turn, state);
        foreach (var target in _players)
        {
            target.Actor.Send(notification);
        }

        log.Add($"play room: move actor={actor.ActorId} cell={cell} board={state.Board}");
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

internal sealed class DirectNotificationCounter
{
    public int OpponentJoined { get; private set; }

    public int TurnChanged { get; private set; }

    public int GameEnded { get; private set; }

    public void Record(object packet)
    {
        switch (packet)
        {
            case OpponentJoinedNotify:
                OpponentJoined++;
                break;
            case TurnChangedNotify:
                TurnChanged++;
                break;
            case GameEndedNotify:
                GameEnded++;
                break;
        }
    }
}

internal sealed class SampleLog
{
    private readonly List<string> _lines = [];

    public IReadOnlyList<string> Lines => _lines;

    public void Add(string line)
    {
        _lines.Add(line);
    }
}
