using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;
using Systems.Zlink.Stream.Connector;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Json;

namespace Bingo.Client;

internal sealed class BingoPlayerClient(
    string actorId,
    BingoNotificationInbox notifications,
    IZlinkStreamConnector connector) : IAsyncDisposable
{
    public string ActorId => actorId;

    public IReadOnlyList<PlayerJoinedNotify> PlayerJoinedNotifications => notifications.PlayerJoined;

    public IReadOnlyList<BingoGameStartedNotify> StartedNotifications => notifications.Started;

    public IReadOnlyList<BingoNumberDrawnNotify> DrawnNotifications => notifications.Drawn;

    public IReadOnlyList<BingoGameEndedNotify> EndedNotifications => notifications.Ended;

    public static async ValueTask<BingoPlayerClient> ConnectAsync(
        string actorId,
        string streamEndpoint,
        CancellationToken cancellationToken)
    {
        var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(streamEndpoint),
            ConnectTimeout = SampleTimings.ConnectTimeout,
            RequestTimeout = SampleTimings.RequestTimeout,
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
        });
        await connector.ConnectAsync(cancellationToken);

        var inbox = new BingoNotificationInbox();
        inbox.Register(connector);
        return new BingoPlayerClient(actorId, inbox, connector);
    }

    public ValueTask<AuthenticateRes> AuthenticateAsync(CancellationToken cancellationToken)
    {
        return connector
            .Request(new AuthenticateReq(ActorId))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<AuthenticateRes>(cancellationToken);
    }

    public ValueTask<MatchBingoRes> MatchAsync(CancellationToken cancellationToken)
    {
        return connector
            .Request(new MatchBingoReq("four-player"))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<MatchBingoRes>(cancellationToken);
    }

    public ValueTask<StartBingoGameRes> StartAsync(
        string roomId,
        CancellationToken cancellationToken)
    {
        return connector
            .Request(new StartBingoGameReq(roomId))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<StartBingoGameRes>(cancellationToken);
    }

    public Task WaitForNotificationAsync(CancellationToken cancellationToken)
    {
        return notifications.WaitAsync(cancellationToken);
    }

    public ValueTask DisposeAsync()
    {
        return connector.DisposeAsync();
    }
}
