namespace TicTacToe.Server.Play.Sessions;

internal sealed class InMemoryActorSessionBindingStore : IZLinkActorSessionBindingStore
{
    private readonly object _gate = new();
    private readonly Dictionary<string, ZLinkActorSessionBinding> _bindings = new(StringComparer.Ordinal);

    public ValueTask BindSessionAsync(
        ZLinkActorSessionBinding binding,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            _bindings[binding.ActorId] = binding;
        }

        return ValueTask.CompletedTask;
    }

    public ValueTask UnbindSessionAsync(
        ZLinkActorSessionUnbind binding,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (_bindings.TryGetValue(binding.ActorId, out var current)
                && string.Equals(current.BindingToken, binding.BindingToken, StringComparison.Ordinal))
            {
                _bindings.Remove(binding.ActorId);
            }
        }

        return ValueTask.CompletedTask;
    }

    public ValueTask<ZLinkActorSessionRoute> FindSessionAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (!_bindings.TryGetValue(actorId, out var binding))
            {
                throw new InvalidOperationException($"No session binding exists for actor {actorId}.");
            }

            return ValueTask.FromResult(new ZLinkActorSessionRoute(
                binding.SessionRouterId,
                binding.BindingToken));
        }
    }
}
