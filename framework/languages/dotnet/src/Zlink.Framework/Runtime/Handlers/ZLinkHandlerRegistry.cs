using System.Collections.Concurrent;

namespace Zlink.Framework.Runtime.Handlers;

internal sealed class ZLinkHandlerRegistry
{
    private readonly IReadOnlyDictionary<string, IReadOnlyList<ZLinkHandlerEndpointDescriptor>> _requests;
    private readonly IReadOnlyDictionary<string, IReadOnlyList<ZLinkHandlerEndpointDescriptor>> _commands;
    private readonly IReadOnlyDictionary<string, IReadOnlyList<ZLinkHandlerEndpointDescriptor>> _publishes;
    private readonly ConcurrentDictionary<ZLinkHandlerSelectionKey, ZLinkHandlerEndpointDescriptor> _singleSelections = new();
    private readonly ConcurrentDictionary<ZLinkHandlerSelectionKey, IReadOnlyList<ZLinkHandlerEndpointDescriptor>> _publishSelections = new();

    public ZLinkHandlerRegistry(IEnumerable<ZLinkHandlerEndpointDescriptor> endpoints)
    {
        var requests = new Dictionary<string, List<ZLinkHandlerEndpointDescriptor>>(StringComparer.Ordinal);
        var commands = new Dictionary<string, List<ZLinkHandlerEndpointDescriptor>>(StringComparer.Ordinal);
        var publishes = new Dictionary<string, List<ZLinkHandlerEndpointDescriptor>>(StringComparer.Ordinal);

        foreach (var endpoint in endpoints)
        {
            switch (endpoint.Kind)
            {
                case ZLinkMessageKind.Request:
                    AddEndpoint(requests, endpoint);
                    break;
                case ZLinkMessageKind.Command:
                    AddEndpoint(commands, endpoint);
                    break;
                case ZLinkMessageKind.Publish:
                    AddEndpoint(publishes, endpoint);
                    break;
            }
        }

        _requests = Freeze(requests);
        _commands = Freeze(commands);
        _publishes = Freeze(publishes);
    }

    public ZLinkHandlerEndpointDescriptor GetRequest(
        string channelName,
        IReadOnlySet<string> mappedGroups,
        string messageName)
    {
        var key = new ZLinkHandlerSelectionKey(ZLinkMessageKind.Request, channelName, messageName);
        if (_singleSelections.TryGetValue(key, out var cached))
        {
            return cached;
        }

        var selected = _requests.TryGetValue(messageName, out var endpoints)
            ? SelectEndpoint(channelName, mappedGroups, messageName, endpoints, "request")
            : throw new InvalidOperationException($"No request handler is registered for '{messageName}'.");
        return _singleSelections.GetOrAdd(key, selected);
    }

    public ZLinkHandlerEndpointDescriptor GetCommand(
        string channelName,
        IReadOnlySet<string> mappedGroups,
        string messageName)
    {
        var key = new ZLinkHandlerSelectionKey(ZLinkMessageKind.Command, channelName, messageName);
        if (_singleSelections.TryGetValue(key, out var cached))
        {
            return cached;
        }

        var selected = _commands.TryGetValue(messageName, out var endpoints)
            ? SelectEndpoint(channelName, mappedGroups, messageName, endpoints, "send")
            : throw new InvalidOperationException($"No send handler is registered for '{messageName}'.");
        return _singleSelections.GetOrAdd(key, selected);
    }

    public IReadOnlyList<ZLinkHandlerEndpointDescriptor> GetPublishes(
        string channelName,
        IReadOnlySet<string> mappedGroups,
        string messageName)
    {
        var key = new ZLinkHandlerSelectionKey(ZLinkMessageKind.Publish, channelName, messageName);
        if (_publishSelections.TryGetValue(key, out var cached))
        {
            return cached;
        }

        var selected = _publishes.TryGetValue(messageName, out var endpoints)
            ? FilterEndpoints(mappedGroups, endpoints)
            : Array.Empty<ZLinkHandlerEndpointDescriptor>();
        return _publishSelections.GetOrAdd(key, selected);
    }

    private static void AddEndpoint(
        Dictionary<string, List<ZLinkHandlerEndpointDescriptor>> targets,
        ZLinkHandlerEndpointDescriptor endpoint)
    {
        if (!targets.TryGetValue(endpoint.MessageName, out var list))
        {
            list = [];
            targets.Add(endpoint.MessageName, list);
        }

        list.Add(endpoint);
    }

    private static IReadOnlyDictionary<string, IReadOnlyList<ZLinkHandlerEndpointDescriptor>> Freeze(
        Dictionary<string, List<ZLinkHandlerEndpointDescriptor>> source)
    {
        return source.ToDictionary(
            static entry => entry.Key,
            static entry => (IReadOnlyList<ZLinkHandlerEndpointDescriptor>)entry.Value.ToArray(),
            StringComparer.Ordinal);
    }

    private static ZLinkHandlerEndpointDescriptor SelectEndpoint(
        string channelName,
        IReadOnlySet<string> mappedGroups,
        string messageName,
        IReadOnlyList<ZLinkHandlerEndpointDescriptor> endpoints,
        string kind)
    {
        var matches = FilterEndpoints(mappedGroups, endpoints);
        return matches.Count switch
        {
            1 => matches[0],
            0 => throw new InvalidOperationException(
                $"No {kind} handler is mapped for '{channelName}:{messageName}'."),
            _ => throw new ZLinkConfigurationException(
                $"Duplicate {kind} handler packet '{messageName}' is mapped to channel '{channelName}'.")
        };
    }

    private static IReadOnlyList<ZLinkHandlerEndpointDescriptor> FilterEndpoints(
        IReadOnlySet<string> mappedGroups,
        IReadOnlyList<ZLinkHandlerEndpointDescriptor> endpoints)
    {
        if (mappedGroups.Count == 0)
        {
            return endpoints;
        }

        List<ZLinkHandlerEndpointDescriptor>? matches = null;
        foreach (var endpoint in endpoints)
        {
            if (endpoint.Groups.Count == 0 || !endpoint.Groups.Any(mappedGroups.Contains))
            {
                continue;
            }

            matches ??= [];
            matches.Add(endpoint);
        }

        return matches is null
            ? Array.Empty<ZLinkHandlerEndpointDescriptor>()
            : matches.ToArray();
    }
}
