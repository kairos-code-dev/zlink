using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using RegistryMessaging.Shared;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;
using RegistryMessaging.Server.Consumer.Configuration;
using RegistryMessaging.Server.Consumer;

namespace RegistryMessaging.Server.Consumer.Endpoints;

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
                replies.Add(await RequestProfileWithRetryAsync(channel, request, TimeSpan.FromSeconds(5)));
            }

            return Results.Ok(replies.ToArray());
        });
        app.MapPost("/profile/request", async (
            ProfileReq request,
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestProfileWithRetryAsync(channel, request, TimeSpan.FromSeconds(5));
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
            var result = await RequestMissingProfileAsync(channel, request);
            return Results.Ok(result);
        });
        app.MapPost("/profile/missing-command", async (
            ProfileMsg command,
            IZLinkChannelClient channel) =>
        {
            channel.SendToChannel("profile", command)
                .PacketName("MissingProfileMsg").Submit();
            return Results.Ok(new { status = "sent" });
        });
        app.MapPost("/profile/payload", async (
            PayloadReq request,
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestPayloadWithRetryAsync(channel, request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/backpressure/reset", () => Results.Ok(new { status = "ready" }));
        app.MapPost("/profile/backpressure/send", (
            ProfileMsg command,
            IZLinkChannelClient channel) =>
        {
            var outcome = SubmitProfileUnderPressure(channel, command);
            return Results.Ok(outcome);
        });
    }

    static async Task<ProfileRes> RequestProfileWithRetryAsync(
        IZLinkChannelClient channel,
        ProfileReq request,
        TimeSpan timeout)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await channel.RequestToChannel("profile", request)
                    .PacketName("ProfileReq")
                    .Timeout(timeout)
                    .Async<ProfileRes>();
            }
            catch (ZLinkFrameworkException ex) when (IsRetriableStartupFailure(ex))
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }
        }

        throw new InvalidOperationException("Timed out waiting for direct profile endpoints.", last);
    }

    static async Task<PayloadRes> RequestPayloadWithRetryAsync(
        IZLinkChannelClient channel,
        PayloadReq request)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await channel.RequestToChannel("profile", request)
                    .PacketName("PayloadReq")
                    .Timeout(TimeSpan.FromSeconds(10))
                    .Async<PayloadRes>();
            }
            catch (ZLinkFrameworkException ex) when (IsRetriableStartupFailure(ex))
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }
        }

        throw new InvalidOperationException("Timed out waiting for payload profile endpoint.", last);
    }

    static async Task<RequestFailureRes> RequestProfileFailureAsync(
        IZLinkChannelClient channel,
        ProfileReq request,
        TimeSpan timeout)
    {
        while (true)
        {
            try
            {
                await RequestProfileWithRetryAsync(channel, request, timeout);
                return new RequestFailureRes(false, "");
            }
            catch (TimeoutException ex)
            {
                return new RequestFailureRes(true, ex.GetType().Name);
            }
        }
    }

    static async Task<RequestFailureRes> RequestMissingProfileAsync(
        IZLinkChannelClient channel,
        ProfileReq request)
    {
        try
        {
            await channel.RequestToChannel("profile", request)
                .PacketName("MissingProfileReq")
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
        channel.SendToChannel("profile", command)
            .PacketName("ProfileMsg").Submit();
        return "Submitted";
    }

    static bool IsRetriableStartupFailure(ZLinkFrameworkException ex) =>
        ex.IsRetriable
        || ex.Kind is ZLinkFrameworkErrorKind.RouteNotConnected
            or ZLinkFrameworkErrorKind.RequestTargetNotFound
            or ZLinkFrameworkErrorKind.RequestProtocolError;
}
