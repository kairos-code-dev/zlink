using Microsoft.Extensions.DependencyInjection;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Runtime.Protocol;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class ActorHandlerActivationTests
{
    [Fact]
    public async Task Actor_Activations_Own_Separate_Handler_Instances_And_Scoped_Dependencies()
    {
        var probe = new LifetimeProbe();
        var registered = new ProbeHandler(new ScopedDependency(probe), probe);
        await using var services = new ServiceCollection()
            .AddSingleton(probe)
            .AddScoped<ScopedDependency>()
            .AddSingleton(registered)
            .BuildServiceProvider();
        var firstState = new ZLinkActorRuntimeState("actor-1", services: services);
        var secondState = new ZLinkActorRuntimeState("actor-2", services: services);

        var first = firstState.HandlerInstances.Resolve<ProbeHandler>();
        var firstAgain = firstState.HandlerInstances.Resolve<ProbeHandler>();
        var second = secondState.HandlerInstances.Resolve<ProbeHandler>();

        Assert.Same(first, firstAgain);
        Assert.NotSame(registered, first);
        Assert.NotSame(first, second);
        Assert.NotSame(first.Dependency, second.Dependency);

        await firstState.DisposeHandlerActivationAsync();
        await firstState.DisposeHandlerActivationAsync();
        Assert.Equal(1, first.DisposeCount);
        Assert.Equal(1, first.Dependency.DisposeCount);
        Assert.Equal(0, second.DisposeCount);

        await secondState.DisposeHandlerActivationAsync();
        Assert.Equal(1, second.DisposeCount);
        Assert.Equal(1, second.Dependency.DisposeCount);
    }

    [Fact]
    public async Task Runtime_Generation_Reset_Disposes_Each_Actor_Activation_Once()
    {
        var probe = new LifetimeProbe();
        await using var services = new ServiceCollection()
            .AddSingleton(probe)
            .AddScoped<ScopedDependency>()
            .BuildServiceProvider();
        var registry = new ZLinkActorSessionRegistry(services);
        var first = registry.GetOrCreate("actor-1")
            .HandlerInstances.Resolve<ProbeHandler>();
        var second = registry.GetOrCreate("actor-2")
            .HandlerInstances.Resolve<ProbeHandler>();

        await registry.ResetGenerationAsync();
        await registry.ResetGenerationAsync();

        Assert.Equal(1, first.DisposeCount);
        Assert.Equal(1, second.DisposeCount);
        Assert.Equal(2, probe.DisposedDependencies);
    }

    [Fact]
    public async Task Terminal_Cleanup_Waits_For_InFlight_Handler_And_Rejects_Recreation()
    {
        var probe = new LifetimeProbe();
        await using var services = new ServiceCollection()
            .AddSingleton(probe)
            .AddScoped<ScopedDependency>()
            .BuildServiceProvider();
        var state = new ZLinkActorRuntimeState("actor-race", services: services);
        var handler = state.HandlerInstances.Resolve<BlockingHandler>();
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.None,
            null,
            "blocking-handler",
            ZlinkStreamMetadata.Empty);

        var dispatch = state.ExecuteDispatchAsync(
                header,
                handler.HandleAsync,
                CancellationToken.None)
            .AsTask();
        await handler.Started.Task.WaitAsync(TimeSpan.FromSeconds(5));

        state.BeginTeardown();
        var cleanup = state.BeginHandlerActivationCompletion(
                () =>
                {
                    state.ClearAfterDestroy();
                    return true;
                })
            .Completion;
        await Task.Delay(50);

        Assert.False(cleanup.IsCompleted);
        Assert.Throws<InvalidOperationException>(() => state.HandlerInstances);
        Assert.Equal(0, handler.DisposeCount);
        Assert.Equal(0, handler.Dependency.DisposeCount);

        handler.Release.TrySetResult();
        await dispatch.WaitAsync(TimeSpan.FromSeconds(5));
        await cleanup.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(1, handler.DisposeCount);
        Assert.Equal(1, handler.Dependency.DisposeCount);
        Assert.Throws<InvalidOperationException>(() => state.HandlerInstances);
    }

    [Fact]
    public async Task Terminal_Barrier_Runs_After_Already_Accepted_Turns_And_Closes_Admission()
    {
        var mailbox = new ZLinkActorDispatchMailbox();
        var current = await mailbox.EnterAsync(CancellationToken.None);
        var accepted = mailbox.EnterAsync(CancellationToken.None).AsTask();
        var terminal = mailbox.CloseAdmissionAndReserveLifecycleBarrier();

        await Assert.ThrowsAsync<ZLinkFrameworkException>(
            () => mailbox.EnterAsync(CancellationToken.None).AsTask());

        current.Dispose();
        var acceptedTurn = await accepted.WaitAsync(TimeSpan.FromSeconds(5));
        var terminalTurn = terminal.ClaimAsync().AsTask();

        Assert.False(terminalTurn.IsCompleted);

        acceptedTurn.Dispose();
        using var claimed = await terminalTurn.WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task Self_Initiated_Teardown_Is_Completed_After_Current_Dispatch_Returns()
    {
        var probe = new LifetimeProbe();
        await using var services = new ServiceCollection()
            .AddSingleton(probe)
            .AddScoped<ScopedDependency>()
            .BuildServiceProvider();
        var state = new ZLinkActorRuntimeState("actor-self", services: services);
        var handler = state.HandlerInstances.Resolve<ProbeHandler>();
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.None,
            null,
            "self-teardown",
            ZlinkStreamMetadata.Empty);
        Task<bool>? terminalTask = null;

        await state.ExecuteDispatchAsync(
            header,
            _ =>
            {
                state.BeginTeardown();
                var terminal = state.BeginHandlerActivationCompletion(
                    () =>
                    {
                        state.ClearAfterDestroy();
                        return true;
                    });
                Assert.True(terminal.RequiresDispatchRelease);
                Assert.False(terminal.Completion.IsCompleted);
                terminalTask = terminal.Completion;
                return ValueTask.CompletedTask;
            },
            CancellationToken.None);

        Assert.NotNull(terminalTask);
        Assert.True(
            await terminalTask.WaitAsync(TimeSpan.FromSeconds(5)));
        Assert.Equal(1, handler.DisposeCount);
        Assert.Equal(1, handler.Dependency.DisposeCount);
        Assert.Throws<InvalidOperationException>(() => state.HandlerInstances);
    }

    private sealed class LifetimeProbe
    {
        public int DisposedDependencies;
    }

    private sealed class ScopedDependency(LifetimeProbe probe) : IAsyncDisposable
    {
        public int DisposeCount { get; private set; }

        public ValueTask DisposeAsync()
        {
            DisposeCount++;
            Interlocked.Increment(ref probe.DisposedDependencies);
            return ValueTask.CompletedTask;
        }
    }

    private sealed class ProbeHandler(
        ScopedDependency dependency,
        LifetimeProbe probe) : IAsyncDisposable
    {
        public ScopedDependency Dependency { get; } = dependency;

        public int DisposeCount { get; private set; }

        public ValueTask DisposeAsync()
        {
            _ = probe;
            DisposeCount++;
            return ValueTask.CompletedTask;
        }
    }

    private sealed class BlockingHandler(ScopedDependency dependency)
        : IAsyncDisposable
    {
        public ScopedDependency Dependency { get; } = dependency;

        public TaskCompletionSource Started { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource Release { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public int DisposeCount { get; private set; }

        public async ValueTask HandleAsync(CancellationToken cancellationToken)
        {
            Started.TrySetResult();
            await Release.Task.WaitAsync(cancellationToken);
        }

        public ValueTask DisposeAsync()
        {
            DisposeCount++;
            return ValueTask.CompletedTask;
        }
    }
}
