using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using RegistryMessaging.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;

namespace RegistryMessaging.Consumer;

internal static class ConsumerHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ConsumerOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });

        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.TraceLabel}-flow.log"))
                .TraceLabel(options.TraceLabel);

            var profile = framework.AddClientServerChannel("profile");
            if (!string.IsNullOrWhiteSpace(options.RegistryRouterEndpoint))
            {
                framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint);
                profile.EnableClient();
            }
            else
            {
                foreach (var endpoint in options.ProviderEndpoints)
                {
                    profile.EnableClient(endpoint);
                }
            }

            if (options.LowHighWaterMark)
            {
                profile.ConfigureClientSocket().SendHighWaterMark = 1;
                profile.ConfigureClientSocket().ReceiveHighWaterMark = 1;
                profile.ConfigureClientSocket().SendTimeout = TimeSpan.FromMilliseconds(100);
            }
        });

        var app = builder.Build();
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

        return app;
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

internal sealed record ConsumerOptions(
    string HttpUrl,
    string LogDir,
    string TraceLabel,
    IReadOnlyList<string> ProviderEndpoints,
    string? RegistryRouterEndpoint,
    bool LowHighWaterMark)
{
    public static ConsumerOptions Parse(string[] args)
    {
        var values = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for {key}.");
            }

            key = key[2..];
            if (!values.TryGetValue(key, out var existing))
            {
                existing = [];
                values.Add(key, existing);
            }

            existing.Add(args[++i]);
        }

        var endpoints = values.TryGetValue("provider-endpoint", out var providerEndpoints)
            ? providerEndpoints.Where(endpoint => !string.IsNullOrWhiteSpace(endpoint)).ToArray()
            : [];
        var registryRouterEndpoint = GetSingleOrNull(values, "registry-router-endpoint");
        if (endpoints.Length == 0 && string.IsNullOrWhiteSpace(registryRouterEndpoint))
        {
            throw new ArgumentException("--provider-endpoint or --registry-router-endpoint is required.");
        }

        return new ConsumerOptions(
            GetSingle(values, "http-url", "http://127.0.0.1:0"),
            GetSingle(values, "log-dir", Path.Combine(Path.GetTempPath(), "zlink-dotnet-e2e-log")),
            GetSingle(values, "trace-label", "consumer"),
            endpoints,
            registryRouterEndpoint,
            bool.TryParse(GetSingle(values, "low-hwm", "false"), out var lowHwm) && lowHwm);
    }

    static string GetSingle(
        IReadOnlyDictionary<string, List<string>> values,
        string key,
        string defaultValue)
    {
        return values.TryGetValue(key, out var matched) && matched.Count > 0
            ? matched[^1]
            : defaultValue;
    }

    static string? GetSingleOrNull(
        IReadOnlyDictionary<string, List<string>> values,
        string key)
    {
        return values.TryGetValue(key, out var matched) && matched.Count > 0
            ? matched[^1]
            : null;
    }
}
