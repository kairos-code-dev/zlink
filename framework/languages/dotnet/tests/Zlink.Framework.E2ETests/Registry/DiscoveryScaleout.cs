using System.Collections.Concurrent;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests;

[Collection(nameof(FrameworkTestsCollection))]
public sealed class DiscoveryScaleoutTests
{
    [Fact(DisplayName = "DSC-008 requestToChannel traffic survives discovery scale-out and scale-in")]
    public async Task DSC_008_RequestToChannel_Traffic_Survives_ScaleOut_And_ScaleIn()
    {
        var registryPubEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var registryRouterEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var providerAEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var providerBEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var providerCEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();

        var registryHost = BuildRegistryHost(registryPubEndpoint, registryRouterEndpoint);
        var providerA = BuildProviderHost(registryRouterEndpoint, providerAEndpoint, "provider-a");
        var providerB = BuildProviderHost(registryRouterEndpoint, providerBEndpoint, "provider-b");
        var providerC = BuildProviderHost(registryRouterEndpoint, providerCEndpoint, "provider-c");
        var clientHostA = BuildClientHost(registryRouterEndpoint);
        var clientHostB = BuildClientHost(registryRouterEndpoint);

        using (registryHost)
        using (providerA)
        using (providerB)
        using (providerC)
        using (clientHostA)
        using (clientHostB)
        {
            await registryHost.StartAsync();
            await providerA.StartAsync();
            await clientHostA.StartAsync();
            await clientHostB.StartAsync();

            var clientA = clientHostA.Services.GetRequiredService<IZLinkChannelClient>();
            var clientB = clientHostB.Services.GetRequiredService<IZLinkChannelClient>();
            var registry = registryHost.Services.GetRequiredService<IZLinkRegistryQuery>();
            var recorderA = providerA.Services.GetRequiredService<ScaleoutEvidenceRecorder>();
            var recorderB = providerB.Services.GetRequiredService<ScaleoutEvidenceRecorder>();
            var recorderC = providerC.Services.GetRequiredService<ScaleoutEvidenceRecorder>();

            var first = await RequestProbeAsync(clientA, "warmup-a");
            Assert.Equal("provider-a", first.ProviderId);
            Assert.Contains("warmup-a", recorderA.RequestIds);

            using var traffic = new ScaleoutTrafficRun();
            var trafficTaskA = RunTrafficAsync(clientA, "traffic-a", traffic);
            var trafficTaskB = RunTrafficAsync(clientB, "traffic-b", traffic);

            await Task.Delay(150);
            Assert.All(traffic.ObservedProviders, provider => Assert.Equal("provider-a", provider));

            await providerB.StartAsync();
            await WaitForReadyEndpointsAsync(registry, providerAEndpoint, providerBEndpoint);
            await providerC.StartAsync();
            await WaitForReadyEndpointsAsync(registry, providerAEndpoint, providerBEndpoint, providerCEndpoint);
            await WaitForObservedProvidersAsync(traffic, "provider-b", "provider-c");

            AssertRequestIdsWereHandledOnce(traffic.CompletedRequestIds, recorderA, recorderB, recorderC);

            var providerACountBeforeScaleIn = recorderA.RequestIds.Count;
            await providerA.StopAsync();
            await WaitUntilEndpointIsNotReadyAsync(registry, providerAEndpoint);
            await Task.Delay(150);
            traffic.Stop();
            await Task.WhenAll(trafficTaskA, trafficTaskB);

            var scaleinRequestIds = new List<string>();
            var scaleinProviders = new HashSet<string>(StringComparer.Ordinal);
            for (var i = 0; i < 10; i++)
            {
                var requestId = $"scalein-{i}";
                scaleinRequestIds.Add(requestId);
                var client = i % 2 == 0 ? clientA : clientB;
                var reply = await RequestProbeAsync(client, requestId);
                scaleinProviders.Add(reply.ProviderId);
                Assert.NotEqual("provider-a", reply.ProviderId);
            }

            Assert.Equal(providerACountBeforeScaleIn, recorderA.RequestIds.Count);
            var remainingProviders = new HashSet<string>(StringComparer.Ordinal) { "provider-b", "provider-c" };
            Assert.All(scaleinProviders, provider => Assert.Contains(provider, remainingProviders));
            AssertRequestIdsWereHandledOnce(traffic.CompletedRequestIds, recorderA, recorderB, recorderC);
            AssertRequestIdsWereHandledOnce(scaleinRequestIds, recorderA, recorderB, recorderC);

            await ChannelMessagingTestSupport.StopHostsAsync(clientHostB, clientHostA, providerC, providerB, registryHost);
        }
    }

    [Fact(DisplayName = "DSC-009 same routing id different endpoint replaces discovered provider")]
    public async Task DSC_009_Same_RoutingId_Different_Endpoint_Replaces_Discovered_Provider()
    {
        var registryPubEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var registryRouterEndpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var providerV1Endpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var providerV2Endpoint = ChannelMessagingTestSupport.GetTcpEndpoint();
        var providerRid = RoutingId.From("api-a");

        var registryHost = BuildRegistryHost(registryPubEndpoint, registryRouterEndpoint);
        var providerV1 = BuildProviderHost(registryRouterEndpoint, providerV1Endpoint, "provider-v1", providerRid);
        var providerV2 = BuildProviderHost(registryRouterEndpoint, providerV2Endpoint, "provider-v2", providerRid);
        var clientHost = BuildClientHost(registryRouterEndpoint);

        using (registryHost)
        using (providerV1)
        using (providerV2)
        using (clientHost)
        {
            await registryHost.StartAsync();
            await providerV1.StartAsync();
            await clientHost.StartAsync();

            var client = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
            var registry = registryHost.Services.GetRequiredService<IZLinkRegistryQuery>();

            var before = await RequestProbeAsync(client, "same-rid-v1");
            Assert.Equal("provider-v1", before.ProviderId);

            await providerV1.StopAsync();
            await providerV2.StartAsync();
            await WaitForSingleReadyEndpointAsync(registry, providerRid, providerV2Endpoint);

            for (var i = 0; i < 20; i++)
            {
                var reply = await RequestProbeAsync(client, $"same-rid-v2-{i}");
                Assert.Equal("provider-v2", reply.ProviderId);
            }

            await ChannelMessagingTestSupport.StopHostsAsync(clientHost, providerV2, registryHost);
        }
    }

    private static IHost BuildRegistryHost(string pubEndpoint, string routerEndpoint)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = pubEndpoint;
            options.RouterEndpoint = routerEndpoint;
        });
        return builder.Build();
    }

    private static IHost BuildClientHost(string registryRouterEndpoint)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.SetDefaultTimeout(TimeSpan.FromSeconds(2));
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
            options.AddClientServerChannel("scaleout-api").EnableClient();
        });
        return builder.Build();
    }

    private static IHost BuildProviderHost(
        string registryRouterEndpoint,
        string endpoint,
        string providerId,
        RoutingId? routingId = null)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(new ScaleoutProviderIdentity(providerId));
        builder.Services.AddSingleton<ScaleoutEvidenceRecorder>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<DiscoveryScaleoutTests>();
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
            var channel = options.AddClientServerChannel("scaleout-api");
            channel.EnableServer(endpoint);
            channel.AddHandlerGroup("scaleout-probe");
            if (routingId.HasValue)
            {
                channel.ConfigureServerRouting().RoutingId = routingId.Value;
            }
        });
        return builder.Build();
    }

    private static async Task<ScaleoutProbeReply> RequestProbeAsync(
        IZLinkChannelClient client,
        string requestId)
    {
        try
        {
            return await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () => await client
                    .RequestToChannel("scaleout-api", new ScaleoutProbeRequest { RequestId = requestId })
                    .Async<ScaleoutProbeReply>(),
                reply => reply.RequestId == requestId,
                attempts: 20,
                delayMs: 100);
        }
        catch (Exception ex)
        {
            throw new InvalidOperationException(
                "HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: requestToChannel did not reach an expected provider.",
                ex);
        }
    }

    private static async Task RunTrafficAsync(
        IZLinkChannelClient client,
        string clientId,
        ScaleoutTrafficRun traffic)
    {
        var index = 0;
        while (!traffic.IsStopped)
        {
            var requestId = $"{clientId}-{index++}";
            try
            {
                var reply = await RequestProbeAsync(client, requestId);
                traffic.Record(requestId, reply.ProviderId);
            }
            catch (Exception ex)
            {
                _ = ex;
            }
        }
    }

    private static async Task WaitForObservedProvidersAsync(
        ScaleoutTrafficRun traffic,
        params string[] providers)
    {
        try
        {
            await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                () => Task.FromResult(traffic.ObservedProviders.ToArray()),
                observed => providers.All(provider => observed.Contains(provider, StringComparer.Ordinal)),
                attempts: 60,
                delayMs: 100);
        }
        catch (Exception ex)
        {
            throw new InvalidOperationException(
                "HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: transition traffic did not observe expected providers.",
                ex);
        }
    }

    private static async Task WaitForReadyEndpointsAsync(
        IZLinkRegistryQuery registry,
        params string[] endpoints)
    {
        var expected = endpoints.ToHashSet(StringComparer.Ordinal);
        try
        {
            await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () => await registry.TopologyAsync(new ZLinkRegistryTopologyFilter(
                    AutoConnectType: ZLinkAutoConnectType.ClientServer,
                    ServiceKind: ZLinkServiceKind.Socket,
                    ServiceRole: ZLinkServiceRole.Router,
                    ChannelName: "scaleout-api",
                    State: ZLinkTopologyState.Ready)),
                entries => expected.All(endpoint => entries.Any(entry => entry.Endpoint == endpoint)),
                attempts: 40,
                delayMs: 100);
        }
        catch (Exception ex)
        {
            throw new InvalidOperationException(
                "HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: registry topology did not reach expected readiness.",
                ex);
        }
    }

    private static async Task WaitUntilEndpointIsNotReadyAsync(
        IZLinkRegistryQuery registry,
        string endpoint)
    {
        try
        {
            await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () => await registry.TopologyAsync(new ZLinkRegistryTopologyFilter(
                    AutoConnectType: ZLinkAutoConnectType.ClientServer,
                    ServiceKind: ZLinkServiceKind.Socket,
                    ServiceRole: ZLinkServiceRole.Router,
                    ChannelName: "scaleout-api",
                    State: ZLinkTopologyState.Ready)),
                entries => entries.All(entry => entry.Endpoint != endpoint),
                attempts: 60,
                delayMs: 100);
        }
        catch (Exception ex)
        {
            throw new InvalidOperationException(
                "HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: registry topology did not remove a scaled-in endpoint.",
                ex);
        }
    }

    private static async Task WaitForSingleReadyEndpointAsync(
        IZLinkRegistryQuery registry,
        RoutingId routingId,
        string endpoint)
    {
        try
        {
            await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () => await registry.TopologyAsync(new ZLinkRegistryTopologyFilter(
                    AutoConnectType: ZLinkAutoConnectType.ClientServer,
                    ServiceKind: ZLinkServiceKind.Socket,
                    ServiceRole: ZLinkServiceRole.Router,
                    ChannelName: "scaleout-api",
                    RoutingId: routingId,
                    State: ZLinkTopologyState.Ready)),
                entries => entries.Length == 1 && entries[0].Endpoint == endpoint,
                attempts: 60,
                delayMs: 100);
        }
        catch (Exception ex)
        {
            throw new InvalidOperationException(
                "HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: routing id did not resolve to a single ready endpoint.",
                ex);
        }
    }

    private static void AssertRequestIdsWereHandledOnce(
        IEnumerable<string> requestIds,
        params ScaleoutEvidenceRecorder[] recorders)
    {
        foreach (var requestId in requestIds)
        {
            var count = recorders.Sum(recorder => recorder.RequestIds.Count(id => id == requestId));
            Assert.Equal(1, count);
        }
    }
}

public sealed class ScaleoutProbeRequest
{
    public string RequestId { get; init; } = string.Empty;
}

public sealed class ScaleoutProbeReply
{
    public string RequestId { get; init; } = string.Empty;

    public string ProviderId { get; init; } = string.Empty;
}

public sealed record ScaleoutProviderIdentity(string ProviderId);

public sealed class ScaleoutEvidenceRecorder
{
    private readonly ConcurrentQueue<string> _requestIds = new();

    public IReadOnlyCollection<string> RequestIds => _requestIds.ToArray();

    public void Record(string requestId)
    {
        _requestIds.Enqueue(requestId);
    }
}

internal sealed class ScaleoutTrafficRun : IDisposable
{
    private readonly CancellationTokenSource _stop = new();
    private readonly ConcurrentQueue<string> _completedRequestIds = new();
    private readonly ConcurrentDictionary<string, byte> _observedProviders = new(StringComparer.Ordinal);

    public bool IsStopped => _stop.IsCancellationRequested;

    public IReadOnlyCollection<string> CompletedRequestIds => _completedRequestIds.ToArray();

    public IReadOnlyCollection<string> ObservedProviders => _observedProviders.Keys.ToArray();

    public void Record(string requestId, string providerId)
    {
        _completedRequestIds.Enqueue(requestId);
        _observedProviders.TryAdd(providerId, 0);
    }

    public void Stop()
    {
        _stop.Cancel();
    }

    public void Dispose()
    {
        _stop.Dispose();
    }
}

[ZLinkHandlerGroup("scaleout-probe")]
public sealed class ScaleoutProbeHandlers(
    ScaleoutProviderIdentity identity,
    ScaleoutEvidenceRecorder recorder)
{
    [ZLinkRequest]
    public ValueTask<ScaleoutProbeReply> HandleAsync(
        ScaleoutProbeRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        _ = cancellationToken;
        recorder.Record(request.RequestId);
        return ValueTask.FromResult(new ScaleoutProbeReply
        {
            RequestId = request.RequestId,
            ProviderId = identity.ProviderId,
        });
    }
}
