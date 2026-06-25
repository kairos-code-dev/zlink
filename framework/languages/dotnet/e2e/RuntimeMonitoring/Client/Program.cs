using System.Net.Http.Json;
using System.Net.Sockets;
using System.Diagnostics;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using RuntimeMonitoring.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Contracts.Handlers;

var options = ClientOptions.Parse(args);
Directory.CreateDirectory(options.LogDir);
using var http = new HttpClient();

using (var host = CreateDiscoveryClientHost(options))
{
    await host.StartAsync();
    var client = host.Services.GetRequiredService<IZLinkChannelClient>();
    var reply = await client.RequestToChannel(
            RuntimeMonitoringNames.Channel,
            new ProfileRequest("monitor", "mon-a1-request"))
        .PacketName("ProfileRequest")
        .Timeout(TimeSpan.FromSeconds(3))
        .Async<ProfileReply>();
    Ensure(reply.Value == "profile:monitor", "MON trigger request failed.");
    await host.StopAsync();
}

await WaitUntilAsync(async () =>
{
    var evidence = await ReadEvidenceAsync(http, options.ServiceUrl);
    return evidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
            && line.Contains($"source={RuntimeMonitoringNames.ChannelServerSource}", StringComparison.Ordinal)
            && (line.Contains("kind=Connected", StringComparison.Ordinal)
                || line.Contains("kind=ConnectionReady", StringComparison.Ordinal)))
        && evidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
            && (line.Contains("kind=Disconnected", StringComparison.Ordinal)
                || line.Contains("kind=Closed", StringComparison.Ordinal)));
}, "MON-A1 socket connect/disconnect events missing.");
Console.WriteLine("scenario MON-A1 passed");

await WaitUntilAsync(async () =>
{
    var evidence = await ReadEvidenceAsync(http, options.RegistryUrl);
    return evidence.Any(line => line.Contains("monitor-registry|source=registry|kind=TopologyChanged", StringComparison.Ordinal)
            && !line.Contains("topology=0", StringComparison.Ordinal))
        && evidence.Any(line => line.Contains("monitor-registry|source=registry|kind=ServiceSummaryChanged", StringComparison.Ordinal)
            && !line.Contains("summary=0", StringComparison.Ordinal));
}, "MON-A2 registry topology/summary events missing.");
Console.WriteLine("scenario MON-A2 passed");

await WaitUntilAsync(async () =>
{
    var evidence = await ReadEvidenceAsync(http, options.ServiceUrl);
    return evidence.Any(line => line.Contains($"monitor-spot|source={RuntimeMonitoringNames.SpotNode}|kind=StatusChanged", StringComparison.Ordinal))
        && evidence.Any(line => line.Contains($"monitor-spot|source={RuntimeMonitoringNames.SpotNode}|kind=PeersChanged", StringComparison.Ordinal))
        && evidence.Any(line => line.Contains($"monitor-spot|source={RuntimeMonitoringNames.SpotNode}|kind=SubjectsChanged", StringComparison.Ordinal))
        && evidence.Any(line => line.Contains($"monitor-spot|source={RuntimeMonitoringNames.SpotNode}|kind=TimerHandlerFailed", StringComparison.Ordinal)
            && line.Contains("timer=failing", StringComparison.Ordinal));
}, "MON-A3 spot status/peer/subject/timer events missing.");
Console.WriteLine("scenario MON-A3 passed");

using (var drainClient = CreateMonitoredDirectClientHost(options, options.ServiceChannelEndpoint, "drain-client"))
{
    await drainClient.StartAsync();
    var drainEvents = drainClient.Services.GetRequiredService<LocalSocketEventStore>();
    var drainChannel = drainClient.Services.GetRequiredService<IZLinkChannelClient>();
    var drainReply = await drainChannel.RequestToChannel(
            RuntimeMonitoringNames.Channel,
            new ProfileRequest("drain", "mon-a4-before-drain"))
        .PacketName("ProfileRequest")
        .Timeout(TimeSpan.FromSeconds(3))
        .Async<ProfileReply>();
    Ensure(drainReply.ProviderRid == "svc-a", "MON-A4 direct request did not hit drained service.");
    await PostAsync(http, $"{options.ServiceUrl}/admin/drain");
    await WaitUntilAsync(async () =>
    {
        _ = await Task.FromResult(0);
        var evidence = await ReadEvidenceAsync(http, options.ServiceUrl);
        return evidence.Any(line => line.Contains("admin|", StringComparison.Ordinal)
                && line.Contains("action=drain", StringComparison.Ordinal))
            && drainEvents.Snapshot().Any(line => line.Contains("kind=PeerAdmissionChanged", StringComparison.Ordinal));
    }, "MON-A4 drain transition was not observed.");
    await PostAsync(http, $"{options.ServiceUrl}/admin/restore");
    await drainClient.StopAsync();
}
await WaitUntilAsync(async () =>
{
    var evidence = await ReadEvidenceAsync(http, options.RegistryUrl);
    return evidence.Count(line => line.Contains("monitor-registry|source=registry|kind=TopologyChanged", StringComparison.Ordinal)) >= 2;
}, "MON-A4 registry topology transition evidence missing.");
Console.WriteLine("scenario MON-A4 passed");

await TriggerHandshakeFailureAsync(options.ServiceChannelEndpoint);
await WaitUntilAsync(async () =>
{
    var serviceEvidence = await ReadEvidenceAsync(http, options.ServiceUrl);
    var registryEvidence = await ReadEvidenceAsync(http, options.RegistryUrl);
    return serviceEvidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
            && (line.Contains("kind=HandshakeFailed", StringComparison.Ordinal)
                || line.Contains("kind=Internal", StringComparison.Ordinal)))
        && registryEvidence.Any(line => line.Contains("monitor-registry|source=registry|kind=StatusChanged", StringComparison.Ordinal))
        && serviceEvidence.Any(line => line.Contains($"monitor-spot|source={RuntimeMonitoringNames.SpotNode}|kind=StatusChanged", StringComparison.Ordinal))
        && serviceEvidence.Any(line => line.Contains($"monitor-spot|source={RuntimeMonitoringNames.SpotNode}|kind=TimerStoppedAfterUnhandledException", StringComparison.Ordinal)
            && line.Contains("timer=stopping", StringComparison.Ordinal));
}, "MON-A5 remaining fixed monitoring kinds missing.");
Console.WriteLine("scenario MON-A5 passed");

using (var filteredClient = CreateDirectClientHost(options, options.ServiceBChannelEndpoint, "filter-client"))
{
    await filteredClient.StartAsync();
    var filteredChannel = filteredClient.Services.GetRequiredService<IZLinkChannelClient>();
    var reply = await filteredChannel.RequestToChannel(
            RuntimeMonitoringNames.Channel,
            new ProfileRequest("filter", "mon-b1-request"))
        .PacketName("ProfileRequest")
        .Timeout(TimeSpan.FromSeconds(3))
        .Async<ProfileReply>();
    Ensure(reply.ProviderRid == "svc-b", "MON-B1 direct request did not hit filtered service.");
    await filteredClient.StopAsync();
}

await WaitUntilAsync(async () =>
{
    var evidence = await ReadEvidenceAsync(http, options.ServiceBUrl);
    return evidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
            && line.Contains("kind=ConnectionReady", StringComparison.Ordinal))
        && evidence.Where(line => line.Contains("monitor-socket|", StringComparison.Ordinal))
            .All(line => line.Contains("kind=ConnectionReady", StringComparison.Ordinal));
}, "MON-B1 socket event kind filter evidence missing.");
Console.WriteLine("scenario MON-B1 passed");

await VerifyMonitoringRegistrationValidationAsync();
Console.WriteLine("scenario MON-B2 passed");

using (var throwingClient = CreateDirectClientHost(options, options.ThrowChannelEndpoint, "throw-client"))
{
    await throwingClient.StartAsync();
    var throwingChannel = throwingClient.Services.GetRequiredService<IZLinkChannelClient>();
    var reply = await throwingChannel.RequestToChannel(
            RuntimeMonitoringNames.Channel,
            new ProfileRequest("throw", "mon-c1-request"))
        .PacketName("ProfileRequest")
        .Timeout(TimeSpan.FromSeconds(3))
        .Async<ProfileReply>();
    Ensure(reply.ProviderRid == "svc-throw", "MON-C1 direct request did not hit throwing-monitor service.");
    await throwingClient.StopAsync();
}

await WaitUntilAsync(async () =>
{
    var evidence = await ReadEvidenceAsync(http, options.ThrowServiceUrl);
    var stderr = await ReadFileIfExistsAsync(Path.Combine(options.LogDir, "svc-throw.stderr.log"));
    return evidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal))
        && evidence.Any(line => line.Contains("monitor-throw|", StringComparison.Ordinal))
        && stderr.Contains("monitoring-event-dispatch", StringComparison.Ordinal)
        && stderr.Contains("monitoring dispatch failure for e2e", StringComparison.Ordinal);
}, "MON-C1 monitoring dispatch failure evidence missing.");
using (var recoveryClient = CreateDirectClientHost(options, options.ThrowChannelEndpoint, "throw-recovery-client"))
{
    await recoveryClient.StartAsync();
    var recoveryChannel = recoveryClient.Services.GetRequiredService<IZLinkChannelClient>();
    var reply = await recoveryChannel.RequestToChannel(
            RuntimeMonitoringNames.Channel,
            new ProfileRequest("throw", "mon-c1-recovery"))
        .PacketName("ProfileRequest")
        .Timeout(TimeSpan.FromSeconds(3))
        .Async<ProfileReply>();
    Ensure(reply.Value == "profile:throw", "MON-C1 messaging did not recover after monitoring handler failure.");
    await recoveryClient.StopAsync();
}
Console.WriteLine("scenario MON-C1 passed");

await PostAsync(http, $"{options.ServiceBUrl}/shutdown");
await WaitUntilAsync(async () => !await IsHealthyAsync(http, options.ServiceBUrl),
    "MON-D1 expected service-b to stop.");
using (var restartedService = StartServiceB(options))
{
    await WaitUntilAsync(async () => await IsHealthyAsync(http, options.ServiceBUrl),
        "MON-D1 expected service-b to restart.");
    using (var restartedClient = CreateDirectClientHost(options, options.ServiceBChannelEndpoint, "restart-client"))
    {
        await restartedClient.StartAsync();
        var restartedChannel = restartedClient.Services.GetRequiredService<IZLinkChannelClient>();
        var reply = await restartedChannel.RequestToChannel(
                RuntimeMonitoringNames.Channel,
                new ProfileRequest("restart", "mon-d1-request"))
            .PacketName("ProfileRequest")
            .Timeout(TimeSpan.FromSeconds(3))
            .Async<ProfileReply>();
        Ensure(reply.ProviderRid == "svc-b", "MON-D1 restarted service did not handle request.");
        await restartedClient.StopAsync();
    }

    await WaitUntilAsync(async () =>
    {
        var evidence = await ReadEvidenceAsync(http, options.RegistryUrl);
        return evidence.Count(line => line.Contains("monitor-registry|source=registry|kind=TopologyChanged", StringComparison.Ordinal)) >= 3;
    }, "MON-D1 repeated failure/recovery topology evidence missing.");
    Console.WriteLine("scenario MON-D1 passed");

    await PostAsync(http, $"{options.ServiceBUrl}/shutdown");
    await restartedService.WaitForExitAsync();
}

Console.WriteLine("runtime-monitoring e2e result=passed");

static IHost CreateDiscoveryClientHost(ClientOptions options)
{
    return CreateClientHostCore(options, directEndpoint: null, traceLabel: "client");
}

static IHost CreateDirectClientHost(ClientOptions options, string directEndpoint, string traceLabel)
{
    return CreateClientHostCore(options, directEndpoint, traceLabel, monitorClientSocket: false);
}

static IHost CreateMonitoredDirectClientHost(ClientOptions options, string directEndpoint, string traceLabel)
{
    return CreateClientHostCore(options, directEndpoint, traceLabel, monitorClientSocket: true);
}

static IHost CreateClientHostCore(
    ClientOptions options,
    string? directEndpoint,
    string traceLabel,
    bool monitorClientSocket = false)
{
    return Host.CreateDefaultBuilder()
        .ConfigureServices(services =>
        {
            if (monitorClientSocket)
            {
                services.AddSingleton<LocalSocketEventStore>();
                services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSocketEvent>, LocalSocketEventRecorder>();
            }

            services.AddZLinkFramework(framework =>
            {
                framework.ConfigureDispatch()
                    .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                    .TraceLogFile(Path.Combine(options.LogDir, $"{traceLabel}-flow.log"))
                    .TraceLabel(traceLabel);
                var channel = framework.AddClientServerChannel(RuntimeMonitoringNames.Channel);
                if (directEndpoint is null)
                {
                    framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint);
                    channel.EnableClient();
                }
                else
                {
                    channel.EnableClient(directEndpoint);
                }
            });

            if (monitorClientSocket)
            {
                services.AddZLinkMonitoring(monitor =>
                {
                    monitor.AddSocketEvents(RuntimeMonitoringNames.ChannelClientSource);
                });
            }
        })
        .Build();
}

static async Task VerifyMonitoringRegistrationValidationAsync()
{
    var duplicate = AssertThrows<ZLinkConfigurationException>(() =>
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddSocketEvents("duplicate.server");
            monitor.AddSocketEvents("duplicate.server");
        });
    });
    Ensure(duplicate.Message.Contains("Duplicate monitoring socket source", StringComparison.Ordinal),
        "MON-B2 duplicate source error was not explicit.");

    var interval = AssertThrows<ZLinkConfigurationException>(() =>
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddRegistryEvents("registry", TimeSpan.Zero);
        });
    });
    Ensure(interval.Message.Contains("interval must be greater than zero", StringComparison.Ordinal),
        "MON-B2 non-positive interval error was not explicit.");

    var spotBuilder = Host.CreateApplicationBuilder();
    spotBuilder.Services.AddZLinkFramework(_ => { });
    spotBuilder.Services.AddZLinkMonitoring(monitor =>
    {
        monitor.AddSpotEvents("missing.spot", TimeSpan.FromMilliseconds(100));
    });
    using (var host = spotBuilder.Build())
    {
        var exception = await AssertThrowsAsync<ZLinkConfigurationException>(() => host.StartAsync());
        Ensure(exception.Message.Contains("not registered", StringComparison.Ordinal),
            "MON-B2 missing spot source startup error was not explicit.");
    }

    var socketBuilder = Host.CreateApplicationBuilder();
    socketBuilder.Services.AddZLinkFramework(framework =>
    {
        framework.AddClientServerChannel("validation.profile")
            .EnableServer(PickTcpEndpoint())
            .AddRequestHandler<ValidationRequestHandler, ProfileRequest, ProfileReply>("ProfileRequest");
    });
    socketBuilder.Services.AddZLinkMonitoring(monitor =>
    {
        monitor.AddSocketEvents("missing.server", ZLinkSocketEventKind.ConnectionReady);
    });
    using (var host = socketBuilder.Build())
    {
        var exception = await AssertThrowsAsync<ZLinkConfigurationException>(() => host.StartAsync());
        Ensure(exception.Message.Contains("not registered", StringComparison.Ordinal),
            "MON-B2 missing socket source startup error was not explicit.");
    }
}

static async Task TriggerHandshakeFailureAsync(string endpoint)
{
    var (host, port) = ParseTcpEndpoint(endpoint);
    using var client = new TcpClient();
    await client.ConnectAsync(host, port);
    await client.GetStream().WriteAsync("not-a-zlink-handshake"u8.ToArray());
}

static Process StartServiceB(ClientOptions options)
{
    var stdout = Path.Combine(options.LogDir, "svc-b-restart.stdout.log");
    var stderr = Path.Combine(options.LogDir, "svc-b-restart.stderr.log");
    var startInfo = new ProcessStartInfo("dotnet")
    {
        RedirectStandardOutput = true,
        RedirectStandardError = true,
        UseShellExecute = false,
    };
    startInfo.Environment["ZLINK_E2E_RID"] = "svc-b";
    startInfo.ArgumentList.Add("run");
    startInfo.ArgumentList.Add("--project");
    startInfo.ArgumentList.Add(options.ServerProject);
    startInfo.ArgumentList.Add("--");
    startInfo.ArgumentList.Add("--role");
    startInfo.ArgumentList.Add("service");
    startInfo.ArgumentList.Add("--rid");
    startInfo.ArgumentList.Add("svc-b");
    startInfo.ArgumentList.Add("--http-url");
    startInfo.ArgumentList.Add(options.ServiceBUrl);
    startInfo.ArgumentList.Add("--registry-router-endpoint");
    startInfo.ArgumentList.Add(options.RegistryRouterEndpoint);
    startInfo.ArgumentList.Add("--channel-endpoint");
    startInfo.ArgumentList.Add(options.ServiceBChannelEndpoint);
    startInfo.ArgumentList.Add("--monitor-mode");
    startInfo.ArgumentList.Add("socket-filter");
    startInfo.ArgumentList.Add("--evidence-file");
    startInfo.ArgumentList.Add(Path.Combine(options.LogDir, "svc-b-restart.evidence.log"));
    startInfo.ArgumentList.Add("--log-dir");
    startInfo.ArgumentList.Add(options.LogDir);

    var process = Process.Start(startInfo)
        ?? throw new InvalidOperationException("Failed to restart service-b.");
    _ = Task.Run(async () => await File.WriteAllTextAsync(stdout, await process.StandardOutput.ReadToEndAsync()));
    _ = Task.Run(async () => await File.WriteAllTextAsync(stderr, await process.StandardError.ReadToEndAsync()));
    return process;
}

static async Task<string[]> ReadEvidenceAsync(HttpClient http, string baseUrl)
{
    return await http.GetFromJsonAsync<string[]>($"{baseUrl}/evidence") ?? [];
}

static async Task PostAsync(HttpClient http, string url)
{
    using var response = await http.PostAsync(url, content: null);
    response.EnsureSuccessStatusCode();
}

static async Task<bool> IsHealthyAsync(HttpClient http, string baseUrl)
{
    try
    {
        using var response = await http.GetAsync($"{baseUrl}/health");
        return response.IsSuccessStatusCode;
    }
    catch (HttpRequestException)
    {
        return false;
    }
}

static async Task<string> ReadFileIfExistsAsync(string path)
{
    return File.Exists(path) ? await File.ReadAllTextAsync(path) : string.Empty;
}

static async Task WaitUntilAsync(Func<Task<bool>> condition, string failureMessage)
{
    var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
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
        catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or TimeoutException)
        {
            last = ex;
        }

        await Task.Delay(100);
    }

    throw new InvalidOperationException(last is null ? failureMessage : $"{failureMessage} Last error: {last.Message}", last);
}

static void Ensure(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}

static TException AssertThrows<TException>(Action action)
    where TException : Exception
{
    try
    {
        action();
    }
    catch (TException ex)
    {
        return ex;
    }

    throw new InvalidOperationException($"Expected {typeof(TException).Name}.");
}

static async Task<TException> AssertThrowsAsync<TException>(Func<Task> action)
    where TException : Exception
{
    try
    {
        await action();
    }
    catch (TException ex)
    {
        return ex;
    }

    throw new InvalidOperationException($"Expected {typeof(TException).Name}.");
}

static string PickTcpEndpoint()
{
    var listener = new TcpListener(System.Net.IPAddress.Loopback, 0);
    listener.Start();
    var port = ((System.Net.IPEndPoint)listener.LocalEndpoint).Port;
    listener.Stop();
    return $"tcp://127.0.0.1:{port}";
}

static (string Host, int Port) ParseTcpEndpoint(string endpoint)
{
    var uri = new Uri(endpoint);
    return (uri.Host, uri.Port);
}

internal sealed class ValidationRequestHandler : IZLinkRequestHandler<ProfileRequest, ProfileReply>
{
    public ValueTask<ProfileReply> HandleAsync(
        ProfileRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(new ProfileReply(request.Value, "validation", request.Marker));
    }
}

internal sealed class LocalSocketEventStore
{
    private readonly List<string> _entries = [];
    private readonly object _gate = new();

    public void Add(string entry)
    {
        lock (_gate)
        {
            _entries.Add(entry);
        }
    }

    public string[] Snapshot()
    {
        lock (_gate)
        {
            return [.. _entries];
        }
    }
}

internal sealed class LocalSocketEventRecorder(LocalSocketEventStore store)
    : IZLinkRuntimeEventHandler<ZLinkSocketEvent>
{
    public ValueTask HandleAsync(ZLinkSocketEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        store.Add($"local-socket|source={@event.SourceName}|kind={@event.Event}|remote={@event.RemoteAddr}");
        return ValueTask.CompletedTask;
    }
}

internal sealed record ClientOptions(
    string RegistryRouterEndpoint,
    string RegistryUrl,
    string ServiceUrl,
    string ServiceChannelEndpoint,
    string ServiceBUrl,
    string ServiceBChannelEndpoint,
    string ThrowServiceUrl,
    string ThrowChannelEndpoint,
    string ServerProject,
    string LogDir)
{
    public static ClientOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                throw new ArgumentException($"Unexpected argument '{key}'.");
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for '{key}'.");
            }

            values[key] = args[++i];
        }

        string Get(string name) => values.TryGetValue(name, out var value)
            ? value
            : throw new ArgumentException($"{name} is required.");

        return new ClientOptions(
            RegistryRouterEndpoint: Get("--registry-router-endpoint"),
            RegistryUrl: Get("--registry-url"),
            ServiceUrl: Get("--service-url"),
            ServiceChannelEndpoint: Get("--service-channel-endpoint"),
            ServiceBUrl: Get("--service-b-url"),
            ServiceBChannelEndpoint: Get("--service-b-channel-endpoint"),
            ThrowServiceUrl: Get("--throw-service-url"),
            ThrowChannelEndpoint: Get("--throw-channel-endpoint"),
            ServerProject: Get("--server-project"),
            LogDir: Get("--log-dir"));
    }
}
