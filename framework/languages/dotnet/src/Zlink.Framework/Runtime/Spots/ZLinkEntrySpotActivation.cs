using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkEntrySpotActivation : IZLinkEntrySpotContext, IAsyncDisposable
{
    private static readonly AsyncLocal<ZLinkEntrySpotActivation?> Current = new();
    private readonly AsyncServiceScope _scope;
    private readonly ZLinkSpotActorHandlerRegistry _actorHandlers =
        new(ZLinkSpotActorHandlerSurface.EntrySpot);
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly CancellationTokenSource _stopSource = new();
    private readonly ZLinkSpotHandlerInvoker _invoker;
    private readonly ZLinkEntrySpotHandlerExecutor _handlerExecutor;
    private bool _configurationOpen = true;
    private int _disposed;

    public ZLinkEntrySpotActivation(
        IServiceProvider services,
        Type entrySpotType,
        RoutingId nodeRid)
    {
        NodeRid = nodeRid;
        _scope = services.CreateAsyncScope();
        EntrySpot = (IZLinkEntrySpot)ActivatorUtilities.CreateInstance(
            _scope.ServiceProvider,
            entrySpotType,
            this);
        if (!ReferenceEquals(EntrySpot.Context, this))
        {
            throw new InvalidOperationException(
                $"Entry SPOT '{entrySpotType.FullName}' must expose the context provided by the runtime.");
        }

        _invoker = new ZLinkSpotHandlerInvoker(_scope.ServiceProvider, EntrySpot);
        _handlerExecutor = new ZLinkEntrySpotHandlerExecutor(services, EntrySpot);
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

    private sealed record ActorLifecycleState(
        ZLinkSpotActorLifecycleDescriptor Descriptor,
        IZLinkActor Actor,
        ZLinkSpotActorLifecycleInfo Info);
}
