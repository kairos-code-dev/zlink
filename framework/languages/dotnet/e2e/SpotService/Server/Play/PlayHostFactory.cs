using System.Collections.Concurrent;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using SpotService.Shared;
using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;

namespace SpotService.Server.Play;

internal static partial class PlayHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ServerOptions.Parse(args, defaultRole: "play");
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));
        builder.Services.AddSingleton(new NodeOptions(options.Rid));

        builder.Services.AddZLinkFramework(framework =>
            {
                framework.AddHandlersFromAssemblyOf(typeof(Program));
                framework.ConfigureDispatch()
                    .SetMessageFlowObserver<EvidenceDispatchErrorObserver>()
                    .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                    .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                    .TraceLabel(options.Rid);
                framework.UseDiscovery().AddRegistryEndpoint(Require(options.RegistryRouterEndpoint, "--registry-router-endpoint"));
                framework.AddRouteMesh(SpotServiceNames.ControlChannel)
                    .EnableServer(Require(options.ControlEndpoint, "--control-endpoint"))
                    .EnableClient()
                    .SetRoutingId(RoutingId.From(options.Rid))
                    .AddHandlerGroup("play");
                var externalSpotChannel = string.Equals(options.Rid, "play-b", StringComparison.Ordinal)
                    ? SpotServiceNames.ExternalSpotChannelB
                    : SpotServiceNames.ExternalSpotChannel;
                if (!string.IsNullOrWhiteSpace(options.ExternalSpotEndpoint))
                {
                    framework.AddRouteMesh(externalSpotChannel)
                        .EnableServer(options.ExternalSpotEndpoint)
                        .EnableClient()
                        .SetRoutingId(RoutingId.From(options.Rid));
                }
                if (!string.IsNullOrWhiteSpace(options.ExternalClientEndpoint))
                {
                    framework.AddClientServerChannel(SpotServiceNames.ExternalClientChannel)
                        .EnableServer(options.ExternalClientEndpoint)
                        .EnableClient(options.ExternalClientEndpoint)
                        .AddHandlerGroup("client");
                }

                var spot = framework.AddSpotMesh(SpotServiceNames.SpotChannel)
                    .UseRegistrySpotResolver()
                    .EnableRouter(Require(options.SpotRouterEndpoint, "--spot-router-endpoint"))
                    .SetRoutingId(RoutingId.From(options.Rid))
                    .EnablePubSub(Require(options.SpotPubEndpoint, "--spot-pub-endpoint"))
                    .AddEntrySpot<ScenarioEntrySpot>()
                    .AddActorFactory<ScenarioActorFactory>(SpotServiceNames.ActorType)
                    .AddSpotFactory<ScenarioUserSpot>()
                    .AddSpotFactory<ScenarioAlternateSpot>();
                if (!string.IsNullOrWhiteSpace(options.ClientSpotPubEndpoint))
                {
                    spot.ConnectPeerPub(options.ClientSpotPubEndpoint);
                }
        });

        var app = builder.Build();
        MapOperationalEndpoints(app, options);
        MapSpotLifecycleEndpoints(app);
        MapSpotFailureEndpoints(app);
        MapSpotInteractionEndpoints(app);
        return app;
    }

static string Require(string? value, string optionName)
    => string.IsNullOrWhiteSpace(value)
        ? throw new InvalidOperationException($"{optionName} is required.")
        : value;

static async Task<bool> FailsAsync(Task task)
{
    try
    {
        await task;
        return false;
    }
    catch
    {
        return true;
    }
}

static async Task<StateReply> RequestSpotStateWithRetryAsync(
    IZLinkRouteClient routes,
    string spotRid,
    StateReq request,
    string failureMessage,
    string channelName = SpotServiceNames.ExternalSpotChannel)
{
    var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(5);
    Exception? last = null;
    while (DateTimeOffset.UtcNow < deadline)
    {
        try
        {
            return await routes.Request(
                    channelName,
                    RoutingId.From(spotRid),
                    request)
                .PacketName("StateReq")
                .Timeout(TimeSpan.FromSeconds(1))
                .Async<StateReply>();
        }
        catch (Exception ex) when (ex is TimeoutException or ZLinkFrameworkException)
        {
            last = ex;
            await Task.Delay(100);
        }
    }

    throw new InvalidOperationException(last is null ? failureMessage : $"{failureMessage} Last error: {last.Message}", last);
}

static async Task SendSpotCommandWithRetryAsync(
    IZLinkRouteClient routes,
    string channelName,
    string spotRid,
    object command,
    string packetName,
    string failureMessage)
{
    var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(5);
    Exception? last = null;
    while (DateTimeOffset.UtcNow < deadline)
    {
        try
        {
            await routes.Send(channelName, RoutingId.From(spotRid), command)
                .PacketName(packetName)
                .Async();
            return;
        }
        catch (Exception ex) when (ex is TimeoutException or ZLinkFrameworkException)
        {
            last = ex;
            await Task.Delay(100);
        }
    }

    throw new InvalidOperationException(last is null ? failureMessage : $"{failureMessage} Last error: {last.Message}", last);
}

static async Task<SpotToSpotReply> RequestSpotToSpotWithRetryAsync(
    IZLinkRouteClient routes,
    string sourceSpotRid,
    SpotToSpotReq request,
    string failureMessage)
{
    var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
    Exception? last = null;
    while (DateTimeOffset.UtcNow < deadline)
    {
        try
        {
            return await routes.Request(
                    SpotServiceNames.ExternalSpotChannel,
                    RoutingId.From(sourceSpotRid),
                    request)
                .PacketName("SpotToSpotReq")
                .Timeout(TimeSpan.FromSeconds(2))
                .Async<SpotToSpotReply>();
        }
        catch (Exception ex) when (
            ex is TimeoutException or OperationCanceledException or ZlinkRequestException or ZlinkSubmitException
            || ex is ZLinkFrameworkException { InnerException: ZlinkRequestException or ZlinkSubmitException })
        {
            last = ex;
        }

        await Task.Delay(TimeSpan.FromMilliseconds(100));
    }

    throw new TimeoutException(failureMessage, last);
}

static async Task ExpectFailureAsync(Task task, string message)
{
    try
    {
        await task;
    }
    catch
    {
        return;
    }

    throw new InvalidOperationException(message);
}

static async Task WaitUntilAsync(Func<bool> condition, string failureMessage)
{
    var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
    while (DateTimeOffset.UtcNow < deadline)
    {
        if (condition())
        {
            return;
        }

        await Task.Delay(100);
    }

    throw new InvalidOperationException(failureMessage);
}

static async Task WaitUntilAsync(Func<Task<bool>> condition, string failureMessage)
{
    var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
    Exception? last = null;
    while (DateTimeOffset.UtcNow < deadline)
    {
        try
        {
            if (await condition())
            {
                return;
            }
        }
        catch (Exception ex)
        {
            last = ex;
        }

        await Task.Delay(100);
    }

    throw new InvalidOperationException(failureMessage, last);
}

static int FindIndex(string[] lines, string pattern)
{
    return Array.FindIndex(lines, line => line.Contains(pattern, StringComparison.Ordinal));
}

static ulong ExtractUInt64(string line, string key)
{
    var prefix = key + "=";
    var start = line.IndexOf(prefix, StringComparison.Ordinal);
    if (start < 0)
    {
        throw new InvalidOperationException($"Missing field '{key}' in evidence line: {line}");
    }

    start += prefix.Length;
    var end = line.IndexOf('|', start);
    var value = end < 0 ? line[start..] : line[start..end];
    return ulong.Parse(value);
}

static int CountNew(string[] after, string[] before, string pattern)
{
    var beforeCount = before.Count(line => line.Contains(pattern, StringComparison.Ordinal));
    var afterCount = after.Count(line => line.Contains(pattern, StringComparison.Ordinal));
    return afterCount - beforeCount;
}

static void Ensure(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}

}
