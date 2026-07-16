using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using LocationMessaging.Shared;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Locations;
using LocationMessaging.Server.Consumer.Configuration;
using LocationMessaging.Server.Consumer;

namespace LocationMessaging.Server.Consumer.Endpoints;

internal static class ConsumerEndpoints
{
    public static void MapConsumerEndpoints(this WebApplication app)
    {
        app.MapGet("/health", () => Results.Ok(new { status = "ready" }));
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
                rows = (await query.ListPeerLocationsAsync(
                        new ZLinkPeerLocationFilter(MeshName: request.MeshName),
                        cancellationToken))
                    .Select(peer => new PeerLocationRow(
                        peer.MeshName,
                        peer.NodeRid?.ToString(),
                        peer.Role.ToString(),
                        peer.Endpoint,
                        peer.Weight,
                        peer.OwnerId,
                        peer.Generation))
                    .ToArray();
                var matches = rows.Where(row =>
                    row.Role == request.Role
                    && row.NodeRid == request.NodeRid
                    && (request.Weight is null || row.Weight == request.Weight));
                if (request.Present ? matches.Any() : !matches.Any()) return Results.Ok(rows);

                await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken);
            } while (DateTimeOffset.UtcNow < deadline);

            return Results.Problem(
                $"Peer row did not reach the requested state for rid={request.NodeRid}.",
                statusCode: StatusCodes.Status504GatewayTimeout);
        });
        app.MapPost("/profile/batch-request", async (
            ProfileReq[] requests,
            IZLinkChannelClient channel) =>
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
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestProfileAsync(channel, request, TimeSpan.FromSeconds(5));
            return Results.Ok(reply);
        });
        app.MapPost("/profile/request/outcome", async (
            ProfileReq request,
            IZLinkChannelClient channel) =>
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
            IZLinkChannelClient channel) =>
        {
            var result = await RequestProfileFailureAsync(channel, request, TimeSpan.FromMilliseconds(100));
            return Results.Ok(result);
        });
        app.MapPost("/profile/missing-request", async (
            ProfileReq request,
            IZLinkChannelClient channel) =>
        {
            var result = await RequestMissingProfileAsync(
                channel,
                new MissingProfileReq(request.Value));
            return Results.Ok(result);
        });
        app.MapPost("/profile/missing-command", (
            ProfileMsg command,
            IZLinkChannelClient channel) =>
        {
            channel.SendToChannel(
                "profile",
                new MissingProfileMsg(command.CommandId)).Submit();
            return Results.Ok(new { status = "sent" });
        });
        app.MapPost("/profile/payload", async (
            PayloadReq request,
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestPayloadAsync(channel, request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/payload-over-limit", async (
            PayloadReq request,
            IZLinkChannelClient channel) =>
        {
            var result = await RequestPayloadFailureAsync(channel, request);
            return Results.Ok(result);
        });
        app.MapPost("/profile/backpressure/reset", () => Results.Ok(new { status = "ready" }));
        app.MapPost("/profile/backpressure/send", (
            ProfileMsg command,
            IZLinkChannelClient channel) =>
        {
            var outcome = SubmitProfileUnderPressure(channel, command);
            return Results.Ok(outcome);
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
    }

    static Task<ProfileRes> RequestProfileAsync(
        IZLinkChannelClient channel,
        ProfileReq request,
        TimeSpan timeout)
        => channel.RequestToChannel("profile", request)
            .Timeout(timeout)
            .Async<ProfileRes>()
            .AsTask();

    static Task<PayloadRes> RequestPayloadAsync(
        IZLinkChannelClient channel,
        PayloadReq request)
        => channel.RequestToChannel("profile", request)
            .Timeout(TimeSpan.FromSeconds(10))
            .Async<PayloadRes>()
            .AsTask();

    static async Task<ExpectedFailureRes> RequestPayloadFailureAsync(
        IZLinkChannelClient channel,
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
        IZLinkChannelClient channel,
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
        IZLinkChannelClient channel,
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
            error.Kind == ZLinkFrameworkErrorKind.HandlerNotFound)
        {
            return new ExpectedFailureRes(error.Kind.ToString());
        }
    }

    static string SubmitProfileUnderPressure(
        IZLinkChannelClient channel,
        ProfileMsg command)
    {
        channel.SendToChannel("profile", command).Submit();
        return "Submitted";
    }

}
