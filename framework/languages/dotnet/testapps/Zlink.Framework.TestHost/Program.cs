using System.Text;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;

var options = TestHostOptions.Parse(args);
var readyFilePath = options.ReadyFilePath;

Console.SetOut(TextWriter.Synchronized(new StreamWriter(
    Console.OpenStandardOutput(),
    new UTF8Encoding(encoderShouldEmitUTF8Identifier: false))
{
    AutoFlush = true,
}));

var builder = Host.CreateApplicationBuilder(args);
builder.Logging.ClearProviders();

TestHostScenarioConfigurator.Configure(builder.Services, options);
builder.Services.AddHostedService(provider =>
    new ReadySignalHostedService(
        provider.GetRequiredService<IHostApplicationLifetime>(),
        readyFilePath,
        options.StopFilePath,
        options.Mode));

using var host = builder.Build();
await host.RunAsync();

internal sealed record TestHostOptions(
    string Mode,
    string? ReadyFilePath,
    string? StopFilePath,
    string? RegistryPubEndpoint,
    string? RegistryRouterEndpoint,
    uint? RegistryId,
    string? DiscoveryEndpoint,
    string? DiscoveryChannelName,
    string? ChannelName,
    string? ServerEndpoint,
    string? SpotNodeName,
    string? SpotBindEndpoint,
    bool EnablePubSub,
    string? SpotFactoryName,
    string? CreateSpotName)
{
    public static TestHostOptions Parse(string[] args)
    {
        string? mode = null;
        string? readyFilePath = null;
        string? stopFilePath = null;
        string? registryPubEndpoint = null;
        string? registryRouterEndpoint = null;
        uint? registryId = null;
        string? discoveryEndpoint = null;
        string? discoveryChannelName = null;
        string? channelName = null;
        string? serverEndpoint = null;
        string? spotNodeName = null;
        string? spotBindEndpoint = null;
        var enablePubSub = false;
        string? spotFactoryName = null;
        string? createSpotName = null;

        for (var index = 0; index < args.Length; index++)
        {
            var argument = args[index];
            if (!argument.StartsWith("--", StringComparison.Ordinal))
            {
                mode ??= argument;
                continue;
            }

            string? ReadValue()
            {
                if (index + 1 >= args.Length)
                {
                    throw new InvalidOperationException($"Missing value for '{argument}'.");
                }

                index++;
                return args[index];
            }

            switch (argument)
            {
                case "--ready-file":
                    readyFilePath = ReadValue();
                    break;
                case "--stop-file":
                    stopFilePath = ReadValue();
                    break;
                case "--registry-pub-endpoint":
                    registryPubEndpoint = ReadValue();
                    break;
                case "--registry-router-endpoint":
                    registryRouterEndpoint = ReadValue();
                    break;
                case "--registry-id":
                    registryId = uint.Parse(ReadValue()!, System.Globalization.CultureInfo.InvariantCulture);
                    break;
                case "--discovery-endpoint":
                    discoveryEndpoint = ReadValue();
                    break;
                case "--discovery-channel":
                    discoveryChannelName = ReadValue();
                    break;
                case "--channel-name":
                    channelName = ReadValue();
                    break;
                case "--server-endpoint":
                    serverEndpoint = ReadValue();
                    break;
                case "--spot-node-name":
                    spotNodeName = ReadValue();
                    break;
                case "--spot-bind-endpoint":
                    spotBindEndpoint = ReadValue();
                    break;
                case "--enable-pubsub":
                    enablePubSub = true;
                    break;
                case "--spot-factory":
                    spotFactoryName = ReadValue();
                    break;
                case "--create-spot":
                    createSpotName = ReadValue();
                    break;
                default:
                    break;
            }
        }

        readyFilePath ??= Environment.GetEnvironmentVariable("ZLINK_TEST_READY_FILE");

        return new TestHostOptions(
            mode ?? "idle",
            readyFilePath,
            stopFilePath,
            registryPubEndpoint,
            registryRouterEndpoint,
            registryId,
            discoveryEndpoint,
            discoveryChannelName,
            channelName,
            serverEndpoint,
            spotNodeName,
            spotBindEndpoint,
            enablePubSub,
            spotFactoryName,
            createSpotName);
    }
}

internal static class TestHostScenarioConfigurator
{
    public static void Configure(IServiceCollection services, TestHostOptions options)
    {
        switch (options.Mode)
        {
            case "idle":
                return;
            case "registry":
                ConfigureRegistry(services, options);
                return;
            case "channel-server":
                ConfigureChannelServer(services, options);
                return;
            case "spot-node":
                ConfigureSpotNode(services, options);
                return;
            default:
                throw new InvalidOperationException($"Unsupported test host mode '{options.Mode}'.");
        }
    }

    private static void ConfigureRegistry(IServiceCollection services, TestHostOptions options)
    {
        services.AddZLinkRegistry(registry =>
        {
            registry.PubEndpoint = options.RegistryPubEndpoint
                ?? throw new InvalidOperationException("Registry mode requires --registry-pub-endpoint.");
            registry.RouterEndpoint = options.RegistryRouterEndpoint
                ?? throw new InvalidOperationException("Registry mode requires --registry-router-endpoint.");

            if (options.RegistryId is uint registryId)
            {
                registry.RegistryId = registryId;
            }
        });
    }

    private static void ConfigureChannelServer(IServiceCollection services, TestHostOptions options)
    {
        services.AddZLinkFramework(framework =>
        {
            if (!string.IsNullOrWhiteSpace(options.DiscoveryEndpoint))
            {
                framework.UseDiscovery(discovery =>
                {
                    discovery.Add(options.DiscoveryEndpoint);
                });
            }

            framework.AddChannel(
                options.ChannelName
                    ?? throw new InvalidOperationException("Channel server mode requires --channel-name."),
                channel =>
                {
                    channel.EnableServer(server =>
                    {
                        server.Bind(options.ServerEndpoint
                            ?? throw new InvalidOperationException("Channel server mode requires --server-endpoint."));
                    });
                });
        });
    }

    private static void ConfigureSpotNode(IServiceCollection services, TestHostOptions options)
    {
        services.AddScoped<StartupStageSubscriptionHandler>();
        services.AddZLinkFramework(framework =>
        {
            framework.UseSpotDiscovery(
                options.DiscoveryChannelName
                    ?? throw new InvalidOperationException("SPOT node mode requires --discovery-channel."),
                discovery =>
                {
                    discovery.Add(options.DiscoveryEndpoint
                        ?? throw new InvalidOperationException("SPOT node mode requires --discovery-endpoint."));
                });

            framework.AddSpotNode(
                options.SpotNodeName
                    ?? throw new InvalidOperationException("SPOT node mode requires --spot-node-name."),
                spot =>
                {
                    spot.Bind(options.SpotBindEndpoint
                        ?? throw new InvalidOperationException("SPOT node mode requires --spot-bind-endpoint."));

                    if (options.EnablePubSub)
                    {
                        spot.EnablePubSub();
                    }

                    if (!string.IsNullOrWhiteSpace(options.SpotFactoryName))
                    {
                        spot.AddSpotFactory<StartupStageSpot>(options.SpotFactoryName);
                    }
                });
        });

        if (!string.IsNullOrWhiteSpace(options.CreateSpotName))
        {
            services.AddHostedService(provider =>
                new StartupSpotCreationHostedService(
                    provider.GetRequiredService<IZLinkSpotManager>(),
                    options.CreateSpotName!));
        }
    }
}

internal sealed class ReadySignalHostedService(
    IHostApplicationLifetime applicationLifetime,
    string? readyFilePath,
    string? stopFilePath,
    string mode) : IHostedService
{
    public Task StartAsync(CancellationToken cancellationToken)
    {
        applicationLifetime.ApplicationStarted.Register(WriteReadyMarker);

        if (!string.IsNullOrWhiteSpace(stopFilePath))
        {
            _ = WatchStopFileAsync(applicationLifetime, stopFilePath, cancellationToken);
        }
        else
        {
            _ = ListenForStopSignalAsync(applicationLifetime, cancellationToken);
        }

        return Task.CompletedTask;
    }

    public Task StopAsync(CancellationToken cancellationToken)
    {
        return Task.CompletedTask;
    }

    private void WriteReadyMarker()
    {
        var payload = JsonSerializer.Serialize(new
        {
            app = "Zlink.Framework.TestHost",
            mode,
            pid = Environment.ProcessId,
        });

        Console.WriteLine($"READY:{payload}");

        if (!string.IsNullOrWhiteSpace(readyFilePath))
        {
            File.WriteAllText(readyFilePath, payload);
        }
    }

    private static async Task ListenForStopSignalAsync(
        IHostApplicationLifetime lifetime,
        CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            var line = await Console.In.ReadLineAsync(cancellationToken);

            if (line is null || string.Equals(line, "STOP", StringComparison.Ordinal))
            {
                lifetime.StopApplication();
                return;
            }
        }
    }

    private static async Task WatchStopFileAsync(
        IHostApplicationLifetime lifetime,
        string stopFilePath,
        CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            if (File.Exists(stopFilePath))
            {
                lifetime.StopApplication();
                return;
            }

            await Task.Delay(100, cancellationToken);
        }
    }
}

internal sealed class StartupSpotCreationHostedService(
    IZLinkSpotManager spotManager,
    string spotName) : IHostedService
{
    public async Task StartAsync(CancellationToken cancellationToken)
    {
        await spotManager.CreateAsync(spotName, cancellationToken);
    }

    public Task StopAsync(CancellationToken cancellationToken)
    {
        return Task.CompletedTask;
    }
}

internal sealed class StartupStageSpot : ZLinkSpot
{
    public StartupStageSpot(
        global::Zlink.RoutingId spotRid,
        global::Zlink.RoutingId nodeRid)
        : base(spotRid, nodeRid)
    {
        AddSubscribe<StartupStageSubscriptionHandler>("stage.monitor");
    }
}

internal sealed class StartupStageSubscriptionHandler : IZLinkSpotSubscriptionHandler<StartupStageSpot, StartupStageEvent>
{
    public ValueTask HandleAsync(
        StartupStageSpot spot,
        StartupStageEvent message,
        CancellationToken cancellationToken)
    {
        _ = spot;
        _ = message;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }
}

internal sealed record StartupStageEvent(string Value);
