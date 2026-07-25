using System.Runtime.CompilerServices;
using System.Threading.Channels;

namespace Zlink.Framework.Runtime.Locations;

internal sealed class ZLinkFanoutRuntimeService : IZLinkFanoutRuntime
{
    private readonly object _gate = new();
    private readonly HashSet<string> _automaticChannels;
    private readonly Dictionary<string, ChannelState> _states =
        new(StringComparer.Ordinal);
    private readonly Dictionary<string, List<Observer>> _observers =
        new(StringComparer.Ordinal);

    internal ZLinkFanoutRuntimeService(ZLinkFrameworkRegistration registration)
    {
        _automaticChannels = registration.Channels.Values
            .Where(static channel =>
                channel.AutoConnectType == ZLinkLocationAutoConnectType.Fanout
                && channel.Subscriber?.AutomaticDiscoveryEnabled == true)
            .Select(static channel => channel.ChannelName)
            .ToHashSet(StringComparer.Ordinal);
        foreach (var channelName in _automaticChannels)
            _states[channelName] = ChannelState.Empty(channelName);
    }

    public ZLinkFanoutChannelSnapshot Snapshot(string channelName)
    {
        lock (_gate)
            return RequireState(channelName).Snapshot;
    }

    public async IAsyncEnumerable<ZLinkFanoutRuntimeEvent> ObserveAsync(
        string channelName,
        int capacity = 1024,
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        if (capacity < 1)
            throw new ArgumentOutOfRangeException(nameof(capacity));
        var observer = new Observer(capacity);
        lock (_gate)
        {
            _ = RequireState(channelName);
            if (!_observers.TryGetValue(channelName, out var observers))
                _observers[channelName] = observers = [];
            observers.Add(observer);
        }

        try
        {
            await foreach (var item in observer.Channel.Reader
                               .ReadAllAsync(cancellationToken)
                               .ConfigureAwait(false))
                yield return item;
        }
        finally
        {
            lock (_gate)
                if (_observers.TryGetValue(channelName, out var observers))
                {
                    observers.Remove(observer);
                    if (observers.Count == 0)
                        _observers.Remove(channelName);
                }
        }
    }

    internal void RecordSnapshot(
        string channelName,
        IReadOnlyList<ZLinkFanoutPublisherConnectionSnapshot> publishers,
        ZLinkLocationRuntimeSnapshot location)
    {
        lock (_gate)
        {
            var previous = RequireState(channelName);
            var now = DateTimeOffset.UtcNow;
            var nextSequence = previous.Snapshot.Sequence;
            var previousByIdentity = previous.Snapshot.Publishers.ToDictionary(
                IdentityKey,
                StringComparer.Ordinal);

            foreach (var entry in publishers)
            {
                var key = IdentityKey(entry);
                if (previousByIdentity.Remove(key, out var old)
                    && old == entry)
                    continue;
                Emit(
                    channelName,
                    new ZLinkFanoutRuntimeEvent.PublisherChanged(
                        ++nextSequence,
                        now,
                        channelName,
                        entry));
            }

            foreach (var removed in previousByIdentity.Values)
                Emit(
                    channelName,
                    new ZLinkFanoutRuntimeEvent.PublisherChanged(
                        ++nextSequence,
                        now,
                        channelName,
                        removed with
                        {
                            ConnectionIntent = false,
                            Ready = false,
                            State = ZLinkFanoutPublisherConnectionState
                                .Disconnected
                        }));

            if (previous.Snapshot.Location != location)
                Emit(
                    channelName,
                    new ZLinkFanoutRuntimeEvent.LocationChanged(
                        ++nextSequence,
                        now,
                        channelName,
                        location));

            var ordered = publishers
                .OrderBy(static entry => entry.PublisherRid.ToHex(),
                    StringComparer.Ordinal)
                .ThenBy(static entry => entry.LifecycleGeneration)
                .ToArray();
            _states[channelName] = new ChannelState(
                new ZLinkFanoutChannelSnapshot(
                    channelName,
                    ordered.Count(static entry => entry.ConnectionIntent),
                    ordered.Count(static entry => entry.Ready),
                    nextSequence,
                    now,
                    ordered,
                    location));
        }
    }

    internal void RecordLocationFailure(
        string channelName,
        DateTimeOffset? lastSuccessAt,
        DateTimeOffset failureAt)
    {
        lock (_gate)
        {
            var current = RequireState(channelName);
            RecordSnapshot(
                channelName,
                current.Snapshot.Publishers,
                new ZLinkLocationRuntimeSnapshot(
                    "degraded",
                    lastSuccessAt,
                    failureAt));
        }
    }

    private ChannelState RequireState(string channelName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(channelName);
        if (!_automaticChannels.Contains(channelName)
            || !_states.TryGetValue(channelName, out var state))
            throw new ZLinkConfigurationException(
                $"Fanout channel '{channelName}' is not an automatic subscriber.");
        return state;
    }

    private void Emit(string channelName, ZLinkFanoutRuntimeEvent item)
    {
        if (!_observers.TryGetValue(channelName, out var observers))
            return;
        foreach (var observer in observers.ToArray())
            observer.Channel.Writer.TryWrite(item);
    }

    private static string IdentityKey(
        ZLinkFanoutPublisherConnectionSnapshot entry) =>
        $"{entry.PublisherRid.ToHex()}:{entry.LifecycleGeneration}";

    private sealed record ChannelState(ZLinkFanoutChannelSnapshot Snapshot)
    {
        internal static ChannelState Empty(string channelName)
        {
            var now = DateTimeOffset.UtcNow;
            return new ChannelState(
                new ZLinkFanoutChannelSnapshot(
                    channelName,
                    0,
                    0,
                    0,
                    now,
                    Array.Empty<ZLinkFanoutPublisherConnectionSnapshot>(),
                    new ZLinkLocationRuntimeSnapshot(
                        "unknown",
                        null,
                        null)));
        }
    }

    private sealed class Observer(int capacity)
    {
        internal Channel<ZLinkFanoutRuntimeEvent> Channel { get; } =
            System.Threading.Channels.Channel
                .CreateBounded<ZLinkFanoutRuntimeEvent>(
                    new BoundedChannelOptions(capacity)
                    {
                        SingleReader = true,
                        SingleWriter = false,
                        FullMode = BoundedChannelFullMode.DropOldest
                    });
    }
}
