using System.Net;
using System.Net.Http.Json;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;

var mode = args.FirstOrDefault(static arg => !arg.StartsWith("--", StringComparison.Ordinal)) ?? "all";
var settings = SampleSettings.FromArgs(args);

switch (mode)
{
    case "all":
        await RunAllAsync(settings);
        break;
    case "play":
        using (var play = BuildPlayServer(settings))
        {
            await play.RunAsync();
        }
        break;
    case "api":
        await using (var api = BuildApiServer(settings))
        {
            await api.RunAsync();
        }
        break;
    case "client":
        await RunClientAsync(settings, CancellationToken.None);
        break;
    default:
        Console.Error.WriteLine("Usage: dotnet run -- [all|play|api|client] [--api-url URL] [--api-bind URL] [--play-endpoint tcp://HOST:PORT] [--control-endpoint tcp://HOST:PORT] [--spot-endpoint tcp://HOST:PORT]");
        Environment.ExitCode = 2;
        break;
}

static async Task RunAllAsync(SampleSettings settings)
{
    settings = settings.WithEphemeralDefaults();

    var play = BuildPlayServer(settings);
    await using var api = BuildApiServer(settings);

    await play.StartAsync();
    await api.StartAsync();

    try
    {
        await RunClientAsync(settings, CancellationToken.None);
        // The all-in-one smoke path exits after success so native cleanup issues
        // do not hide the sample result. Long-running role modes use normal host
        // lifetimes.
        SampleProcess.ExitImmediately(0);
    }
    finally
    {
        await api.StopAsync();
        await play.StopAsync();
    }
}

static WebApplication BuildApiServer(SampleSettings settings)
{
    var builder = WebApplication.CreateBuilder();
    builder.Logging.ClearProviders();
    builder.WebHost.UseUrls(settings.ApiBindUrl);
    builder.Services.AddZLinkFramework(options =>
    {
        options.AddChannel("play-control", channel =>
        {
            channel.EnableClient(client =>
            {
                client.UseManualConnections(connections =>
                {
                    connections.Connect(settings.ControlEndpoint);
                });
            });
        });
    });

    var app = builder.Build();

    app.MapPost("/rooms", async (
        CreateRoomHttpRequest request,
        IZLinkClient client,
        CancellationToken cancellationToken) =>
    {
        var reply = await client.Request(
                "play-control",
                new CreateRoomCommand(request.RoomName ?? "sample-room"))
            .WithTimeout(TimeSpan.FromSeconds(5))
            .ExecAsync(cancellationToken);

        return Results.Ok(new CreateRoomHttpReply(
            reply.RoomId,
            reply.PlayEndpoint,
            reply.RoomName));
    });

    return app;
}

static IHost BuildPlayServer(SampleSettings settings)
{
    var builder = Host.CreateApplicationBuilder();
    builder.Logging.ClearProviders();

    builder.Services.AddSingleton(settings);
    builder.Services.AddScoped<CreateRoomHandler>();
    builder.Services.AddScoped<RoomStreamSession>();
    builder.Services.AddScoped<RoomJoinHandler>();
    builder.Services.AddZLinkHandlersFromAssemblyContaining<CreateRoomHandler>();
    builder.Services.AddZLinkFramework(options =>
    {
        options.UseSpotDiscovery("game.room", _ => { });
        options.AddActorFactory<PlayerActorFactory>("player");

        options.AddChannel("play-control", channel =>
        {
            channel.EnableServer(server =>
            {
                server.Bind(settings.ControlEndpoint);
            });
        });

        options.AddStreamNode("client-stream", stream =>
        {
            stream.Bind(settings.PlayEndpoint);
            stream.AddRawSession<RoomStreamSession>();
        });

        options.AddSpotNode("play-node", spot =>
        {
            spot.Bind(settings.SpotEndpoint);
            spot.AddSpotFactory<RoomSpot>("room");
        });
    });

    return builder.Build();
}

static async Task RunClientAsync(SampleSettings settings, CancellationToken cancellationToken)
{
    using var http = new HttpClient
    {
        BaseAddress = new Uri(settings.ApiPublicUrl),
        Timeout = TimeSpan.FromSeconds(10),
    };

    var room = await http.PostAsJsonAsync(
            "/rooms",
            new CreateRoomHttpRequest("echo-room"),
            cancellationToken)
        .ContinueWith(static async task =>
        {
            using var response = await task.ConfigureAwait(false);
            response.EnsureSuccessStatusCode();
            return await response.Content.ReadFromJsonAsync<CreateRoomHttpReply>()
                .ConfigureAwait(false)
                ?? throw new InvalidOperationException("API returned an empty room response.");
        }, cancellationToken)
        .Unwrap()
        .ConfigureAwait(false);

    Console.WriteLine($"api: created room {room.RoomId} at {room.PlayEndpoint}");

    using var tcp = ConnectToPlay(room.PlayEndpoint);
    var stream = tcp.GetStream();
    stream.ReadTimeout = 5000;
    stream.WriteTimeout = 5000;

    SendLine(stream, $"join-room|{room.RoomId}|player-1");
    var join = ReceiveLine(stream);
    Console.WriteLine($"client: {join}");

    SendLine(stream, "echo|o");
    var echo = ReceiveLine(stream);
    Console.WriteLine($"client: {echo}");

    if (!string.Equals(echo, "echo-reply|o", StringComparison.Ordinal))
    {
        throw new InvalidOperationException($"Unexpected echo reply: {echo}");
    }
}

static TcpClient ConnectToPlay(string zlinkEndpoint)
{
    var uri = new Uri(zlinkEndpoint);
    var client = new TcpClient();
    client.Connect(IPAddress.Parse(uri.Host), uri.Port);
    return client;
}

static void SendLine(NetworkStream stream, string line)
{
    var payload = Encoding.UTF8.GetBytes(line + "\n");
    stream.Write(payload);
    stream.Flush();
}

static string ReceiveLine(NetworkStream stream)
{
    var bytes = new List<byte>();
    while (true)
    {
        var value = stream.ReadByte();
        if (value == '\n')
        {
            return Encoding.UTF8.GetString(bytes.ToArray());
        }

        if (value == '\r')
        {
            continue;
        }

        if (value < 0)
        {
            throw new IOException("The play server closed the stream.");
        }

        bytes.Add((byte)value);
        if (bytes.Count > 64 * 1024)
        {
            throw new InvalidDataException("Line frame is too large.");
        }
    }
}

sealed record SampleSettings(
    string ApiBindUrl,
    string ApiPublicUrl,
    string ControlEndpoint,
    string PlayEndpoint,
    string SpotEndpoint)
{
    public static SampleSettings FromArgs(string[] args)
    {
        string? apiBind = null;
        string? apiUrl = null;
        string? control = null;
        string? play = null;
        string? spot = null;

        for (var i = 0; i < args.Length; i++)
        {
            string? ReadValue()
            {
                if (i + 1 >= args.Length)
                {
                    throw new ArgumentException($"Missing value for '{args[i]}'.");
                }

                i++;
                return args[i];
            }

            switch (args[i])
            {
                case "--api-bind":
                    apiBind = ReadValue();
                    break;
                case "--api-url":
                    apiUrl = ReadValue();
                    break;
                case "--control-endpoint":
                    control = ReadValue();
                    break;
                case "--play-endpoint":
                    play = ReadValue();
                    break;
                case "--spot-endpoint":
                    spot = ReadValue();
                    break;
            }
        }

        return new SampleSettings(
            apiBind ?? "http://127.0.0.1:18080",
            apiUrl ?? apiBind ?? "http://127.0.0.1:18080",
            control ?? "tcp://127.0.0.1:18081",
            play ?? "tcp://127.0.0.1:18082",
            spot ?? "tcp://127.0.0.1:18083");
    }

    public SampleSettings WithEphemeralDefaults()
    {
        var apiPort = SamplePorts.Reserve();
        return this with
        {
            ApiBindUrl = $"http://127.0.0.1:{apiPort}",
            ApiPublicUrl = $"http://127.0.0.1:{apiPort}",
            ControlEndpoint = $"tcp://127.0.0.1:{SamplePorts.Reserve()}",
            PlayEndpoint = $"tcp://127.0.0.1:{SamplePorts.Reserve()}",
            SpotEndpoint = $"tcp://127.0.0.1:{SamplePorts.Reserve()}",
        };
    }
}

static class SamplePorts
{
    public static int Reserve()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        return ((IPEndPoint)listener.LocalEndpoint).Port;
    }
}

static class SampleProcess
{
    public static void ExitImmediately(int exitCode)
    {
        if (OperatingSystem.IsWindows())
        {
            Environment.Exit(exitCode);
        }

        Exit(exitCode);
    }

    [DllImport("libc", EntryPoint = "_exit")]
    private static extern void Exit(int status);
}

sealed class CreateRoomHandler(IZLinkSpotManager spots, SampleSettings settings)
{
    [ZLinkRequest]
    public async ValueTask<CreateRoomReply> CreateAsync(
        CreateRoomCommand request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var created = await spots.CreateAsync("room", cancellationToken);
        return new CreateRoomReply(
            created.SpotRid.ToHex(),
            settings.PlayEndpoint,
            request.RoomName);
    }
}

sealed class RoomSpot : ZLinkSpot
{
    public RoomSpot(RoutingId spotRid, RoutingId nodeRid)
        : base(spotRid, nodeRid)
    {
        AddActorJoin<RoomJoinHandler, JoinRoomRequest, JoinRoomReply>();
    }
}

sealed class RoomJoinHandler : IZLinkSpotActorJoinHandler<RoomSpot, JoinRoomRequest, JoinRoomReply>
{
    public async ValueTask<JoinRoomReply> HandleAsync(
        RoomSpot spot,
        IZLinkActor actor,
        JoinRoomRequest request,
        CancellationToken cancellationToken)
    {
        if (actor is not PlayerActor player)
        {
            throw new InvalidOperationException("Room join requires a PlayerActor.");
        }

        player.RoomId = request.RoomId;
        player.PlayerId = request.PlayerId;
        await player.OnAttachedAsync(spot, cancellationToken);
        return new JoinRoomReply(request.RoomId, request.PlayerId);
    }
}

sealed class RoomStreamSession(
    IZLinkSpotClient spotClient,
    IZLinkActorRuntime actorRuntime)
    : IZLinkRawStreamSession
{
    private readonly PlayerActor _actor = new(Guid.NewGuid().ToString("N"));
    private readonly StringBuilder _buffer = new();

    public ValueTask OnConnectedAsync(IZLinkStream stream, CancellationToken cancellationToken)
    {
        return actorRuntime.AttachAsync(_actor, stream, cancellationToken);
    }

    public ValueTask OnDisconnectedAsync(IZLinkStream stream, CancellationToken cancellationToken)
    {
        return actorRuntime.DisconnectAsync(_actor, stream, cancellationToken);
    }

    public ValueTask OnErrorAsync(
        IZLinkStream stream,
        ZLinkStreamError error,
        CancellationToken cancellationToken)
    {
        _ = stream;
        _ = error;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnRawAsync(
        IZLinkStream stream,
        Message payload,
        CancellationToken cancellationToken)
    {
        _buffer.Append(payload.GetString());
        while (TryReadLine(_buffer, out var line))
        {
            var parts = line.Split('|', 3);
            if (parts.Length == 0)
            {
                continue;
            }

            if (string.Equals(parts[0], "join-room", StringComparison.Ordinal))
            {
                if (parts.Length != 3)
                {
                    throw new InvalidOperationException("join-room requires room id and player id.");
                }

                var roomId = parts[1];
                var playerId = parts[2];
                var spotRid = RoutingId.FromBytes(Convert.FromHexString(roomId));
                _ = await spotClient.JoinActorAsync<JoinRoomRequest, JoinRoomReply>(
                    spotRid,
                    _actor,
                    new JoinRoomRequest(roomId, playerId),
                    cancellationToken);

                using var reply = Message.FromString($"join-room-reply|{roomId}|{playerId}\n");
                stream.Write(reply);
                continue;
            }

            if (string.Equals(parts[0], "echo", StringComparison.Ordinal))
            {
                using var header = Message.FromString("echo");
                using var body = Message.FromString(parts.Length == 2 ? parts[1] : string.Empty);
                await actorRuntime.SubmitAsync(_actor, header, body, cancellationToken);
            }
        }
    }

    private static bool TryReadLine(StringBuilder buffer, out string line)
    {
        for (var i = 0; i < buffer.Length; i++)
        {
            if (buffer[i] != '\n')
            {
                continue;
            }

            line = buffer.ToString(0, i).TrimEnd('\r');
            buffer.Remove(0, i + 1);
            return true;
        }

        line = string.Empty;
        return false;
    }
}

sealed class PlayerActor(string actorKey) : IZLinkActor
{
    public string ActorKey { get; } = actorKey;

    public IZLinkStream? Stream { get; private set; }

    public ZLinkSpot? Spot { get; private set; }

    public string RoomId { get; set; } = string.Empty;

    public string PlayerId { get; set; } = string.Empty;

    public ValueTask AttachAsync(IZLinkStream stream, CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        Stream = stream;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnAttachedAsync(ZLinkSpot spot, CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        Spot = spot;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDetachedAsync(ZLinkSpot spot, CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        if (ReferenceEquals(Spot, spot))
        {
            Spot = null;
        }

        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        Stream = null;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDispatchAsync(
        Message header,
        Message body,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        if (Stream is null)
        {
            throw new InvalidOperationException("Actor has no active client stream.");
        }

        if (Spot is null)
        {
            throw new InvalidOperationException("Actor has not joined a room.");
        }

        var packetName = header.GetString();
        if (!string.Equals(packetName, "echo", StringComparison.Ordinal))
        {
            return ValueTask.CompletedTask;
        }

        using var reply = Message.FromString($"echo-reply|{body.GetString()}\n");
        Stream.Write(reply);
        return ValueTask.CompletedTask;
    }
}

sealed class PlayerActorFactory;

sealed record CreateRoomHttpRequest(string? RoomName);

sealed record CreateRoomHttpReply(string RoomId, string PlayEndpoint, string RoomName);

sealed record CreateRoomCommand(string RoomName) : IZLinkRequest<CreateRoomReply>;

sealed record CreateRoomReply(string RoomId, string PlayEndpoint, string RoomName);

sealed record JoinRoomRequest(string RoomId, string PlayerId) : IZLinkRequest<JoinRoomReply>;

sealed record JoinRoomReply(string RoomId, string PlayerId);
