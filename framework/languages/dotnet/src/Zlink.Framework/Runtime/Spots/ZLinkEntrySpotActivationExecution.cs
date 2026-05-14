namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkEntrySpotActivation
{
    private async ValueTask ExecuteAsync(
        Func<ZLinkEntrySpotActivation, CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        if (ReferenceEquals(Current.Value, this))
        {
            await operation(this, cancellationToken).ConfigureAwait(false);
            return;
        }

        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        var previous = Current.Value;
        Current.Value = this;
        try
        {
            await operation(this, cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            Current.Value = previous;
            _gate.Release();
        }
    }

    private async ValueTask ExecuteAsync<TState>(
        Func<ZLinkEntrySpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        if (ReferenceEquals(Current.Value, this))
        {
            await operation(this, state, cancellationToken).ConfigureAwait(false);
            return;
        }

        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        var previous = Current.Value;
        Current.Value = this;
        try
        {
            await operation(this, state, cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            Current.Value = previous;
            _gate.Release();
        }
    }

    private async ValueTask InvokeActorPacketWithoutLifecycleGateAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var previous = Current.Value;
        Current.Value = this;
        try
        {
            await _handlerExecutor.InvokeActorPacketAsync(
                    descriptor,
                    actor,
                    header,
                    body,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            Current.Value = previous;
        }
    }

    private async ValueTask<byte[]> InvokeActorPacketForReplyWithoutLifecycleGateAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var previous = Current.Value;
        Current.Value = this;
        try
        {
            return await _handlerExecutor.InvokeActorPacketForReplyAsync(
                    descriptor,
                    actor,
                    header,
                    body,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            Current.Value = previous;
        }
    }
}
