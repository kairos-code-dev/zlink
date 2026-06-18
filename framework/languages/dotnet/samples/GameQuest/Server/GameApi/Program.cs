using System.Buffers.Binary;
using System.Net.Sockets;
using System.Net.WebSockets;
using GameQuest.GameApi.Adapters.Store;
using GameQuest.GameApi.Application;
using GameQuest.GameApi.Session;
using GameQuest.Shared;
using GameQuest.Server.Configuration;
using Microsoft.AspNetCore.Mvc;
using Zlink.Framework.AspNetCore;

namespace GameQuest.GameApi;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var topology = GameQuestTopology.FromEnvironment();
        var apiName = Environment.GetEnvironmentVariable("GAMEQUEST_API_NAME") ?? "api-a";
        var builder = WebApplication.CreateBuilder(args);

        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<GameQuestStore>();
        builder.Services.AddSingleton<GameQuestSessionRegistry>();
        builder.Services.AddScoped<GameplayActionService>();
        builder.Services.AddScoped<GameQuestSession>();
        builder.Services.AddScoped<SubscribeQuestHandler>();
        builder.Services.AddScoped<GetQuestProgressHandler>();
        builder.Services.AddScoped<SyncQuestProgressHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.DefaultTimeout = SampleNames.RequestTimeout;
            options.AddHandlersFromAssemblyOf(typeof(Program));
            {
                var channel = options.AddFanoutChannel(SampleNames.FanoutChannel);
                channel.EnablePublisher(topology.FanoutPublisherEndpointForApi(apiName));

            }
            {
                var stream = options.AddStreamNode(SampleNames.StreamNode);
                stream.Bind(Environment.GetEnvironmentVariable("GAMEQUEST_STREAM_BIND_ENDPOINT")
                            ?? throw new InvalidOperationException("GAMEQUEST_STREAM_BIND_ENDPOINT is required."));
                stream.RegisterSession<GameQuestSession>();

            }
        });

        var app = builder.Build();

        app.UseWebSockets();

        app.MapGet("/health", () => Results.Ok(new { status = "ok" }));

        app.Map("/quest/ws", async context =>
        {
            if (!context.WebSockets.IsWebSocketRequest)
            {
                context.Response.StatusCode = StatusCodes.Status400BadRequest;
                return;
            }

            await BridgeWebSocketToStreamAsync(context);
        });

        app.MapPost("/combat/kill", async (
            [FromBody] KillMonsterReq request,
            GameplayActionService actions,
            CancellationToken cancellationToken) =>
        {
            var response = await actions.KillMonsterAsync(request, cancellationToken);
            return Results.Ok(response);
        });

        app.MapPost("/inventory/collect", async (
            [FromBody] CollectItemReq request,
            GameplayActionService actions,
            CancellationToken cancellationToken) =>
        {
            var response = await actions.CollectItemAsync(request, cancellationToken);
            return Results.Ok(response);
        });

        app.MapPost("/mission/complete", async (
            [FromBody] CompleteMissionReq request,
            GameplayActionService actions,
            CancellationToken cancellationToken) =>
        {
            var response = await actions.CompleteMissionAsync(request, cancellationToken);
            return Results.Ok(response);
        });

        app.MapPost("/world/enter", async (
            [FromBody] EnterAreaReq request,
            GameplayActionService actions,
            CancellationToken cancellationToken) =>
        {
            var response = await actions.EnterAreaAsync(request, cancellationToken);
            return Results.Ok(response);
        });

        app.MapPost("/feature/unlock", async (
            [FromBody] UnlockFeatureReq request,
            GameplayActionService actions,
            CancellationToken cancellationToken) =>
        {
            var response = await actions.UnlockFeatureAsync(request, cancellationToken);
            return Results.Ok(response);
        });

        app.MapGet("/quest/progress/{playerId}", async (
            string playerId,
            GameQuestStore store,
            CancellationToken cancellationToken) =>
        {
            var response = new GetQuestProgressRes(await store.ReadProjectionAsync(playerId, cancellationToken));
            return Results.Ok(response);
        });

        app.MapPost("/internal/snapshot", async (
            [FromBody] GetGameplaySnapshotReq request,
            GameQuestStore store,
            CancellationToken cancellationToken) =>
        {
            return Results.Ok(await store.ReadSnapshotAsync(request.PlayerId, cancellationToken));
        });

        app.MapPost("/self-check/gameplay/kill-without-publish/{playerId}", async (
            string playerId,
            GameQuestStore store,
            CancellationToken cancellationToken) =>
        {
            await store.AddUnpublishedKillAsync(playerId, 1, cancellationToken);
            return Results.Ok(new { accepted = true });
        });

        app.MapPost("/self-check/sync/{playerId}", async (
            string playerId,
            GameplayActionService actions,
            CancellationToken cancellationToken) =>
        {
            return Results.Ok(await actions.SyncAsync(playerId, cancellationToken));
        });

        app.MapPost("/self-check/projection/{playerId}/{questId}/delete", async (
            string playerId,
            string questId,
            GameQuestStore store,
            CancellationToken cancellationToken) =>
        {
            await store.DeleteProjectionAsync(playerId, questId, cancellationToken);
            return Results.Ok(new { deleted = true });
        });

        app.MapPost("/self-check/projection/{playerId}/{questId}/rebuild", async (
            string playerId,
            string questId,
            GameQuestStore store,
            CancellationToken cancellationToken) =>
        {
            return Results.Ok(await store.RebuildProjectionAsync(playerId, questId, cancellationToken));
        });

        app.MapPost("/internal/notify", async (
            [FromBody] NotifyQuestProgressReq request,
            GameQuestSessionRegistry registry,
            CancellationToken cancellationToken) =>
        {
            var delivered = await registry.NotifyAsync(request, cancellationToken);
            return Results.Ok(new NotifyQuestProgressRes(delivered));
        });

        app.MapPost("/self-check/assert", async (
            GameQuestStore store,
            CancellationToken cancellationToken) =>
        {
            var alice = await store.ReadProjectionAsync("player-alice", cancellationToken);
            var bob = await store.ReadProjectionAsync("player-bob", cancellationToken);
            var evidence = alice.Concat(bob)
                .Select(p => $"{p.PlayerId}:{p.QuestId}:{p.Status}:{p.CurrentCount}/{p.RequiredCount}")
                .Order(StringComparer.Ordinal)
                .ToArray();
            var bindings = await store.ReadBindingHistoryAsync(cancellationToken);
            var activeBindings = await store.ReadBindingsAsync(cancellationToken);
            var events = await store.ReadQuestEventsAsync(cancellationToken);
            var passed = alice.Any(p => p is { QuestId: QuestIds.FirstHunt, Status: QuestStatuses.RewardGranted })
                         && alice.Any(p => p is { QuestId: QuestIds.OpenAuction, Status: QuestStatuses.RewardGranted })
                         && bob.Any(p => p is { QuestId: QuestIds.HerbGathering, Status: QuestStatuses.RewardGranted })
                         && bindings.Any(binding => binding.PlayerId == "player-bob" && binding.GameApiInstanceId == "api-b")
                         && activeBindings.All(binding => binding.PlayerId != "player-alice")
                         && Count(events, "player-alice", QuestIds.FirstHunt, nameof(QuestProgressedEvent)) == 3
                         && Count(events, "player-alice", QuestIds.FirstHunt, nameof(QuestCompletedEvent)) == 1
                         && Count(events, "player-alice", QuestIds.FirstHunt, nameof(QuestRewardGrantedEvent)) == 1
                         && Count(events, "player-alice", QuestIds.FirstHunt, nameof(QuestProgressReconciledEvent)) == 1
                         && Count(events, "player-alice", QuestIds.OpenAuction, nameof(QuestCompletedEvent)) == 1
                         && Count(events, "player-alice", QuestIds.OpenAuction, nameof(QuestRewardGrantedEvent)) == 1
                         && Count(events, "player-bob", QuestIds.HerbGathering, nameof(QuestCompletedEvent)) == 1
                         && Count(events, "player-bob", QuestIds.HerbGathering, nameof(QuestRewardGrantedEvent)) == 1
                         && events
                             .GroupBy(e => (e.PlayerId, e.QuestId, e.Version))
                             .All(group => group.Count() == 1);
            return Results.Ok(new GameQuestServerAssertRes(
                passed,
                evidence.Concat(bindings.Select(binding =>
                        $"binding:{binding.PlayerId}:{binding.ConnectionId}:{binding.GameApiInstanceId}"))
                    .Concat(events.Select(e =>
                        $"event:{e.PlayerId}:{e.QuestId}:{e.EventType}:v{e.Version}:source={e.SourceEventId}"))
                    .Order(StringComparer.Ordinal)
                    .ToArray()));
        });

        await app.RunAsync();
    }

    private static async Task BridgeWebSocketToStreamAsync(HttpContext context)
{
    var bindEndpoint = Environment.GetEnvironmentVariable("GAMEQUEST_STREAM_BIND_ENDPOINT")
                       ?? throw new InvalidOperationException("GAMEQUEST_STREAM_BIND_ENDPOINT is required.");
    var target = new Uri(bindEndpoint);
    using var tcp = new TcpClient();
    await tcp.ConnectAsync(target.Host, target.Port, context.RequestAborted);
    await using var stream = tcp.GetStream();
    using var webSocket = await context.WebSockets.AcceptWebSocketAsync();

    var webSocketToStream = CopyWebSocketToStreamAsync(webSocket, stream, context.RequestAborted);
    var streamToWebSocket = CopyStreamFramesToWebSocketAsync(stream, webSocket, context.RequestAborted);
    await Task.WhenAny(webSocketToStream, streamToWebSocket);

    tcp.Close();
    if (webSocket.State is WebSocketState.Open or WebSocketState.CloseReceived)
    {
        await webSocket.CloseAsync(WebSocketCloseStatus.NormalClosure, "closed", CancellationToken.None);
    }
}

static int Count(
    IEnumerable<StoredQuestEvent> events,
    string playerId,
    string questId,
    string eventType) =>
    events.Count(e =>
        e.PlayerId == playerId
        && e.QuestId == questId
        && e.EventType == eventType);

static async Task CopyWebSocketToStreamAsync(
    WebSocket webSocket,
    NetworkStream stream,
    CancellationToken cancellationToken)
{
    var buffer = new byte[8192];
    while (!cancellationToken.IsCancellationRequested && webSocket.State == WebSocketState.Open)
    {
        var result = await webSocket.ReceiveAsync(buffer, cancellationToken);
        if (result.MessageType == WebSocketMessageType.Close)
        {
            return;
        }

        if (result.MessageType != WebSocketMessageType.Binary)
        {
            await webSocket.CloseAsync(WebSocketCloseStatus.InvalidMessageType, "binary frames only", cancellationToken);
            return;
        }

        if (result.Count > 0)
        {
            await stream.WriteAsync(buffer.AsMemory(0, result.Count), cancellationToken);
        }
    }
}

static async Task CopyStreamFramesToWebSocketAsync(
    NetworkStream stream,
    WebSocket webSocket,
    CancellationToken cancellationToken)
{
    var prefix = new byte[6];
    while (!cancellationToken.IsCancellationRequested && webSocket.State == WebSocketState.Open)
    {
        if (!await ReadExactOrCloseAsync(stream, prefix, cancellationToken))
        {
            return;
        }

        var headerLength = BinaryPrimitives.ReadUInt16BigEndian(prefix.AsSpan(0, 2));
        var payloadLength = BinaryPrimitives.ReadUInt32BigEndian(prefix.AsSpan(2, 4));
        var frame = new byte[checked(6 + headerLength + payloadLength)];
        prefix.CopyTo(frame.AsSpan(0, 6));
        if (!await ReadExactOrCloseAsync(stream, frame.AsMemory(6), cancellationToken))
        {
            return;
        }

        await webSocket.SendAsync(frame, WebSocketMessageType.Binary, true, cancellationToken);
    }
}

static async ValueTask<bool> ReadExactOrCloseAsync(
    NetworkStream stream,
    Memory<byte> buffer,
    CancellationToken cancellationToken)
{
    var offset = 0;
    while (offset < buffer.Length)
    {
        var count = await stream.ReadAsync(buffer[offset..], cancellationToken);
        if (count == 0)
        {
            return false;
        }

        offset += count;
    }

    return true;
    }
}
