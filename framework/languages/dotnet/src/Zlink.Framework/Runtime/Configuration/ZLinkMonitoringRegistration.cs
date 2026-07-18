namespace Zlink.Framework.Runtime.Configuration;

internal sealed class ZLinkMonitoringRegistration
{
    public Dictionary<string, ZLinkSocketMonitoringRegistration> SocketSources { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkPollingMonitoringRegistration> SpotSources { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkPollingMonitoringRegistration> LocationRuntimeSources { get; } =
        new(StringComparer.Ordinal);

    // MeshNode runtime event sources (spec 50): keyed by mesh name, bridged
    // from IZLinkRouteMeshRuntime.ObserveAsync into the runtime event bus.
    public HashSet<string> MeshNodeSources { get; } = new(StringComparer.Ordinal);

    public HashSet<string> LocationPeerSources { get; } = new(StringComparer.Ordinal);

    public HashSet<string> LocationSpotSources { get; } = new(StringComparer.Ordinal);

    public HashSet<string> LocationActorSources { get; } = new(StringComparer.Ordinal);

    public bool HasLocationSources =>
        LocationRuntimeSources.Count > 0
        || LocationPeerSources.Count > 0
        || LocationSpotSources.Count > 0
        || LocationActorSources.Count > 0;
}

internal sealed class ZLinkSocketMonitoringRegistration
{
    public required string SourceName { get; init; }

    public HashSet<ZLinkSocketEventKind> Events { get; } = [];
}

internal sealed class ZLinkPollingMonitoringRegistration
{
    public required string SourceName { get; init; }

    public required TimeSpan Interval { get; init; }
}

internal sealed class ZLinkMonitoringOptionsModel(ZLinkMonitoringRegistration registration) : IZLinkMonitoringOptions
{
    public void AddSocketEvents(string sourceName, params ZLinkSocketEventKind[] events)
    {
        var entry = new ZLinkSocketMonitoringRegistration
        {
            SourceName = ValidateSourceName(sourceName)
        };

        foreach (var @event in events) entry.Events.Add(@event);

        if (!registration.SocketSources.TryAdd(entry.SourceName, entry))
            throw new ZLinkConfigurationException(
                $"Duplicate monitoring socket source '{entry.SourceName}'.");
    }

    public void AddMeshNodeEvents(string meshName)
    {
        if (string.IsNullOrWhiteSpace(meshName))
            throw new ZLinkConfigurationException("Mesh monitoring requires a mesh name.");
        if (!registration.MeshNodeSources.Add(meshName))
            throw new ZLinkConfigurationException(
                $"Duplicate monitoring mesh source '{meshName}'.");
    }

    public void AddSpotEvents(string sourceName, TimeSpan interval)
    {
        AddPollingSource(
            registration.SpotSources,
            "spot",
            sourceName,
            interval);
    }

    public void AddLocationRuntimeEvents(string sourceName, TimeSpan interval)
    {
        AddPollingSource(
            registration.LocationRuntimeSources,
            "location-runtime",
            sourceName,
            interval);
    }

    public void AddLocationPeerEvents(string sourceName) =>
        AddPushSource(registration.LocationPeerSources, "location-peer", sourceName);

    public void AddLocationSpotEvents(string sourceName) =>
        AddPushSource(registration.LocationSpotSources, "location-spot", sourceName);

    public void AddLocationActorEvents(string sourceName) =>
        AddPushSource(registration.LocationActorSources, "location-actor", sourceName);

    private static void AddPushSource(
        HashSet<string> sources,
        string kind,
        string sourceName)
    {
        if (!sources.Add(ValidateSourceName(sourceName)))
            throw new ZLinkConfigurationException(
                $"Duplicate monitoring {kind} source '{sourceName}'.");
    }

    private static void AddPollingSource(
        Dictionary<string, ZLinkPollingMonitoringRegistration> sources,
        string kind,
        string sourceName,
        TimeSpan interval)
    {
        if (interval <= TimeSpan.Zero)
            throw new ZLinkConfigurationException(
                $"Monitoring {kind} interval must be greater than zero.");

        var normalized = ValidateSourceName(sourceName);
        var entry = new ZLinkPollingMonitoringRegistration
        {
            SourceName = normalized,
            Interval = interval
        };

        if (!sources.TryAdd(normalized, entry))
            throw new ZLinkConfigurationException(
                $"Duplicate monitoring {kind} source '{normalized}'.");
    }

    private static string ValidateSourceName(string sourceName)
    {
        if (string.IsNullOrWhiteSpace(sourceName))
            throw new ZLinkConfigurationException("Monitoring source name must not be empty.");

        return sourceName;
    }
}
