using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Json;

namespace Bingo.Client;

internal sealed class BingoPlayerClient(
    string actorId,
    BingoNotificationInbox notifications,
    IZlinkStreamConnector connector,
    CancellationToken cancellationToken) : IAsyncDisposable
{
    public string ActorId => actorId;

    public IReadOnlyList<PlayerJoinedNotify> PlayerJoinedNotifications => notifications.Snapshot<PlayerJoinedNotify>();

    public IReadOnlyList<BingoGameStartedNotify> StartedNotifications => notifications.Snapshot<BingoGameStartedNotify>();

    public IReadOnlyList<BingoNumberDrawnNotify> DrawnNotifications => notifications.Snapshot<BingoNumberDrawnNotify>();

    public IReadOnlyList<BingoGameEndedNotify> EndedNotifications => notifications.Snapshot<BingoGameEndedNotify>();

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
        RegisterNotificationHandlers(connector, inbox);
        return new BingoPlayerClient(actorId, inbox, connector, cancellationToken);
    }

    public ValueTask<AuthenticateRes> AuthenticateAsync()
    {
        return connector
            .Request(new AuthenticateReq(ActorId))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<AuthenticateRes>(cancellationToken);
    }

    public ValueTask<MatchBingoRes> MatchAsync()
    {
        return connector
            .Request(new MatchBingoReq("four-player"))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<MatchBingoRes>(cancellationToken);
    }

    public ValueTask<StartBingoGameRes> StartAsync(
        string roomId)
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

    private static void RegisterNotificationHandlers(
        IZlinkStreamConnector connector,
        BingoNotificationInbox inbox)
    {
        RegisterNotificationHandler<PlayerJoinedNotify>(connector, inbox);
        RegisterNotificationHandler<BingoGameStartedNotify>(connector, inbox);
        RegisterNotificationHandler<BingoNumberDrawnNotify>(connector, inbox);
        RegisterNotificationHandler<BingoStateNotify>(connector, inbox);
        RegisterNotificationHandler<BingoGameEndedNotify>(connector, inbox);
    }

    private static void RegisterNotificationHandler<TMessage>(
        IZlinkStreamConnector connector,
        BingoNotificationInbox inbox)
    {
        _ = connector.On<TMessage>((message, _) =>
        {
            inbox.Enqueue(message.Payload!);
            return ValueTask.CompletedTask;
        });
    }
}
