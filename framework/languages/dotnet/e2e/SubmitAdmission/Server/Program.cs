using Systems.Zlink;
using SubmitAdmission.Server;
using SubmitAdmission.Server.Infrastructure;
using SubmitAdmission.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;

var options = ServerOptions.Parse(args);
var builder = WebApplication.CreateBuilder(args);
builder.Configuration.Sources.Clear();
builder.Configuration.AddInMemoryCollection();
builder.Logging.ClearProviders();
builder.Logging.AddSimpleConsole(console => console.SingleLine = true);
builder.WebHost.UseUrls(options.HttpUrl);
builder.Services.AddSingleton(options);
builder.Services.AddSingleton<OperationEvidenceStore>();
builder.Services.AddSingleton<HandlerGate>();
builder.Services.AddSingleton<PendingCancellationRegistry>();

IZLinkMeshPeerConnections? peerConnections = null;
builder.Services.AddZLinkFramework(framework =>
{
    if (options.Role is "caller" or "target")
    {
        var mesh = framework.AddRouteMesh(SubmitAdmissionNames.Mesh)
            .Listen(Require(options.MeshEndpoint, nameof(options.MeshEndpoint)))
            .SetRoutingId(RoutingId.From(options.Rid));
        var routerSocket = mesh.ConfigureRouterSocket();
        routerSocket.SendHighWaterMark = 1;
        routerSocket.ReceiveHighWaterMark = 1;
        mesh.AddRouteSendHandler<NodeAdmissionHandler, AdmissionMessage>();
        mesh.AddRouteRequestHandler<NodeReadyHandler, RouteReadyRequest, RouteReadyReply>();
        var channel = mesh.ChannelName(SubmitAdmissionNames.Channel);
        channel.SetWeight(100);
        channel.AddSendHandler<ChannelAdmissionHandler, AdmissionMessage>();
        channel.AddRequestHandler<ChannelReadyHandler, RouteReadyRequest, RouteReadyReply>();

        if (!string.IsNullOrWhiteSpace(options.PeerEndpoint))
        {
            mesh.PeerConnections.Connect(
                RoutingId.From(Require(options.PeerRid, nameof(options.PeerRid))),
                options.PeerEndpoint);
        }
        peerConnections = mesh.PeerConnections;
    }
    else if (options.Role == "publisher")
    {
        framework.AddFanoutChannel(SubmitAdmissionNames.Fanout)
            .EnablePublisher(Require(options.FanoutEndpoint, nameof(options.FanoutEndpoint)));
    }
    else
    {
        throw new InvalidOperationException($"Unknown role: {options.Role}");
    }
});
if (peerConnections is not null) builder.Services.AddSingleton(peerConnections);

var app = builder.Build();
var admissionObserver = new AdmissionActivityObserver(
    app.Services.GetRequiredService<OperationEvidenceStore>());
app.Lifetime.ApplicationStopped.Register(admissionObserver.Dispose);
app.MapGet("/health", () => Results.Ok(new { options.Role, options.Rid }));
app.MapGet("/evidence/{operationId}", (string operationId, OperationEvidenceStore evidence) =>
{
    var snapshot = evidence.Snapshot(operationId);
    return snapshot is null ? Results.NotFound() : Results.Ok(snapshot);
});
app.MapGet("/evidence-handler-completed-count", (OperationEvidenceStore evidence) =>
    Results.Ok(new { count = evidence.HandlerCompletedCount }));
app.MapPost("/gate/close", (HandlerGate gate) => { gate.Close(); return Results.Ok(); });
app.MapPost("/gate/open", (HandlerGate gate) => { gate.Open(); return Results.Ok(); });

if (options.Role == "caller")
{
    app.MapGet("/ready/{targetRid}", async (
        string targetRid,
        IZLinkRouteClient routes,
        CancellationToken cancellationToken) =>
    {
        var marker = Guid.NewGuid().ToString("N");
        var reply = await routes.RequestToNode(
                SubmitAdmissionNames.Mesh,
                RoutingId.From(targetRid),
                new RouteReadyRequest(marker))
            .Async<RouteReadyReply>(cancellationToken);
        return Results.Ok(reply);
    });

    app.MapPost("/submit/node/{targetRid}", async (
        string targetRid,
        AdmissionMessage message,
        IZLinkRouteClient routes,
        OperationEvidenceStore evidence,
        CancellationToken cancellationToken) =>
    {
        const string family = "node-direct";
        evidence.Invocation(message.OperationId, family, targetRid);
        evidence.BindCurrentTrace(message.OperationId);
        await routes.SendToNode(
                SubmitAdmissionNames.Mesh,
                RoutingId.From(targetRid),
                message)
            .Metadata("scenario", message.OperationId)
            .Async(cancellationToken);
        evidence.Terminal(message.OperationId, family, targetRid, "Submitted");
        return Results.Ok(new SubmitResponse(message.OperationId, family, "Submitted", 1, 1));
    });

    app.MapPost("/submit/fill/node/{targetRid}", async (
        string targetRid,
        int? count,
        bool? cancelable,
        IZLinkRouteClient routes,
        OperationEvidenceStore evidence,
        PendingCancellationRegistry cancellations) =>
    {
        const string family = "node-direct";
        const int payloadBytes = 32768;
        var operationCount = Math.Clamp(count ?? 64, 1, 64);
        for (var sequence = 0; sequence < operationCount; sequence++)
        {
            var candidate = await StartFillCandidateAsync(
                    Task.CompletedTask,
                    sequence,
                    payloadBytes,
                    targetRid,
                    family,
                    routes,
                    evidence,
                    cancelable == true,
                    cancellations)
                .ConfigureAwait(false);
            if (candidate.Pending
                || candidate.TerminalStatus != "Submitted")
                return Results.Ok(new FillResponse(
                    candidate.OperationId,
                    candidate.Pending,
                    sequence + 1,
                    candidate.TerminalStatus));
        }

        return Results.Ok(new FillResponse(string.Empty, false, operationCount, null));
    });

    app.MapPost("/admin/cancel/{operationId}", (
        string operationId,
        PendingCancellationRegistry cancellations) =>
        cancellations.Cancel(operationId) ? Results.Ok() : Results.NotFound());

    app.MapPost("/submit/channel", async (
        AdmissionMessage message,
        IZLinkRouteClient routes,
        OperationEvidenceStore evidence,
        CancellationToken cancellationToken) =>
    {
        const string family = "channel";
        evidence.Invocation(message.OperationId, family, SubmitAdmissionNames.Channel);
        await routes.SendToChannel(
                SubmitAdmissionNames.Channel,
                message)
            .Async(cancellationToken);
        evidence.Terminal(message.OperationId, family, SubmitAdmissionNames.Channel, "Submitted");
        return Results.Ok(new SubmitResponse(message.OperationId, family, "Submitted", 1, 1));
    });

    app.MapPost("/submit/pre-cancelled/{targetRid}", async (
        string targetRid,
        AdmissionMessage message,
        IZLinkRouteClient routes,
        OperationEvidenceStore evidence) =>
    {
        const string family = "node-direct";
        evidence.Invocation(message.OperationId, family, targetRid);
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        try
        {
            await routes.SendToNode(
                    SubmitAdmissionNames.Mesh,
                    RoutingId.From(targetRid),
                    message)
                .Metadata("scenario", message.OperationId)
                .Async(cancellation.Token);
            evidence.Terminal(message.OperationId, family, targetRid, "Submitted");
            return Results.Ok(new CancellationResponse(
                message.OperationId, "Submitted", string.Empty, 1, 0, 1));
        }
        catch (OperationCanceledException exception)
        {
            evidence.Terminal(message.OperationId, family, targetRid, "Cancelled");
            return Results.Ok(new CancellationResponse(
                message.OperationId, "Cancelled", exception.GetType().Name, 1, 0, 1));
        }
    });

    app.MapPost("/submit/invalid-pre-cancelled/{targetRid}", async (
        string targetRid,
        string operationId,
        IZLinkRouteClient routes,
        OperationEvidenceStore evidence) =>
    {
        const string family = "node-direct";
        evidence.Invocation(operationId, family, targetRid, invalid: true);
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        try
        {
            await routes.SendToNode<AdmissionMessage>(
                    SubmitAdmissionNames.Mesh,
                    RoutingId.From(targetRid),
                    null!)
                .Async(cancellation.Token);
            evidence.Terminal(operationId, family, targetRid, "Submitted");
            return Results.Ok(new CancellationResponse(
                operationId, "Submitted", string.Empty, 1, 1, 1));
        }
        catch (Exception exception)
        {
            evidence.Terminal(operationId, family, targetRid, exception.GetType().Name);
            return Results.Ok(new CancellationResponse(
                operationId, "Invalid", exception.GetType().Name, 1, 1, 1));
        }
    });

    app.MapPost("/admin/disconnect", (IZLinkMeshPeerConnections connections) =>
    {
        connections.Disconnect(Require(options.PeerEndpoint, nameof(options.PeerEndpoint)));
        return Results.Ok();
    });
    app.MapPost("/admin/connect", (IZLinkMeshPeerConnections connections) =>
    {
        connections.Connect(
            RoutingId.From(Require(options.PeerRid, nameof(options.PeerRid))),
            Require(options.PeerEndpoint, nameof(options.PeerEndpoint)));
        return Results.Ok();
    });
    app.MapPost("/admin/stop-twice", (HttpResponse response, IHostApplicationLifetime lifetime) =>
    {
        response.OnCompleted(() =>
        {
            lifetime.StopApplication();
            lifetime.StopApplication();
            return Task.CompletedTask;
        });
        return Results.Accepted();
    });
}

if (options.Role == "publisher")
{
    app.MapPost("/submit/fanout", async (
        AdmissionMessage message,
        IZLinkFanoutClient fanout,
        OperationEvidenceStore evidence,
        CancellationToken cancellationToken) =>
    {
        const string family = "fanout";
        evidence.Invocation(message.OperationId, family, "subscriber-zero");
        await fanout.Publish(
                SubmitAdmissionNames.Fanout,
                "admission",
                message)
            .Async(cancellationToken);
        evidence.Terminal(message.OperationId, family, "subscriber-zero", "Submitted");
        return Results.Ok(new SubmitResponse(message.OperationId, family, "Submitted", 1, 1));
    });
}

await app.RunAsync();

static string Require(string? value, string name) =>
    string.IsNullOrWhiteSpace(value)
        ? throw new InvalidOperationException($"{name} is required.")
        : value;

static async Task RecordTerminalAsync(
    Task terminal,
    string operationId,
    string family,
    string targetRid,
    OperationEvidenceStore evidence,
    PendingCancellationRegistry? cancellations = null)
{
    try
    {
        await terminal.ConfigureAwait(false);
        evidence.Terminal(operationId, family, targetRid, "Submitted");
    }
    catch (Exception exception)
    {
        evidence.Terminal(operationId, family, targetRid, exception.GetType().Name);
    }
    finally
    {
        cancellations?.Complete(operationId);
    }
}

static async Task<FillCandidate> StartFillCandidateAsync(
    Task start,
    int sequence,
    int payloadBytes,
    string targetRid,
    string family,
    IZLinkRouteClient routes,
    OperationEvidenceStore evidence,
    bool cancelable,
    PendingCancellationRegistry cancellations)
{
    await start.ConfigureAwait(false);
    using var activity = new System.Diagnostics.Activity("submit-admission-fill").Start();
    var operationId = $"fill-{Guid.NewGuid():N}";
    var empty = new AdmissionMessage(operationId, sequence, string.Empty);
    var jsonOptions = new System.Text.Json.JsonSerializerOptions(
        System.Text.Json.JsonSerializerDefaults.Web);
    var envelopeBytes = System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(empty, jsonOptions).Length;
    var message = empty with { Payload = new string('x', payloadBytes - envelopeBytes) };
    if (System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(message, jsonOptions).Length != payloadBytes)
        throw new InvalidOperationException("SubmitAdmission setup payload is not exactly 32768 bytes.");
    evidence.Invocation(message.OperationId, family, targetRid);
    evidence.BindCurrentTrace(message.OperationId);
    var cancellation = cancelable ? new CancellationTokenSource() : null;
    var terminal = routes.SendToNode(
            SubmitAdmissionNames.Mesh,
            RoutingId.From(targetRid),
            message)
        .Metadata("scenario", message.OperationId)
        .Async(cancellation?.Token ?? CancellationToken.None)
        .AsTask();
    if (!terminal.IsCompleted)
    {
        // A SEND_READY callback may complete a transient EAGAIN inline with
        // the endpoint continuation. Yield once so only a still-pending
        // operation consumes the bounded setup slot.
        await Task.Yield();
        if (!terminal.IsCompleted)
        {
            evidence.Pending(message.OperationId, family, targetRid);
            if (cancellation is not null)
                cancellations.Register(message.OperationId, cancellation);
            _ = RecordTerminalAsync(
                terminal,
                message.OperationId,
                family,
                targetRid,
                evidence,
                cancellation is null ? null : cancellations);
            return new FillCandidate(message.OperationId, true, null);
        }
    }

    await terminal.ConfigureAwait(false);
    cancellation?.Dispose();
    evidence.Terminal(message.OperationId, family, targetRid, "Submitted");
    return new FillCandidate(message.OperationId, false, "Submitted");
}

internal sealed record FillCandidate(
    string OperationId,
    bool Pending,
    string? TerminalStatus);
