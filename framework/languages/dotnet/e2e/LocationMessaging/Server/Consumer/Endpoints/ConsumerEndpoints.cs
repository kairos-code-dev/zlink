using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using LocationMessaging.Shared;
using Zlink.Framework.Contracts.Channels;
using LocationMessaging.Server.Consumer.Configuration;
using LocationMessaging.Server.Consumer;

namespace LocationMessaging.Server.Consumer.Endpoints;

internal static class ConsumerEndpoints
{
    public static void MapConsumerEndpoints(this WebApplication app)
    {
        app.MapGet("/health", () => Results.Ok(new { status = "ready" }));
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

    static async Task<RequestFailureRes> RequestPayloadFailureAsync(
        IZLinkChannelClient channel,
        PayloadReq request)
    {
        try
        {
            await channel.RequestToChannel("profile", request)
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<PayloadRes>();
            return new RequestFailureRes(false, "");
        }
        catch (Exception ex)
        {
            return new RequestFailureRes(true, ex.GetType().Name);
        }
    }

    static async Task<RequestFailureRes> RequestProfileFailureAsync(
        IZLinkChannelClient channel,
        ProfileReq request,
        TimeSpan timeout)
    {
        try
        {
            await RequestProfileAsync(channel, request, timeout);
            return new RequestFailureRes(false, "");
        }
        catch (TimeoutException ex)
        {
            return new RequestFailureRes(true, ex.GetType().Name);
        }
    }

    static async Task<RequestFailureRes> RequestMissingProfileAsync(
        IZLinkChannelClient channel,
        MissingProfileReq request)
    {
        try
        {
            await channel.RequestToChannel("profile", request)
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<ProfileRes>();
            return new RequestFailureRes(false, "");
        }
        catch (Exception ex)
        {
            return new RequestFailureRes(true, ex.GetType().Name);
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
