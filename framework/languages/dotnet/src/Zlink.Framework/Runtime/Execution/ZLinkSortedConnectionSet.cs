namespace Zlink.Framework.Runtime.Execution;

internal sealed class ZLinkSortedConnectionSet
{
    private readonly HashSet<string> _items = new(StringComparer.Ordinal);
    private string[] _snapshot = [];

    public bool Add(string endpoint)
    {
        if (!_items.Add(endpoint)) return false;

        RefreshSnapshot();
        return true;
    }

    public bool Remove(string endpoint)
    {
        if (!_items.Remove(endpoint)) return false;

        RefreshSnapshot();
        return true;
    }

    public bool Contains(string endpoint)
    {
        return _items.Contains(endpoint);
    }

    public IReadOnlyList<string> Snapshot()
    {
        return _snapshot;
    }

    private void RefreshSnapshot()
    {
        _snapshot = _items
            .OrderBy(static endpoint => endpoint, StringComparer.Ordinal)
            .ToArray();
    }
}