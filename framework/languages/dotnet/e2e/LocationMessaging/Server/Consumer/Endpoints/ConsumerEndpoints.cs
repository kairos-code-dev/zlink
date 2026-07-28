using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using LocationMessaging.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Locations;
using LocationMessaging.Server.Consumer.Configuration;
using LocationMessaging.Server.Consumer;

namespace LocationMessaging.Server.Consumer.Endpoints;

internal static class ConsumerEndpoints
{
    public static void MapConsumerEndpoints(
        this WebApplication app,
        ConsumerOptions options)
    {
        app.MapGet("/health", () => Results.Ok(new { status = "ready" }));
        app.MapGet("/topology/ready", (
            int count,
            IZLinkRouteMeshRuntime runtime) =>
        {
            var snapshot = runtime.GetStatus(options.MeshName);
            var readyCount = snapshot.Peers.Count(
                static peer => peer.State == ZLinkPeerState.Ready);
            return Results.Ok(new
            {
                ready = readyCount >= count
                        && snapshot.Channels.Any(static channel =>
                            channel.ChannelName == "profile"
                            && channel.IsReady),
                readyCount,
                rid = string.Empty,
                peers = snapshot.Peers.Select(static peer => new
                {
                    rid = peer.NodeRid.ToString(),
                    Endpoint = string.Empty,
                    Ready = peer.State == ZLinkPeerState.Ready,
                    AdmissionState = peer.State.ToString(),
                    LastFailure = peer.UnavailableReason?.ToString()
                })
            });
        });
        app.MapGet("/topology/status", (IZLinkRouteMeshRuntime runtime) =>
        {
            var snapshot = runtime.GetStatus(options.MeshName);
            return Results.Ok(new
            {
                state = snapshot.State.ToString(),
                snapshot.IsReady,
                snapshot.ReadyPeerCount,
                peers = snapshot.Peers.Select(static peer => new
                {
                    rid = peer.NodeRid.ToString(),
                    state = peer.State.ToString()
                })
            });
        });
        app.MapPost("/node-direct/{targetRid}", async (
            string targetRid,
            IZLinkRouteClient routes,
            IZLinkRouteMeshRuntime runtime,
            CancellationToken cancellationToken) =>
        {
            var before = runtime.GetStatus(options.MeshName);
            var send = await CaptureAsync(async () =>
                await routes.SendToNode(
                        options.MeshName,
                        RoutingId.From(targetRid),
                        new ProfileMsg($"rm-a3-{Guid.NewGuid():N}"))
                    .Async(cancellationToken));
            var request = await CaptureAsync(async () =>
                await routes.RequestToNode(
                        options.MeshName,
                        RoutingId.From(targetRid),
                        new ProfileReq("rm-a3"))
                    .Async<ProfileRes>(cancellationToken));
            var after = runtime.GetStatus(options.MeshName);
            return Results.Ok(new
            {
                send,
                request,
                peerCountBefore = before.Peers.Count,
                peerCountAfter = after.Peers.Count,
                readyPeerCountBefore = before.ReadyPeerCount,
                readyPeerCountAfter = after.ReadyPeerCount
            });
        });
        app.MapPost("/connections/wait", async (
            EvidenceWaitReq request,
            ConnectionEvidence evidence,
            CancellationToken cancellationToken) =>
        {
            try
            {
                var snapshot = await evidence.WaitAsync(
                    request.Contains,
                    request.AfterCount,
                    TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000)),
                    cancellationToken);
                return Results.Ok(snapshot);
            }
            catch (TimeoutException error)
            {
                return Results.Problem(
                    error.Message,
                    statusCode: StatusCodes.Status504GatewayTimeout);
            }
        });
        app.MapPost("/locations/peers/wait", async (
            PeerLocationWaitReq request,
            IServiceProvider services,
            CancellationToken cancellationToken) =>
        {
            var query = services.GetRequiredService<IZLinkLocationRuntimeQuery>();
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 120000));
            var deadline = DateTimeOffset.UtcNow + timeout;
            PeerLocationRow[] rows;
            do
            {
                rows = (await query.ListTopologyAsync(
                        new ZLinkLocationTopologyFilter(request.MeshName),
                        cancellationToken: cancellationToken))
                    .Items
                    .Select(peer => new PeerLocationRow(
                        peer.MeshName,
                        peer.NodeRid.ToString(),
                        "Router",
                        peer.Endpoint,
                        peer.Draining,
                        peer.State.ToString()))
                    .ToArray();
                var matches = rows.Where(row =>
                    row.Role == request.Role
                    && row.NodeRid is { } nodeRid
                    && (nodeRid == request.NodeRid
                        || nodeRid.StartsWith($"{request.NodeRid}-", StringComparison.Ordinal)));
                if (request.Present ? matches.Any() : !matches.Any()) return Results.Ok(rows);

                await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken);
            } while (DateTimeOffset.UtcNow < deadline);

            return Results.Problem(
                $"Peer row did not reach the requested state for rid={request.NodeRid}.",
                statusCode: StatusCodes.Status504GatewayTimeout);
        });
        app.MapPost("/profile/batch-request", async (
            ProfileReq[] requests,
            IZLinkRouteClient channel) =>
        {
            var replies = new List<ProfileRes>(requests.Length);
            foreach (var request in requests)
            {
                replies.Add(await RequestProfileAsync(channel, request, TimeSpan.FromSeconds(5)));
            }

            return Results.Ok(replies.ToArray());
        });
        app.MapPost("/profile/request", async (
            ProfileReq request,
            IZLinkRouteClient channel) =>
        {
            var reply = await RequestProfileAsync(channel, request, TimeSpan.FromSeconds(5));
            return Results.Ok(reply);
        });
        app.MapPost("/profile/request/outcome", async (
            ProfileReq request,
            IZLinkRouteClient channel) =>
        {
            try
            {
                var reply = await RequestProfileAsync(channel, request, TimeSpan.FromSeconds(5));
                return Results.Ok(new RequestOutcomeRes(request.Value, reply.ProviderRid));
            }
            catch (ZLinkFrameworkException error)
            {
                return Results.Ok(new RequestOutcomeRes(request.Value, error.Kind.ToString()));
            }
            catch (TimeoutException)
            {
                return Results.Ok(new RequestOutcomeRes(request.Value, "Timeout"));
            }
        });
        app.MapPost("/profile/slow-request", async (
            ProfileReq request,
            IZLinkRouteClient channel) =>
        {
            var result = await RequestProfileFailureAsync(channel, request, TimeSpan.FromMilliseconds(100));
            return Results.Ok(result);
        });
        app.MapPost("/profile/missing-request", async (
            ProfileReq request,
            IZLinkRouteClient channel) =>
        {
            var result = await RequestMissingProfileAsync(
                channel,
                new MissingProfileReq(request.Value));
            return Results.Ok(result);
        });
        app.MapPost("/profile/missing-command", async (
            ProfileMsg command,
            IZLinkRouteClient channel,
            CancellationToken cancellationToken) =>
        {
            await channel.SendToChannel("profile",
                    new MissingProfileMsg(command.CommandId))
                .Async(cancellationToken);
            return Results.Ok(new { status = "sent" });
        });
        app.MapPost("/profile/payload", async (
            PayloadReq request,
            IZLinkRouteClient channel) =>
        {
            var reply = await RequestPayloadAsync(channel, request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/payload-over-limit", async (
            PayloadReq request,
            IZLinkRouteClient channel) =>
        {
            var result = await RequestPayloadFailureAsync(channel, request);
            return Results.Ok(result);
        });
        app.MapPost("/profile/backpressure/reset", () => Results.Ok(new { status = "ready" }));
        app.MapPost("/profile/backpressure/send", async (
            ProfileMsg command,
            IZLinkRouteClient channel,
            CancellationToken cancellationToken) =>
        {
            var outcome = await SubmitProfileUnderPressureAsync(channel, command, cancellationToken);
            return Results.Ok(outcome);
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
    }

    static Task<ProfileRes> RequestProfileAsync(
        IZLinkRouteClient channel,
        ProfileReq request,
        TimeSpan timeout)
        => channel.RequestToChannel("profile", request)
            .Timeout(timeout)
            .Async<ProfileRes>()
            .AsTask();

    static async Task<string> CaptureAsync(Func<Task> operation)
    {
        try
        {
            await operation();
            return "Submitted";
        }
        catch (ZLinkFrameworkException error)
        {
            return error.Kind.ToString();
        }
    }

    static Task<PayloadRes> RequestPayloadAsync(
        IZLinkRouteClient channel,
        PayloadReq request)
        => channel.RequestToChannel("profile", request)
            .Timeout(TimeSpan.FromSeconds(10))
            .Async<PayloadRes>()
            .AsTask();

    static async Task<ExpectedFailureRes> RequestPayloadFailureAsync(
        IZLinkRouteClient channel,
        PayloadReq request)
    {
        try
        {
            await channel.RequestToChannel("profile", request)
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<PayloadRes>();
            throw new InvalidOperationException(
                "An oversized payload completed without the expected public timeout error.");
        }
        catch (TimeoutException error)
        {
            return new ExpectedFailureRes(error.GetType().Name);
        }
    }

    static async Task<ExpectedFailureRes> RequestProfileFailureAsync(
        IZLinkRouteClient channel,
        ProfileReq request,
        TimeSpan timeout)
    {
        try
        {
            await RequestProfileAsync(channel, request, timeout);
            throw new InvalidOperationException(
                "A slow request completed without the expected public timeout error.");
        }
        catch (TimeoutException error)
        {
            return new ExpectedFailureRes(error.GetType().Name);
        }
    }

    static async Task<ExpectedFailureRes> RequestMissingProfileAsync(
        IZLinkRouteClient channel,
        MissingProfileReq request)
    {
        try
        {
            await channel.RequestToChannel("profile", request)
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<ProfileRes>();
            throw new InvalidOperationException(
                "A request without a registered handler completed without the expected public error.");
        }
        catch (ZLinkFrameworkException error) when (
            error.Kind == ZLinkFrameworkErrorKind.NotFound)
        {
            return new ExpectedFailureRes(error.Kind.ToString());
        }
    }

    static async ValueTask<string> SubmitProfileUnderPressureAsync(
        IZLinkRouteClient channel,
        ProfileMsg command,
        CancellationToken cancellationToken)
    {
        try
        {
            await channel.SendToChannel("profile", command)
                .Async(cancellationToken);
            return "Submitted";
        }
        catch (ZLinkFrameworkException error) when (
            error.Kind == ZLinkFrameworkErrorKind.DeadlineExceeded)
        {
            return error.Kind.ToString();
        }
    }

}
