using System.Reflection;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class BackendActorHandleOwnershipTests
{
    private static readonly RoutingId NodeRid = RoutingId.From("ownership-node");

    [Fact]
    public async Task CreatedActorIsRetainedUntilExplicitDestroyAndClosedOnce()
    {
        var (node, proxy) = CreateSpotNode();
        var wrapper = new ZLinkBackendSpotNodeWrapper(node);
        var actorRef = new ActorRef(NodeRid, "retained", 1);
        var weakActor = CreateOwnedActor(wrapper, proxy, actorRef);

        ForceFullCollection();

        Assert.True(weakActor.TryGetTarget(out var actor));
        Assert.Equal(0, actor.CloseCount);

        var backendRef = actorRef.ToBackend();
        await wrapper.DestroyActorAsync(backendRef, TimeSpan.FromSeconds(1), default);
        Assert.Equal(1, actor.CloseCount);
        Assert.Empty(proxy.DelegatedDestroyRefs);

        await wrapper.DestroyActorAsync(backendRef, TimeSpan.FromSeconds(1), default);
        Assert.Equal(1, actor.CloseCount);
        Assert.Equal([actorRef], proxy.DelegatedDestroyRefs);
        await wrapper.DisposeAsync();
    }

    [Fact]
    public async Task UnknownActorDestroyUsesPublicSpotNodeOperation()
    {
        var (node, proxy) = CreateSpotNode();
        var wrapper = new ZLinkBackendSpotNodeWrapper(node);
        var actorRef = new ActorRef(NodeRid, "external", 9);
        var timeout = TimeSpan.FromMilliseconds(750);

        await wrapper.DestroyActorAsync(actorRef.ToBackend(), timeout, default);

        Assert.Equal([actorRef], proxy.DelegatedDestroyRefs);
        Assert.Equal(timeout, proxy.DestroyOperation.TimeoutValue);
        Assert.Equal(1, proxy.DestroyOperation.AsyncCount);
        await wrapper.DisposeAsync();
    }

    [Fact]
    public async Task DisposeReleasesAllActorsBeforeNodeAndAggregatesFailures()
    {
        var events = new List<string>();
        var (node, proxy) = CreateSpotNode(events);
        var wrapper = new ZLinkBackendSpotNodeWrapper(node);
        var firstFailure = new InvalidOperationException("first actor cleanup failed");
        var nodeFailure = new InvalidOperationException("node cleanup failed");
        var first = new TrackingActor(
            new ActorRef(NodeRid, "first", 1),
            "actor:first",
            events,
            disposeFailure: firstFailure);
        var second = new TrackingActor(
            new ActorRef(NodeRid, "second", 1),
            "actor:second",
            events);
        proxy.ActorsToCreate.Enqueue(first);
        proxy.ActorsToCreate.Enqueue(second);
        proxy.DisposeFailure = nodeFailure;

        using (var request = Message.From("first")) wrapper.CreateActor("first", request);
        using (var request = Message.From("second")) wrapper.CreateActor("second", request);

        var exception = await Assert.ThrowsAsync<AggregateException>(
            () => wrapper.DisposeAsync().AsTask());

        Assert.Equal(["actor:first", "actor:second", "node"], events);
        Assert.Contains(firstFailure, exception.InnerExceptions);
        Assert.Contains(nodeFailure, exception.InnerExceptions);
        Assert.Equal(1, first.DisposeCount);
        Assert.Equal(1, second.DisposeCount);
        Assert.Equal(1, proxy.DisposeCount);

        var repeated = await Assert.ThrowsAsync<AggregateException>(
            () => wrapper.DisposeAsync().AsTask());
        Assert.Same(exception, repeated);
        Assert.Equal(1, proxy.DisposeCount);
    }

    [Fact]
    public async Task DuplicateCreatedReferenceCleansUpOnlyTheRejectedHandle()
    {
        var (node, proxy) = CreateSpotNode();
        var wrapper = new ZLinkBackendSpotNodeWrapper(node);
        var actorRef = new ActorRef(NodeRid, "duplicate", 3);
        var retained = new TrackingActor(actorRef);
        var rejected = new TrackingActor(actorRef);
        proxy.ActorsToCreate.Enqueue(retained);
        proxy.ActorsToCreate.Enqueue(rejected);

        using (var request = Message.From("first")) wrapper.CreateActor("duplicate", request);
        using (var request = Message.From("second"))
            Assert.Throws<InvalidOperationException>(() => wrapper.CreateActor("duplicate", request));

        Assert.Equal(0, retained.DisposeCount);
        Assert.Equal(1, rejected.DisposeCount);

        await wrapper.DestroyActorAsync(actorRef.ToBackend(), TimeSpan.Zero, default);
        Assert.Equal(1, retained.CloseCount);
        await wrapper.DisposeAsync();
    }

    private static WeakReference<TrackingActor> CreateOwnedActor(
        ZLinkBackendSpotNodeWrapper wrapper,
        SpotNodeProxy proxy,
        ActorRef actorRef)
    {
        var actor = new TrackingActor(actorRef);
        proxy.ActorsToCreate.Enqueue(actor);
        using var request = Message.From("create");
        wrapper.CreateActor(actorRef.ActorId, request);
        return new WeakReference<TrackingActor>(actor);
    }

    private static void ForceFullCollection()
    {
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
    }

    private static (ISpotNode Node, SpotNodeProxy Proxy) CreateSpotNode(
        List<string>? events = null)
    {
        var node = DispatchProxy.Create<ISpotNode, SpotNodeProxy>();
        var proxy = (SpotNodeProxy)(object)node;
        proxy.RoutingId = NodeRid;
        proxy.Events = events;
        return (node, proxy);
    }

    private class SpotNodeProxy : DispatchProxy
    {
        public RoutingId RoutingId { get; set; }
        public Queue<IActor> ActorsToCreate { get; } = new();
        public List<ActorRef> DelegatedDestroyRefs { get; } = [];
        public TrackingActorDestroyOperation DestroyOperation { get; } = new();
        public List<string>? Events { get; set; }
        public Exception? DisposeFailure { get; set; }
        public int DisposeCount { get; private set; }

        protected override object? Invoke(MethodInfo? targetMethod, object?[]? args)
        {
            ArgumentNullException.ThrowIfNull(targetMethod);
            args ??= [];
            switch (targetMethod.Name)
            {
                case "get_RoutingId":
                    return RoutingId;
                case "CreateActor":
                    return ActorsToCreate.Dequeue();
                case "DestroyActor":
                    DelegatedDestroyRefs.Add((ActorRef)args[0]!);
                    return DestroyOperation;
                case "DisposeAsync":
                    DisposeCount++;
                    Events?.Add("node");
                    return DisposeFailure is null
                        ? ValueTask.CompletedTask
                        : new ValueTask(Task.FromException(DisposeFailure));
                default:
                    throw new NotSupportedException(targetMethod.Name);
            }
        }
    }

    private sealed class TrackingActorDestroyOperation : ActorDestroyOperation
    {
        public TimeSpan TimeoutValue { get; private set; }
        public int AsyncCount { get; private set; }

        public ActorDestroyOperation Timeout(TimeSpan timeout)
        {
            TimeoutValue = timeout;
            return this;
        }

        public Task<IReadOnlyList<Message>> Async(CancellationToken ct = default)
        {
            ct.ThrowIfCancellationRequested();
            AsyncCount++;
            return Task.FromResult<IReadOnlyList<Message>>([]);
        }

        public bool Submit(ReplyHandler callback) => throw new NotSupportedException();
    }

    private sealed class TrackingActor(
        ActorRef actorRef,
        string? disposeEvent = null,
        List<string>? events = null,
        Exception? disposeFailure = null) : IActor
    {
        public ActorRef Ref => actorRef;
        public int CloseCount { get; private set; }
        public int DisposeCount { get; private set; }

        public ActorJoinOperation Join(ISpot spot) => throw new NotSupportedException();
        public ActorLeaveOperation Leave(ISpot spot) => throw new NotSupportedException();
        public ActorReceived? Recv(RecvFlags flags = RecvFlags.None) =>
            throw new NotSupportedException();
        public SendOperation SendBoundSession() => throw new NotSupportedException();
        public void CloseBoundSession(TimeSpan timeout = default) =>
            throw new NotSupportedException();

        public void Close(TimeSpan timeout = default)
        {
            CloseCount++;
        }

        public void Dispose()
        {
            DisposeAsync().AsTask().GetAwaiter().GetResult();
        }

        public ValueTask DisposeAsync()
        {
            DisposeCount++;
            if (disposeEvent is not null) events?.Add(disposeEvent);
            return disposeFailure is null
                ? ValueTask.CompletedTask
                : new ValueTask(Task.FromException(disposeFailure));
        }
    }
}
