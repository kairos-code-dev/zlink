using System.Runtime.CompilerServices;
using System.Threading.Channels;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkClientServerRuntimeService(
    ZLinkFrameworkRuntime runtime,
    ZLinkLocationStoreHealth? storeHealth) : IZLinkClientServerRuntime
{
    private static readonly TimeSpan PollInterval =
        TimeSpan.FromMilliseconds(10);
    private readonly object _gate = new();
    private readonly Dictionary<string, SequenceState> _sequences =
        new(StringComparer.Ordinal);

    public ZLinkClientServerChannelSnapshot Snapshot(string channelName)
    {
        ArgumentException.ThrowIfNullOrEmpty(channelName);
        var state = runtime.ClientServerMonitoringState(channelName);
        var servers = state.Client?.SnapshotConnections()
            .Where(static entry => entry.ServerRid is not null)
            .Select(static entry => new ZLinkClientServerServerSnapshot(
                entry.ServerRid!.Value,
                entry.LifecycleGeneration!.Value,
                entry.DescriptorRevision!.Value,
                entry.Endpoint,
                entry.Weight,
                entry.Ready,
                entry.State,
                entry.DescriptorSource,
                entry.LastFailure))
            .ToList() ?? [];

        if (state.Server is { } localServer)
        {
            var local = localServer.Read();
            var existing = servers.FindIndex(
                entry => entry.ServerRid == localServer.ServerRid
                         && entry.LifecycleGeneration
                         == localServer.LifecycleGeneration);
            if (existing < 0)
                servers.Add(new ZLinkClientServerServerSnapshot(
                    localServer.ServerRid,
                    localServer.LifecycleGeneration,
                    local.Revision,
                    local.AdvertisedEndpoint,
                    local.Weight,
                    local.State == ZLinkFrameworkRuntimeState.Serving
                    && local.Weight > 0,
                    Map(local.State),
                    "manual",
                    LastFailure: null));
        }

        servers.Sort(static (left, right) =>
        {
            var rid = StringComparer.Ordinal.Compare(
                left.ServerRid.ToHex(),
                right.ServerRid.ToHex());
            return rid != 0
                ? rid
                : left.LifecycleGeneration.CompareTo(
                    right.LifecycleGeneration);
        });
        var location = LocationSnapshot();
        var role = state.HasClient && state.HasServer
            ? Zlink.Framework.Contracts.Configuration.ZLinkClientServerRole
                .ClientAndServer
            : state.HasClient
                ? Zlink.Framework.Contracts.Configuration.ZLinkClientServerRole
                    .Client
                : Zlink.Framework.Contracts.Configuration.ZLinkClientServerRole
                    .Server;
        var readyCount = servers.Count(static entry => entry.Ready);
        var fingerprint = new Fingerprint(
            role,
            state.Client?.ConnectionIntentCount ?? 0,
            state.Client?.PendingRequestCount ?? 0,
            location,
            string.Join(
                "\n",
                servers.Select(static entry =>
                    $"{entry.ServerRid.ToHex()}|{entry.LifecycleGeneration}|"
                    + $"{entry.DescriptorRevision}|{entry.Endpoint}|"
                    + $"{entry.Weight}|{entry.Ready}|{entry.State}|"
                    + $"{entry.DescriptorSource}|{entry.LastFailure}")));
        var sequence = Sequence(channelName, fingerprint);
        return new ZLinkClientServerChannelSnapshot(
            channelName,
            role,
            Selectable: state.HasClient
                        && runtime.IsStarted
                        && readyCount > 0,
            readyCount,
            fingerprint.ConnectionIntentCount,
            fingerprint.PendingRequestCount,
            sequence,
            DateTimeOffset.UtcNow,
            servers,
            location);
    }

    public bool IsReady(string channelName)
    {
        var snapshot = Snapshot(channelName);
        return runtime.IsStarted
               && snapshot.Servers.Any(static server => server.Ready);
    }

    public async IAsyncEnumerable<ZLinkClientServerRuntimeEvent> ObserveAsync(
        string channelName,
        int capacity = 1024,
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        if (capacity <= 0)
            throw new ArgumentOutOfRangeException(nameof(capacity));
        _ = Snapshot(channelName);

        var queue = Channel.CreateBounded<ZLinkClientServerRuntimeEvent>(
            new BoundedChannelOptions(capacity)
            {
                FullMode = BoundedChannelFullMode.DropWrite,
                SingleReader = true,
                SingleWriter = true
            },
            static dropped => ZLinkRuntimeMetrics.RecordObserverOverflow(
                dropped.Identifier));
        using var stop =
            CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        var producer = ProduceAsync(channelName, queue.Writer, stop.Token);
        try
        {
            while (await queue.Reader.WaitToReadAsync(cancellationToken)
                       .ConfigureAwait(false))
                while (queue.Reader.TryRead(out var change))
                    yield return change;
        }
        finally
        {
            stop.Cancel();
            try
            {
                await producer.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
                when (stop.IsCancellationRequested)
            {
            }
            while (queue.Reader.TryRead(out _))
            {
            }
        }
    }

    private async Task ProduceAsync(
        string channelName,
        ChannelWriter<ZLinkClientServerRuntimeEvent> writer,
        CancellationToken cancellationToken)
    {
        var previous = Snapshot(channelName);
        try
        {
            using var timer = new PeriodicTimer(PollInterval);
            while (await timer.WaitForNextTickAsync(cancellationToken)
                       .ConfigureAwait(false))
            {
                var current = Snapshot(channelName);
                if (current.Sequence == previous.Sequence) continue;
                foreach (var change in Changes(previous, current))
                    if (!writer.TryWrite(change))
                        ZLinkRuntimeMetrics.RecordObserverOverflow(
                            change.Identifier);
                previous = current;
            }
        }
        finally
        {
            writer.TryComplete();
        }
    }

    private static IEnumerable<ZLinkClientServerRuntimeEvent> Changes(
        ZLinkClientServerChannelSnapshot previous,
        ZLinkClientServerChannelSnapshot current)
    {
        var prior = previous.Servers.ToDictionary(
            static entry =>
                $"{entry.ServerRid.ToHex()}:{entry.LifecycleGeneration}",
            StringComparer.Ordinal);
        var changed = false;
        foreach (var server in current.Servers)
        {
            var key =
                $"{server.ServerRid.ToHex()}:{server.LifecycleGeneration}";
            if (prior.Remove(key, out var old) && old == server) continue;
            changed = true;
            yield return Event(current, server, server.LastFailure);
        }
        foreach (var removed in prior.Values)
        {
            changed = true;
            yield return Event(
                current,
                removed with
                {
                    Ready = false,
                    State = ZLinkClientServerServerState.Disconnected
                },
                "removed");
        }
        if (!changed)
            yield return new ZLinkClientServerRuntimeEvent(
                "zlink.runtime.client_server.state_changed",
                current.Sequence,
                current.ObservedAt,
                current.ChannelName,
                ServerRid: null,
                LifecycleGeneration: null,
                DescriptorRevision: null,
                Weight: null,
                Ready: current.Selectable,
                State: null,
                Reason: null);
    }

    private static ZLinkClientServerRuntimeEvent Event(
        ZLinkClientServerChannelSnapshot current,
        ZLinkClientServerServerSnapshot server,
        string? reason) =>
        new(
            "zlink.runtime.client_server.server_changed",
            current.Sequence,
            current.ObservedAt,
            current.ChannelName,
            server.ServerRid,
            server.LifecycleGeneration,
            server.DescriptorRevision,
            server.Weight,
            server.Ready,
            server.State,
            reason);

    private ulong Sequence(string channelName, Fingerprint fingerprint)
    {
        lock (_gate)
        {
            if (!_sequences.TryGetValue(channelName, out var state))
            {
                _sequences[channelName] = new SequenceState(1, fingerprint);
                return 1;
            }
            if (state.Fingerprint == fingerprint) return state.Sequence;
            var next = checked(state.Sequence + 1);
            _sequences[channelName] = new SequenceState(next, fingerprint);
            return next;
        }
    }

    private ZLinkLocationRuntimeSnapshot LocationSnapshot()
    {
        if (storeHealth is null)
            return new ZLinkLocationRuntimeSnapshot(
                "not_configured",
                null,
                null);
        var snapshot = storeHealth.GetSnapshot();
        return new ZLinkLocationRuntimeSnapshot(
            snapshot.Healthy ? "ready" : "degraded",
            snapshot.LastSuccessAt,
            snapshot.LastFailureAt);
    }

    private static ZLinkClientServerServerState Map(
        ZLinkFrameworkRuntimeState state) =>
        state switch
        {
            ZLinkFrameworkRuntimeState.Serving =>
                ZLinkClientServerServerState.Ready,
            ZLinkFrameworkRuntimeState.Draining or
                ZLinkFrameworkRuntimeState.Relocating =>
                ZLinkClientServerServerState.Draining,
            ZLinkFrameworkRuntimeState.Stopped =>
                ZLinkClientServerServerState.Disconnected,
            ZLinkFrameworkRuntimeState.Error =>
                ZLinkClientServerServerState.Rejected,
            _ => ZLinkClientServerServerState.Configured
        };

    private sealed record Fingerprint(
        Zlink.Framework.Contracts.Configuration.ZLinkClientServerRole Role,
        int ConnectionIntentCount,
        int PendingRequestCount,
        ZLinkLocationRuntimeSnapshot Location,
        string ServerFingerprint);

    private sealed record SequenceState(
        ulong Sequence,
        Fingerprint Fingerprint);
}
