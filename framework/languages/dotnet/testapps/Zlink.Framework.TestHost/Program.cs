using System.Text;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Protocol;
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
    string? EventFilePath,
    string? RegistryPubEndpoint,
    string? RegistryRouterEndpoint,
    uint? RegistryId,
    string? DiscoveryEndpoint,
    string? DiscoveryChannelName,
    string? ChannelName,
    string? ServerEndpoint,
    string? PublisherEndpoint,
    string? SpotNodeName,
    string? SpotBindEndpoint,
    bool EnablePubSub,
    string? SpotFactoryName,
    string? CreateSpotName,
    string? PublishTopic,
    string? PublishValue,
    string? AttachSpotPublisherChannel,
    string? StreamEndpoint)
{
    public static TestHostOptions Parse(string[] args)
    {
        string? mode = null;
        string? readyFilePath = null;
        string? stopFilePath = null;
        string? eventFilePath = null;
        string? registryPubEndpoint = null;
        string? registryRouterEndpoint = null;
        uint? registryId = null;
        string? discoveryEndpoint = null;
        string? discoveryChannelName = null;
        string? channelName = null;
        string? serverEndpoint = null;
        string? publisherEndpoint = null;
        string? spotNodeName = null;
        string? spotBindEndpoint = null;
        var enablePubSub = false;
        string? spotFactoryName = null;
        string? createSpotName = null;
        string? publishTopic = null;
        string? publishValue = null;
        string? attachSpotPublisherChannel = null;
        string? streamEndpoint = null;

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
                case "--event-file":
                    eventFilePath = ReadValue();
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
                case "--publisher-endpoint":
                    publisherEndpoint = ReadValue();
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
                case "--publish-topic":
                    publishTopic = ReadValue();
                    break;
                case "--publish-value":
                    publishValue = ReadValue();
                    break;
                case "--attach-spot-publisher-channel":
                    attachSpotPublisherChannel = ReadValue();
                    break;
                case "--stream-endpoint":
                    streamEndpoint = ReadValue();
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
            eventFilePath,
            registryPubEndpoint,
            registryRouterEndpoint,
            registryId,
            discoveryEndpoint,
            discoveryChannelName,
            channelName,
            serverEndpoint,
            publisherEndpoint,
            spotNodeName,
            spotBindEndpoint,
            enablePubSub,
            spotFactoryName,
            createSpotName,
            publishTopic,
            publishValue,
            attachSpotPublisherChannel,
            streamEndpoint);
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
            case "channel-subscriber":
                ConfigureChannelSubscriber(services, options);
                return;
            case "channel-publisher":
                ConfigureChannelPublisher(services, options);
                return;
            case "spot-node":
                ConfigureSpotNode(services, options);
                return;
            case "stream-raw":
                ConfigureStreamRawNode(services, options);
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

            framework.AddClientServerChannel(
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

    private static void ConfigureChannelSubscriber(IServiceCollection services, TestHostOptions options)
    {
        services.AddSingleton(new TestHostEventSink(options.EventFilePath));
        services.AddZLinkFramework(framework =>
        {
            framework.UseDiscovery(discovery =>
            {
                discovery.Add(options.DiscoveryEndpoint
                    ?? throw new InvalidOperationException("Channel subscriber mode requires --discovery-endpoint."));
            });

            framework.AddFanoutChannel(
                options.ChannelName
                    ?? throw new InvalidOperationException("Channel subscriber mode requires --channel-name."),
                channel =>
                {
                    channel.EnableSubscriber();
                });
        });
        services.AddZLinkHandlersFromAssemblyContaining<Program>();
    }

    private static void ConfigureChannelPublisher(IServiceCollection services, TestHostOptions options)
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

            framework.AddFanoutChannel(
                options.ChannelName
                    ?? throw new InvalidOperationException("Channel publisher mode requires --channel-name."),
                channel =>
                {
                    channel.EnablePublisher(publisher =>
                    {
                        publisher.Bind(options.PublisherEndpoint
                            ?? throw new InvalidOperationException("Channel publisher mode requires --publisher-endpoint."));
                    });
                });
        });

        if (!string.IsNullOrWhiteSpace(options.PublishTopic))
        {
            services.AddHostedService(provider =>
                new ChannelStartupPublishHostedService(
                    provider.GetRequiredService<IZLinkFanoutPublisher>(),
                    options.ChannelName!,
                    options.PublishTopic!,
                    options.PublishValue ?? "startup"));
        }
    }

    private static void ConfigureSpotNode(IServiceCollection services, TestHostOptions options)
    {
        services.AddSingleton(new TestHostEventSink(options.EventFilePath));
        services.AddScoped<StartupStageSubscriptionHandler>();
        services.AddZLinkFramework(framework =>
        {
            framework.AddSpotMesh(
                options.DiscoveryChannelName
                    ?? throw new InvalidOperationException("SPOT node mode requires --discovery-channel."),
                spotMesh =>
                {
                    spotMesh.UseDiscovery(discovery =>
                    {
                        discovery.Add(options.DiscoveryEndpoint
                            ?? throw new InvalidOperationException("SPOT node mode requires --discovery-endpoint."));
                    });

                    spotMesh.AddNode(
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

                            if (!string.IsNullOrWhiteSpace(options.AttachSpotPublisherChannel))
                            {
                                spot.AttachSpotMeshPublisherClient(options.AttachSpotPublisherChannel);
                            }

                            if (!string.IsNullOrWhiteSpace(options.SpotFactoryName))
                            {
                                spot.AddSpotFactory<StartupStageSpot>(options.SpotFactoryName);
                            }
                        });
                });
        });

        if (!string.IsNullOrWhiteSpace(options.CreateSpotName))
        {
            services.AddHostedService(provider =>
                new StartupSpotCreationHostedService(
                    provider.GetRequiredService<IZLinkSpotManager>(),
                    options.CreateSpotName!));
        }

        if (!string.IsNullOrWhiteSpace(options.AttachSpotPublisherChannel)
            && !string.IsNullOrWhiteSpace(options.PublishTopic))
        {
            services.AddHostedService(provider =>
                new SpotStartupPublishHostedService(
                    provider.GetRequiredService<IZLinkSpotMeshPublisherClient>(),
                    options.AttachSpotPublisherChannel!,
                    options.PublishTopic!,
                    options.PublishValue ?? "startup"));
        }
    }

    private static void ConfigureStreamRawNode(IServiceCollection services, TestHostOptions options)
    {
        services.AddSingleton(new TestHostEventSink(options.EventFilePath));
        services.AddSingleton<TestHostRawStreamRecorder>();
        services.AddZLinkFramework(framework =>
        {
            framework.AddStreamNode("stream.raw", stream =>
            {
                stream.Bind(options.StreamEndpoint
                    ?? throw new InvalidOperationException("STREAM raw mode requires --stream-endpoint."));
                stream.AddHeaderSession<TestHostRawStreamSession>();
            });
        });
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

internal sealed class ChannelStartupPublishHostedService(
    IZLinkFanoutPublisher publisher,
    string channelName,
    string topic,
    string value) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        while (!stoppingToken.IsCancellationRequested)
        {
            await publisher.Publish(channelName, topic, new TestHostPublishedEvent(value))
                .Async(stoppingToken);

            try
            {
                await Task.Delay(TimeSpan.FromMilliseconds(200), stoppingToken);
            }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
            {
                return;
            }
        }
    }
}

internal sealed class SpotStartupPublishHostedService(
    IZLinkSpotMeshPublisherClient publisher,
    string channelName,
    string topic,
    string value) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        while (!stoppingToken.IsCancellationRequested)
        {
            await publisher.Publish(channelName, topic, new StartupStageEvent(value))
                .Async(stoppingToken);

            try
            {
                await Task.Delay(TimeSpan.FromMilliseconds(200), stoppingToken);
            }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
            {
                return;
            }
        }
    }
}

internal sealed class TestHostEventSink(string? path)
{
    private readonly object _gate = new();

    public void Append(string value)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            return;
        }

        var directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrWhiteSpace(directory))
        {
            Directory.CreateDirectory(directory);
        }

        lock (_gate)
        {
            File.AppendAllText(path, value + Environment.NewLine);
        }
    }
}

internal sealed class StartupStageSpot(IZLinkSpotContext context) : IZLinkSpot
{
    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.AddSubscribe<StartupStageSubscriptionHandler>("stage.monitor");
    }
}

internal sealed class StartupStageSubscriptionHandler(TestHostEventSink sink) : IZLinkSpotSubscriptionHandler<StartupStageSpot, StartupStageEvent>
{
    public ValueTask HandleAsync(
        StartupStageSpot spot,
        StartupStageEvent message,
        CancellationToken cancellationToken)
    {
        _ = spot;
        _ = cancellationToken;
        sink.Append(message.Value);
        return ValueTask.CompletedTask;
    }
}

internal sealed record StartupStageEvent(string Value);

internal sealed record TestHostPublishedEvent(string Value);

internal sealed class ChannelSubscriptionEventHandler(TestHostEventSink sink)
{
    [ZLinkEvent]
    public ValueTask HandleAsync(
        TestHostPublishedEvent @event,
        ZLinkEventContext context,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        sink.Append($"{context.Topic}:{@event.Value}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class TestHostRawStreamRecorder(TestHostEventSink sink)
{
    public void RecordConnected(IZLinkSessionContext context)
    {
        sink.Append(
            $"connected|{context.SessionId}|{context.RoutingId?.ToHex() ?? "<null>"}|{context.LocalAddr ?? "<null>"}|{context.RemoteAddr ?? "<null>"}");
    }

    public void RecordPayload(string payload)
    {
        sink.Append($"raw|{payload}");
    }

    public void RecordError(ZLinkStreamError error)
    {
        sink.Append($"error|{error.Error}");
    }

    public void RecordDisconnected(IZLinkSessionContext context)
    {
        sink.Append($"disconnected|{context.SessionId}");
    }
}

internal sealed class TestHostRawStreamSession(TestHostRawStreamRecorder recorder) : IZLinkSession
{
    public IZLinkSessionContext Context { get; set; } = default!;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        recorder.RecordConnected(Context);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        recorder.RecordDisconnected(Context);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        recorder.RecordError(error);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        global::Zlink.Message payload,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        _ = header;
        recorder.RecordPayload(Encoding.UTF8.GetString(payload.AsReadOnlySpan()));
        return Context.Reply("pong")
            .Async(cancellationToken);
    }
}
