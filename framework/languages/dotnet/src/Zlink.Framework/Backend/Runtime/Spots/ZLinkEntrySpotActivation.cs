using Microsoft.Extensions.DependencyInjection;
using Systems.Zlink.Stream.Connector.Protocol;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkEntrySpotActivation : IZLinkEntrySpotContext, IAsyncDisposable
{
    private static readonly AsyncLocal<ZLinkEntrySpotActivation?> Current = new();
    private readonly IServiceProvider _services;
    private readonly AsyncServiceScope _scope;
    private readonly ZLinkSpotActorHandlerRegistry _actorHandlers =
        new(ZLinkSpotActorHandlerSurface.EntrySpot);
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly CancellationTokenSource _stopSource = new();
    private readonly ZLinkSpotHandlerInvoker _invoker;
    private bool _configurationOpen = true;
    private int _disposed;

    public ZLinkEntrySpotActivation(
        IServiceProvider services,
        Type entrySpotType,
        RoutingId nodeRid)
    {
        _services = services;
        NodeRid = nodeRid;
        _scope = services.CreateAsyncScope();
        EntrySpot = (IZLinkEntrySpot)ActivatorUtilities.CreateInstance(
            _scope.ServiceProvider,
            entrySpotType,
            this);
        _invoker = new ZLinkSpotHandlerInvoker(_scope.ServiceProvider, EntrySpot);
    }

    public IZLinkEntrySpot EntrySpot { get; }

    public RoutingId NodeRid { get; }

    public void Configure()
    {
        EntrySpot.Configure();
        _configurationOpen = false;
        _actorHandlers.Bind();
    }

    public async ValueTask InitializeAsync(CancellationToken cancellationToken)
    {
        await ExecuteAsync(
            static (activation, ct) => activation.EntrySpot.OnInitializeAsync(ct),
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        await ExecuteAsync(
            static (activation, ct) => activation.EntrySpot.OnClosingAsync(ct),
            cancellationToken).ConfigureAwait(false);
    }

    public void AddActorPacket<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor
    {
        AddActorPacketCore<THandler, TActor>(null);
    }

    public void AddActorPacket<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor
    {
        if (string.IsNullOrWhiteSpace(packetName))
        {
            throw new InvalidOperationException("Actor packet name must not be empty.");
        }

        AddActorPacketCore<THandler, TActor>(packetName);
    }

    public void AddActorJoined<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor
    {
        EnsureConfigurationOpen();
        _actorHandlers.AddJoined(typeof(THandler), typeof(TActor));
    }

    public void AddActorLeft<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor
    {
        EnsureConfigurationOpen();
        _actorHandlers.AddLeft(typeof(THandler), typeof(TActor));
    }

    public bool TryResolveActorPacket(
        Type actorType,
        ZlinkStreamHeader header,
        out ZLinkSpotActorPacketDescriptor? descriptor)
    {
        return _actorHandlers.TryResolve(actorType, header, out descriptor);
    }

    public bool TryResolveActorJoined(
        Type actorType,
        out ZLinkSpotActorLifecycleDescriptor? descriptor)
    {
        return _actorHandlers.TryResolveJoined(actorType, out descriptor);
    }

    public bool TryResolveActorLeft(
        Type actorType,
        out ZLinkSpotActorLifecycleDescriptor? descriptor)
    {
        return _actorHandlers.TryResolveLeft(actorType, out descriptor);
    }

    public ValueTask InvokeActorPacketAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        return InvokeActorPacketWithoutLifecycleGateAsync(
            descriptor,
            actor,
            header,
            body,
            cancellationToken);
    }

    public async ValueTask<byte[]> InvokeActorPacketForReplyAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var reply = await InvokeActorPacketForReplyWithoutLifecycleGateAsync(
                descriptor,
                actor,
                header,
                body,
                cancellationToken)
            .ConfigureAwait(false);

        return reply
            ?? throw new InvalidOperationException(
                $"Entry Spot actor packet reply for '{descriptor.MessageName}' was null.");
    }

    public ValueTask InvokeActorLifecycleAsync(
        ZLinkSpotActorLifecycleDescriptor descriptor,
        IZLinkActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        return ExecuteAsync(
            static (activation, state, ct) =>
                activation._invoker.InvokeActorLifecycleAsync(
                    state.Descriptor,
                    state.Actor,
                    state.Info,
                    ct),
            new ActorLifecycleState(descriptor, actor, info),
            cancellationToken);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return;
        }

        _stopSource.Cancel();
        _gate.Dispose();
        _stopSource.Dispose();
        await _scope.DisposeAsync().ConfigureAwait(false);
    }

    private void AddActorPacketCore<THandler, TActor>(string? packetName)
        where THandler : class
        where TActor : IZLinkActor
    {
        EnsureConfigurationOpen();
        _actorHandlers.AddPacket(typeof(THandler), typeof(TActor), packetName);
    }

    private void EnsureConfigurationOpen()
    {
        if (!_configurationOpen)
        {
            throw new InvalidOperationException(
                "Entry Spot handler registration is only allowed while Configure is running.");
        }
    }

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
            await using var scope = _services.CreateAsyncScope();
            var invoker = new ZLinkSpotHandlerInvoker(scope.ServiceProvider, EntrySpot);
            await invoker.InvokeActorPacketAsync(
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
            await using var scope = _services.CreateAsyncScope();
            var invoker = new ZLinkSpotHandlerInvoker(scope.ServiceProvider, EntrySpot);
            return await invoker.InvokeActorPacketForReplyAsync(
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

    private sealed record ActorLifecycleState(
        ZLinkSpotActorLifecycleDescriptor Descriptor,
        IZLinkActor Actor,
        ZLinkSpotActorLifecycleInfo Info);
}
