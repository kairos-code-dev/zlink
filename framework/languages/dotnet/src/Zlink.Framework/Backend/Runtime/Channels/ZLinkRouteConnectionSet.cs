using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteConnectionSet(IZLinkBackendRouterSocket router)
{
    private readonly HashSet<string> _manualConnections = new(StringComparer.Ordinal);

    public void Connect(string endpoint)
    {
        if (_manualConnections.Add(endpoint))
        {
            router.Connect(endpoint);
        }
    }

    public void Disconnect(string endpoint)
    {
        router.Disconnect(endpoint);
        _manualConnections.Remove(endpoint);
    }

    public IReadOnlyList<string> List()
    {
        return _manualConnections.OrderBy(static endpoint => endpoint, StringComparer.Ordinal).ToArray();
    }
}
