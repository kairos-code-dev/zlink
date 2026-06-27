using DiscoveryRegistryHa.Shared;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;

namespace DiscoveryRegistryHa.Server.Consumer;

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
            foreach (var endpoint in options.DiscoveryEndpoints)
            {
                framework.UseDiscovery().AddRegistryEndpoint(endpoint);
            }

            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);
            framework.AddClientServerChannel(DiscoveryRegistryHaNames.Channel)
                .EnableClient();
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Rid }));
        app.MapPost("/profile/request", async (ProfileRequest request, IZLinkChannelClient client) =>
        {
            var reply = await client.RequestToChannel(DiscoveryRegistryHaNames.Channel, request)
                .PacketName("ProfileRequest")
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<ProfileReply>();
            return Results.Ok(reply);
        });
        app.MapPost("/profile/request/wait", async (
            ProfileRequest request,
            IZLinkChannelClient client,
            CancellationToken cancellationToken) =>
        {
            var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
            Exception? last = null;
            while (DateTimeOffset.UtcNow < deadline)
            {
                try
                {
                    var reply = await client.RequestToChannel(DiscoveryRegistryHaNames.Channel, request)
                        .PacketName("ProfileRequest")
                        .Timeout(TimeSpan.FromSeconds(3))
                        .Async<ProfileReply>();
                    return Results.Ok(reply);
                }
                catch (Exception ex)
                {
                    last = ex;
                    await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken);
                }
            }

            throw new TimeoutException("Timed out waiting for profile request routing.", last);
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        return app;
    }
}
