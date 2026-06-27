using DiscoveryRegistryHa.Shared;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Registry;

namespace DiscoveryRegistryHa.Server.Embedded;

internal static class EmbeddedHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ServerOptions.Parse(args, defaultRole: "embedded");
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new EvidenceStore(options.EvidenceFile));

        builder.Services.AddZLinkRegistry(registry =>
        {
            registry.RegistryId = options.RegistryId;
            registry.PubEndpoint = Require(options.RegistryPubEndpoint, "--registry-pub-endpoint");
            registry.RouterEndpoint = Require(options.RegistryRouterEndpoint, "--registry-router-endpoint");
            registry.HeartbeatInterval = TimeSpan.FromMilliseconds(250);
            registry.HeartbeatTimeout = TimeSpan.FromSeconds(2);
            registry.BroadcastInterval = TimeSpan.FromMilliseconds(250);
            foreach (var peer in options.PeerPubEndpoints)
            {
                registry.AddPeer(peer);
            }
        });

        builder.Services.AddZLinkFramework(framework =>
        {
            if (options.DiscoveryEndpoints.Count == 0)
            {
                framework.UseDiscovery().AddRegistryEndpoint(Require(options.RegistryRouterEndpoint, "--registry-router-endpoint"));
            }

            foreach (var endpoint in options.DiscoveryEndpoints)
            {
                framework.UseDiscovery().AddRegistryEndpoint(endpoint);
            }

            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);

            var channel = framework.AddClientServerChannel(DiscoveryRegistryHaNames.Channel)
                .EnableServer(Require(options.ChannelEndpoint, "--channel-endpoint"))
                .SetRoutingId(RoutingId.From(options.Rid));
            channel.AddRequestHandler<ProfileRequestHandler, ProfileRequest, ProfileReply>("ProfileRequest");
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapPost("/evidence/wait", async (
            EvidenceWaitRequest request,
            EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var snapshot = await evidence.WaitUntilAsync(
                line => line.Contains(request.Contains, StringComparison.Ordinal),
                timeout,
                cancellationToken);
            return Results.Ok(snapshot);
        });
        app.MapGet("/registry/status", async ([FromServices] IZLinkRegistryQuery query, CancellationToken cancellationToken) =>
            Results.Ok(await query.StatusAsync(cancellationToken)));
        app.MapGet("/registry/topology", async ([FromServices] IZLinkRegistryQuery query, CancellationToken cancellationToken) =>
            Results.Ok(await query.TopologyAsync(
                new ZLinkRegistryTopologyFilter(ChannelName: DiscoveryRegistryHaNames.Channel),
                cancellationToken)));
        app.MapGet("/registry/members", async ([FromServices] IZLinkRegistryQuery query, CancellationToken cancellationToken) =>
            Results.Ok(await query.MemberPeersAsync(DiscoveryRegistryHaNames.Channel, cancellationToken)));
        app.MapPost("/registry/members/wait", async (
            MemberEndpointWaitRequest request,
            [FromServices] IZLinkRegistryQuery query,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var deadline = DateTimeOffset.UtcNow + timeout;
            while (DateTimeOffset.UtcNow < deadline)
            {
                var members = await query.MemberPeersAsync(DiscoveryRegistryHaNames.Channel, cancellationToken);
                if (members.Any(member =>
                        member.ServiceRole == ZLinkServiceRole.Router
                        && string.Equals(member.Endpoint, request.Endpoint, StringComparison.Ordinal)))
                {
                    return Results.Ok(members);
                }

                await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken);
            }

            throw new TimeoutException("Timed out waiting for discovery registry HA member endpoint.");
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        return app;
    }

    static string Require(string? value, string name)
    {
        return string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"{name} is required.")
            : value;
    }
}
