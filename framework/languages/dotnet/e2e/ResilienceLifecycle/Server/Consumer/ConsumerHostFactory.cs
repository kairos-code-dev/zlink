using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using ResilienceLifecycle.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;

namespace ResilienceLifecycle.Consumer;

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
            framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint);
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, "consumer-flow.log"))
                .TraceLabel("consumer");
            framework.AddClientServerChannel(ResilienceLifecycleNames.Channel).EnableClient();
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready" }));
        app.MapPost("/profile/request", async (
            ProfileRequest request,
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestProfileWithRetryAsync(channel, request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/request/timeout/{milliseconds:int}", async (
            int milliseconds,
            ProfileRequest request,
            IZLinkChannelClient channel) =>
        {
            try
            {
                var reply = await channel.RequestToChannel(ResilienceLifecycleNames.Channel, request)
                    .PacketName("ProfileRequest")
                    .Timeout(TimeSpan.FromMilliseconds(milliseconds))
                    .Async<ProfileReply>();
                return Results.Ok(reply);
            }
            catch (TimeoutException)
            {
                return Results.StatusCode(StatusCodes.Status408RequestTimeout);
            }
        });
        app.MapPost("/profile/request/missing", async (
            ProfileRequest request,
            IZLinkChannelClient channel) =>
        {
            try
            {
                var reply = await channel.RequestToChannel(ResilienceLifecycleNames.Channel, request)
                    .PacketName("MissingProfileRequest")
                    .Timeout(TimeSpan.FromSeconds(3))
                    .Async<ProfileReply>();
                return Results.Ok(reply);
            }
            catch (Exception ex)
            {
                return Results.Problem(ex.Message);
            }
        });
        app.MapPost("/profile/command", async (
            ProfileCommand command,
            IZLinkChannelClient channel) =>
        {
            await channel.SendToChannel(ResilienceLifecycleNames.Channel, command)
                .PacketName("ProfileCommand")
                .Async();
            return Results.Ok(new { status = "sent" });
        });
        app.MapPost("/profile/request/new-client", async (ProfileRequest request) =>
        {
            using var host = CreateClientHost(options, $"storm-{request.Marker}");
            await host.StartAsync();
            try
            {
                var channel = host.Services.GetRequiredService<IZLinkChannelClient>();
                var reply = await RequestProfileWithRetryAsync(channel, request);
                return Results.Ok(reply);
            }
            finally
            {
                await StopClientHostAsync(host);
            }
        });
        return app;
    }

    static IHost CreateClientHost(ConsumerOptions options, string traceLabel)
    {
        return Host.CreateDefaultBuilder()
            .ConfigureServices(services =>
            {
                services.AddZLinkFramework(framework =>
                {
                    framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint);
                    framework.ConfigureDispatch()
                        .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                        .TraceLogFile(Path.Combine(options.LogDir, $"{traceLabel}-flow.log"))
                        .TraceLabel(traceLabel);
                    framework.AddClientServerChannel(ResilienceLifecycleNames.Channel).EnableClient();
                });
            })
            .Build();
    }

    static async Task StopClientHostAsync(IHost host)
    {
        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        try
        {
            await host.StopAsync(cts.Token);
        }
        catch (OperationCanceledException)
        {
            // A reconnect storm scenario must not hang the HTTP response while host shutdown waits.
        }
    }

    static async Task<ProfileReply> RequestProfileWithRetryAsync(
        IZLinkChannelClient channel,
        ProfileRequest request)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await channel.RequestToChannel(ResilienceLifecycleNames.Channel, request)
                    .PacketName("ProfileRequest")
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<ProfileReply>();
            }
            catch (ZLinkFrameworkException ex) when (IsRetriableStartupFailure(ex))
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }
        }

        throw new InvalidOperationException("Timed out waiting for resilience profile providers.", last);
    }

    static bool IsRetriableStartupFailure(ZLinkFrameworkException ex) =>
        ex.IsRetriable
        || ex.Kind is ZLinkFrameworkErrorKind.RouteNotConnected
            or ZLinkFrameworkErrorKind.RequestTargetNotFound
            or ZLinkFrameworkErrorKind.RequestProtocolError;
}

internal sealed record ConsumerOptions(
    string HttpUrl,
    string RegistryRouterEndpoint,
    string LogDir)
{
    public static ConsumerOptions Parse(string[] args)
    {
        var values = ParseArgs(args);
        return new ConsumerOptions(
            Require(values, "http-url"),
            Require(values, "registry-router-endpoint"),
            Require(values, "log-dir"));
    }

    static IReadOnlyDictionary<string, string> ParseArgs(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
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

            values[key[2..]] = args[++i];
        }

        return values;
    }

    static string Require(IReadOnlyDictionary<string, string> values, string key) =>
        values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"--{key} is required.");
}
