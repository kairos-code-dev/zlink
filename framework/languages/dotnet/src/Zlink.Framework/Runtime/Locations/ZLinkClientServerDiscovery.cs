namespace Zlink.Framework.Runtime.Locations;

internal sealed class ZLinkClientServerDiscovery : IAsyncDisposable
{
    private readonly IZLinkClientServerLocationStore _store;
    private readonly ZLinkLocationRuntime _locationRuntime;
    private readonly ZLinkLocationOptions _options;
    private readonly ZLinkOwnerLeaseTracker? _leases;
    private readonly List<ClientLoop> _clients = [];
    private readonly List<LocalServer> _servers = [];

    internal ZLinkClientServerDiscovery(
        IZLinkClientServerLocationStore store,
        ZLinkLocationRuntime locationRuntime,
        ZLinkLocationOptions options,
        ZLinkOwnerLeaseTracker? leases)
    {
        _store = store;
        _locationRuntime = locationRuntime;
        _options = options;
        _leases = leases;
    }

    internal async ValueTask StartAsync(
        ZLinkFrameworkComponentState state,
        CancellationToken cancellationToken)
    {
        foreach (var (channelName, registration) in state.Registration.Channels)
        {
            if (registration.ClientServerRole == ZLinkClientServerRole.Server
                && state.ClientServerServerBundles.TryGetValue(channelName, out var serverBundle))
            {
                var router = (IZLinkBackendRouterSocket)serverBundle.Socket;
                var server = new LocalServer(
                    channelName,
                    RoutingId.From(serverBundle.LocalRid!),
                    CreateLifecycleGeneration(),
                    AdvertisedEndpoint(
                        router.GetLastEndpoint(),
                        registration.Server!.AdvertiseHost),
                    registration.Server!.SocketConfig.Weight,
                    router);
                await PublishAsync(server, ZLinkLocationWriteIntent.NewClaim, cancellationToken)
                    .ConfigureAwait(false);
                _servers.Add(server);
            }

            if (registration.ClientServerRole == ZLinkClientServerRole.Client
                && state.ClientServerClientBundles.TryGetValue(channelName, out var clientBundle))
            {
                var loop = new ClientLoop(
                    channelName,
                    _store,
                    clientBundle,
                    (IZLinkBackendDealerSocket)clientBundle.Socket,
                    _options,
                    _leases,
                    state.ErrorSink);
                await loop.StartAsync(cancellationToken).ConfigureAwait(false);
                _clients.Add(loop);
            }
        }
    }

    internal async ValueTask<bool> MarkDrainingAsync(
        CancellationToken cancellationToken)
    {
        var published = true;
        foreach (var server in _servers)
        {
            server.Router.SetPeerWeight(0);
            server.State = ZLinkFrameworkRuntimeState.Draining;
            server.Weight = 0;
            server.Revision++;
            var result = await PublishAsync(
                    server,
                    ZLinkLocationWriteIntent.Renew,
                    cancellationToken)
                .ConfigureAwait(false);
            published &= result.Status == ZLinkLocationWriteStatus.Stored;
        }

        return published;
    }

    public async ValueTask DisposeAsync()
    {
        var failures = new List<Exception>();
        foreach (var client in _clients)
            try
            {
                await client.DisposeAsync().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        _clients.Clear();

        foreach (var server in _servers)
            try
            {
                _ = await _store.RemoveClientServerAsync(
                        new ZLinkClientServerServerDescriptorKey(
                            server.ChannelName,
                            server.Rid),
                        _locationRuntime.OwnerToken,
                        CancellationToken.None)
                    .ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        _servers.Clear();

        if (failures.Count == 1)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failures[0]).Throw();
        if (failures.Count > 1)
            throw new AggregateException(failures);
    }

    private async ValueTask<ZLinkLocationWriteResult> PublishAsync(
        LocalServer server,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken)
    {
        var owner = _locationRuntime.OwnerToken;
        var descriptor = new ZLinkClientServerServerDescriptor(
            server.ChannelName,
            server.Rid,
            server.LifecycleGeneration,
            server.Revision,
            server.Endpoint,
            server.Weight,
            server.State,
            ZLinkTransportSecurityIdentity.Plaintext,
            owner.OwnerId,
            owner.LeaseGeneration,
            default);
        var result = await _store.UpdateClientServerAsync(
                descriptor,
                intent,
                cancellationToken)
            .ConfigureAwait(false);
        if (result.Status == ZLinkLocationWriteStatus.RejectedConflict
            && intent == ZLinkLocationWriteIntent.NewClaim)
            result = await _store.UpdateClientServerAsync(
                    descriptor,
                    ZLinkLocationWriteIntent.Takeover,
                    cancellationToken)
                .ConfigureAwait(false);
        if (result.Status != ZLinkLocationWriteStatus.Stored)
            throw new ZLinkConfigurationException(
                $"ClientServer server descriptor '{server.ChannelName}' could not be published.");
        return result;
    }

    private static ulong CreateLifecycleGeneration()
    {
        Span<byte> bytes = stackalloc byte[sizeof(ulong)];
        ulong value;
        do
        {
            System.Security.Cryptography.RandomNumberGenerator.Fill(bytes);
            value = System.Buffers.Binary.BinaryPrimitives.ReadUInt64BigEndian(bytes);
        } while (value == 0);
        return value;
    }

    private static string AdvertisedEndpoint(
        string boundEndpoint,
        string? advertiseHost)
    {
        if (string.IsNullOrWhiteSpace(advertiseHost))
            return boundEndpoint;
        var endpoint = new Uri(boundEndpoint, UriKind.Absolute);
        return new UriBuilder(endpoint) { Host = advertiseHost }.Uri
            .GetComponents(UriComponents.SchemeAndServer, UriFormat.Unescaped);
    }

    private sealed class LocalServer(
        string channelName,
        RoutingId rid,
        ulong lifecycleGeneration,
        string endpoint,
        int weight,
        IZLinkBackendRouterSocket router)
    {
        internal string ChannelName { get; } = channelName;
        internal RoutingId Rid { get; } = rid;
        internal ulong LifecycleGeneration { get; } = lifecycleGeneration;
        internal string Endpoint { get; } = endpoint;
        internal int Weight { get; set; } = weight;
        internal ulong Revision { get; set; } = 1;
        internal ZLinkFrameworkRuntimeState State { get; set; } =
            ZLinkFrameworkRuntimeState.Serving;
        internal IZLinkBackendRouterSocket Router { get; } = router;
    }

    private sealed class ClientLoop(
        string channelName,
        IZLinkClientServerLocationStore store,
        ZLinkChannelRuntimeBundle bundle,
        IZLinkBackendDealerSocket socket,
        ZLinkLocationOptions options,
        ZLinkOwnerLeaseTracker? leases,
        IZLinkRuntimeErrorSink errorSink) : IAsyncDisposable
    {
        private readonly Dictionary<string, Target> _active = new(StringComparer.Ordinal);
        private readonly Dictionary<string, ulong> _observedRevisions = new(StringComparer.Ordinal);
        private CancellationTokenSource? _stop;
        private Task? _loop;

        internal async ValueTask StartAsync(CancellationToken cancellationToken)
        {
            await ReconcileAsync(cancellationToken).ConfigureAwait(false);
            _stop = new CancellationTokenSource();
            _loop = Task.Run(() => RunAsync(_stop.Token), CancellationToken.None);
        }

        private async Task RunAsync(CancellationToken cancellationToken)
        {
            using var timer = new PeriodicTimer(options.PollingInterval);
            while (await timer.WaitForNextTickAsync(cancellationToken).ConfigureAwait(false))
                try
                {
                    await ReconcileAsync(cancellationToken).ConfigureAwait(false);
                }
                catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
                {
                    break;
                }
                catch (Exception exception)
                {
                    // Fail-static: retain the last successful connection set.
                    errorSink.ReportRuntimeTaskException(
                        $"client-server-discovery:{channelName}",
                        exception);
                }
        }

        private async ValueTask ReconcileAsync(CancellationToken cancellationToken)
        {
            var rows = await ListAllAsync(cancellationToken).ConfigureAwait(false);
            var desired = new Dictionary<string, Target>(StringComparer.Ordinal);
            foreach (var row in rows)
            {
                if (row.ServerRid.Size == 0
                    || row.LifecycleGeneration == 0
                    || row.DescriptorRevision == 0
                    || string.IsNullOrWhiteSpace(row.Endpoint)
                    || row.Weight is < 0 or > 100)
                    continue;
                if (leases is not null
                    && !await leases.IsOwnerTokenLiveAsync(
                            new ZLinkLocationOwnerToken(
                                row.OwnerId,
                                row.LeaseGeneration),
                            cancellationToken)
                        .ConfigureAwait(false))
                    continue;

                var lifecycleKey = $"{row.ServerRid.ToHex()}:{row.LifecycleGeneration}";
                var revisionKey = lifecycleKey;
                if (_observedRevisions.TryGetValue(revisionKey, out var observed)
                    && row.DescriptorRevision < observed)
                    continue;
                _observedRevisions[revisionKey] = row.DescriptorRevision;
                desired[lifecycleKey] = new Target(
                    lifecycleKey,
                    row.ServerRid,
                    row.LifecycleGeneration,
                    row.Endpoint,
                    row.Weight,
                    row.State == ZLinkFrameworkRuntimeState.Serving
                    && row.Weight > 0);
            }

            foreach (var key in _active.Keys.Where(key => !desired.ContainsKey(key)).ToArray())
            {
                var target = _active[key];
                if (bundle.DisconnectAuto(socket, target.Endpoint))
                    _active.Remove(key);
            }

            foreach (var (key, target) in desired)
            {
                if (_active.TryGetValue(key, out var active)
                    && StringComparer.Ordinal.Equals(active.Endpoint, target.Endpoint))
                {
                    _active[key] = target;
                    continue;
                }
                if (!target.Selectable)
                    continue;
                if (_active.TryGetValue(key, out active))
                    bundle.DisconnectAuto(socket, active.Endpoint);
                if (bundle.ConnectAuto(socket, target.Endpoint))
                    _active[key] = target;
            }
        }

        private async ValueTask<IReadOnlyList<ZLinkClientServerServerDescriptor>>
            ListAllAsync(CancellationToken cancellationToken)
        {
            var result = new List<ZLinkClientServerServerDescriptor>();
            string? continuation = null;
            do
            {
                var page = await store.ListClientServersAsync(
                        channelName,
                        new ZLinkPageRequest(256, continuation),
                        cancellationToken)
                    .ConfigureAwait(false);
                result.AddRange(page.Items);
                continuation = page.ContinuationToken;
            } while (continuation is not null);
            return result;
        }

        public async ValueTask DisposeAsync()
        {
            if (_stop is not null)
            {
                await _stop.CancelAsync().ConfigureAwait(false);
                if (_loop is not null)
                    try
                    {
                        await _loop.ConfigureAwait(false);
                    }
                    catch (OperationCanceledException)
                    {
                    }
                _stop.Dispose();
                _stop = null;
                _loop = null;
            }
            foreach (var target in _active.Values)
                bundle.DisconnectAuto(socket, target.Endpoint);
            _active.Clear();
        }

        private sealed record Target(
            string Key,
            RoutingId Rid,
            ulong LifecycleGeneration,
            string Endpoint,
            int Weight,
            bool Selectable);
    }
}
