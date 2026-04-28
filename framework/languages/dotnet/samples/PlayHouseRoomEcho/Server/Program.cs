using Systems.Zlink.Stream.Connector.Connector;
using Systems.Zlink.Stream.Connector.Options;
namespace PlayHouseRoomEcho.Server;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var mode = args.FirstOrDefault(static arg => !arg.StartsWith("--", StringComparison.Ordinal)) ?? "all";
        var settings = SampleSettings.FromArgs(args);

        switch (mode)
        {
            case "all":
                await RunAllAsync(settings).ConfigureAwait(false);
                break;
            case "play":
                using (var play = new PlayServer(settings).Build())
                {
                    await play.RunAsync().ConfigureAwait(false);
                }
                break;
            case "api":
                await using (var api = new ApiServer(settings).Build())
                {
                    await api.RunAsync().ConfigureAwait(false);
                }
                break;
            case "client":
                await RunClientAsync(settings, CancellationToken.None).ConfigureAwait(false);
                break;
            default:
                await Console.Error.WriteLineAsync("Usage: dotnet run -- [all|play|api|client] [--api-url URL] [--api-bind URL] [--api-channel-endpoint tcp://HOST:PORT] [--play-channel-endpoint tcp://HOST:PORT] [--play-endpoint tcp://HOST:PORT] [--spot-endpoint tcp://HOST:PORT] [--log-dir DIR]");
                Environment.ExitCode = 2;
                break;
        }
    }

    private static async Task RunAllAsync(SampleSettings settings)
    {
        settings = settings.WithEphemeralDefaults();

        var play = new PlayServer(settings).Build();
        await using var api = new ApiServer(settings).Build();

        await play.StartAsync().ConfigureAwait(false);
        await api.StartAsync().ConfigureAwait(false);

        try
        {
            await RunClientAsync(settings, CancellationToken.None).ConfigureAwait(false);
            // The all-in-one smoke path exits after success so native cleanup issues
            // do not hide the sample result. Long-running role modes use normal host
            // lifetimes.
            SampleProcess.ExitImmediately(0);
        }
        finally
        {
            await api.StopAsync().ConfigureAwait(false);
            await play.StopAsync().ConfigureAwait(false);
        }
    }

    private static async Task RunClientAsync(SampleSettings settings, CancellationToken cancellationToken)
    {
        using var loggerFactory = SampleLogging.CreateLoggerFactory(settings, "client");
        var logger = loggerFactory.CreateLogger("Game.Client");
        using var http = new HttpClient
        {
            BaseAddress = new Uri(settings.ApiPublicUrl),
            Timeout = TimeSpan.FromSeconds(10),
        };

        logger.LogInformation("client -> api: posting create room request. api={ApiUrl}", settings.ApiPublicUrl);
        var room = await CreateRoomViaHttpAsync(http, cancellationToken).ConfigureAwait(false);
        logger.LogInformation(
            "api -> client: room info received. roomId={RoomId}, endpoint={Endpoint}, room={RoomName}",
            room.RoomId,
            room.PlayEndpoint,
            room.RoomName);
        Console.WriteLine($"api: created room {room.RoomId} at {room.PlayEndpoint}");

        logger.LogInformation("client -> play stream: connecting. endpoint={Endpoint}", room.PlayEndpoint);
        await using var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(room.PlayEndpoint),
            ConnectTimeout = TimeSpan.FromSeconds(5),
            SendTimeout = TimeSpan.FromSeconds(5),
            RequestTimeout = TimeSpan.FromSeconds(5),
        }, cancellationToken).ConfigureAwait(false);
        logger.LogInformation("client -> play stream: connected. endpoint={Endpoint}", room.PlayEndpoint);

        logger.LogInformation(
            "client -> play stream: JoinRoom request. roomId={RoomId}, player={PlayerId}",
            room.RoomId,
            "player-1");
        var join = await connector
            .Request(new JoinRoom(room.RoomId, "player-1"))
            .ExecAsync<JoinRoomAccepted>(cancellationToken)
            .ConfigureAwait(false);
        logger.LogInformation(
            "play stream -> client: JoinRoomAccepted response. roomId={RoomId}, player={PlayerId}",
            join.RoomId,
            join.PlayerId);
        Console.WriteLine($"client: {nameof(JoinRoomAccepted)}|{join.RoomId}|{join.PlayerId}");

        logger.LogInformation("client -> play stream: Echo request. text={Text}", "o");
        var echo = await connector
            .Request(new Echo("o"))
            .ExecAsync<EchoReply>(cancellationToken)
            .ConfigureAwait(false);
        logger.LogInformation("play stream -> client: EchoReply response. text={Text}", echo.Text);
        Console.WriteLine($"client: {nameof(EchoReply)}|{echo.Text}");

        if (!string.Equals(echo.Text, "o", StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"Unexpected echo reply: {nameof(EchoReply)}|{echo.Text}");
        }
    }

    private static async Task<CreateRoomHttpReply> CreateRoomViaHttpAsync(
        HttpClient http,
        CancellationToken cancellationToken)
    {
        using var response = await http.PostAsJsonAsync(
                "/rooms",
                new CreateRoomHttpRequest("echo-room"),
                cancellationToken)
            .ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<CreateRoomHttpReply>(cancellationToken)
                   .ConfigureAwait(false)
               ?? throw new InvalidOperationException("API returned an empty room response.");
    }

}
