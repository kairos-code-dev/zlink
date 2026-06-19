namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkBoundSessionDispatchScope : IAsyncDisposable
{
    private static readonly AsyncLocal<ZLinkBoundSessionDispatchScope?> CurrentScope = new();
    private readonly ZLinkBoundSessionDispatchScope? _previous;
    private readonly List<Func<CancellationToken, ValueTask>> _deferredCloses = new();
    private readonly string _actorId;
    private bool _drained;

    private ZLinkBoundSessionDispatchScope(string actorId)
    {
        _actorId = actorId;
        _previous = CurrentScope.Value;
        CurrentScope.Value = this;
    }

    public static ZLinkBoundSessionDispatchScope Enter(string actorId)
    {
        return new ZLinkBoundSessionDispatchScope(actorId);
    }

    public static bool TryDeferClose(
        string actorId,
        Func<CancellationToken, ValueTask> closeAsync)
        => TryDefer(actorId, closeAsync);

    public static bool TryDefer(
        string actorId,
        Func<CancellationToken, ValueTask> operationAsync)
    {
        var scope = CurrentScope.Value;
        if (scope is null
            || !string.Equals(scope._actorId, actorId, StringComparison.Ordinal)
            || scope._drained)
        {
            return false;
        }

        scope._deferredCloses.Add(operationAsync);
        return true;
    }

    public async ValueTask DrainAsync(CancellationToken cancellationToken)
    {
        if (_drained)
        {
            return;
        }

        _drained = true;
        foreach (var closeAsync in _deferredCloses)
        {
            await closeAsync(cancellationToken).ConfigureAwait(false);
        }

        _deferredCloses.Clear();
    }

    public async ValueTask DisposeAsync()
    {
        try
        {
            await DrainAsync(CancellationToken.None).ConfigureAwait(false);
        }
        finally
        {
            CurrentScope.Value = _previous;
        }
    }
}
