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
            ProfileRequest[] requests,
            IZLinkChannelClient channel) =>
        {
            var replies = new List<ProfileReply>(requests.Length);
            foreach (var request in requests)
            {
                replies.Add(await RequestProfileWithRetryAsync(channel, request, TimeSpan.FromSeconds(5)));
            }

            return Results.Ok(replies.ToArray());
        });
        app.MapPost("/profile/request", async (
            ProfileRequest request,
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestProfileWithRetryAsync(channel, request, TimeSpan.FromSeconds(5));
            return Results.Ok(reply);
        });
        app.MapPost("/profile/slow-request", async (
            ProfileRequest request,
            IZLinkChannelClient channel) =>
        {
            var result = await RequestProfileFailureAsync(channel, request, TimeSpan.FromMilliseconds(100));
            return Results.Ok(result);
        });
        app.MapPost("/profile/missing-request", async (
            ProfileRequest request,
            IZLinkChannelClient channel) =>
        {
            var result = await RequestMissingProfileAsync(channel, request);
            return Results.Ok(result);
        });
        app.MapPost("/profile/missing-command", async (
            ProfileCommand command,
            IZLinkChannelClient channel) =>
        {
            await channel.SendToChannel("profile", command)
                .PacketName("MissingProfileCommand")
                .Async();
            return Results.Ok(new { status = "sent" });
        });
        app.MapPost("/profile/payload", async (
            PayloadRequest request,
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestPayloadWithRetryAsync(channel, request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/backpressure/reset", () => Results.Ok(new { status = "ready" }));
        app.MapPost("/profile/backpressure/send", async (
            ProfileCommand command,
            IZLinkChannelClient channel) =>
        {
            var outcome = await SendProfileWithBoundedFailureAsync(channel, command);
            return Results.Ok(outcome);
        });
    }

    static async Task<ProfileReply> RequestProfileWithRetryAsync(
        IZLinkChannelClient channel,
        ProfileRequest request,
        TimeSpan timeout)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await channel.RequestToChannel("profile", request)
                    .PacketName("ProfileRequest")
                    .Timeout(timeout)
                    .Async<ProfileReply>();
            }
            catch (ZLinkFrameworkException ex) when (IsRetriableStartupFailure(ex))
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }
        }

        throw new InvalidOperationException("Timed out waiting for direct profile endpoints.", last);
    }

    static async Task<PayloadReply> RequestPayloadWithRetryAsync(
        IZLinkChannelClient channel,
        PayloadRequest request)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await channel.RequestToChannel("profile", request)
                    .PacketName("PayloadRequest")
                    .Timeout(TimeSpan.FromSeconds(10))
                    .Async<PayloadReply>();
            }
            catch (ZLinkFrameworkException ex) when (IsRetriableStartupFailure(ex))
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }
        }

        throw new InvalidOperationException("Timed out waiting for payload profile endpoint.", last);
    }

    static async Task<RequestFailureResult> RequestProfileFailureAsync(
        IZLinkChannelClient channel,
        ProfileRequest request,
        TimeSpan timeout)
    {
        while (true)
        {
            try
            {
                await RequestProfileWithRetryAsync(channel, request, timeout);
                return new RequestFailureResult(false, "");
            }
            catch (TimeoutException ex)
            {
                return new RequestFailureResult(true, ex.GetType().Name);
            }
        }
    }

    static async Task<RequestFailureResult> RequestMissingProfileAsync(
        IZLinkChannelClient channel,
        ProfileRequest request)
    {
        try
        {
            await channel.RequestToChannel("profile", request)
                .PacketName("MissingProfileRequest")
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<ProfileReply>();
            return new RequestFailureResult(false, "");
        }
        catch (Exception ex)
        {
            return new RequestFailureResult(true, ex.GetType().Name);
        }
    }

    static async Task<string> SendProfileWithBoundedFailureAsync(
        IZLinkChannelClient channel,
        ProfileCommand command)
    {
        try
        {
            var send = Task.Run(async () =>
            {
                using var timeout = new CancellationTokenSource(TimeSpan.FromMilliseconds(500));
                await channel.SendToChannel("profile", command)
                    .PacketName("ProfileCommand")
                    .Async(timeout.Token);
            });
            var completed = await Task.WhenAny(send, Task.Delay(TimeSpan.FromMilliseconds(750)));
            if (!ReferenceEquals(completed, send))
            {
                return "BoundedFailure";
            }

            await send;
            return "Accepted";
        }
        catch (TimeoutException)
        {
            return "BoundedFailure";
        }
        catch (OperationCanceledException)
        {
            return "BoundedFailure";
        }
        catch (ZLinkFrameworkException error) when (error.IsRetriable)
        {
            return "BoundedFailure";
        }
        catch (Exception error) when (error.Message.Contains("backpressure", StringComparison.OrdinalIgnoreCase)
            || error.Message.Contains("socket became writable", StringComparison.OrdinalIgnoreCase))
        {
            return "BoundedFailure";
        }
    }

    static bool IsRetriableStartupFailure(ZLinkFrameworkException ex) =>
        ex.IsRetriable
        || ex.Kind is ZLinkFrameworkErrorKind.RouteNotConnected
            or ZLinkFrameworkErrorKind.RequestTargetNotFound
            or ZLinkFrameworkErrorKind.RequestProtocolError;
}
