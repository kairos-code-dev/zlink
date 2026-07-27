using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Eventing;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class RuntimeConcurrencyBoundaryTests
{
    [Fact]
    public async Task RuntimeEventHandlerFailure_DoesNotEscapeAfterRuntimeStops()
    {
        var services = new ServiceCollection();
        services.AddScoped<IZLinkRuntimeEventHandler<TestRuntimeEvent>, ThrowingRuntimeEventHandler>();
        services.AddZLinkFramework(_ => { });
        await using var provider = services.BuildServiceProvider();
        var runtime = provider.GetRequiredService<ZLinkFrameworkRuntime>();
        await runtime.StartAsync(CancellationToken.None);
        await runtime.StopAsync(CancellationToken.None);

        await provider.GetRequiredService<IZLinkRuntimeEventPublisher>()
            .PublishAsync(new TestRuntimeEvent(), CancellationToken.None);
    }

    [Fact]
    public async Task RuntimeEventHandlerFailure_DoesNotEscapeWhenRuntimeStopsDuringDispatch()
    {
        var services = new ServiceCollection();
        services.AddSingleton<BlockingRuntimeEventHandlerState>();
        services.AddScoped<IZLinkRuntimeEventHandler<TestRuntimeEvent>, BlockingThrowingRuntimeEventHandler>();
        services.AddZLinkFramework(_ => { });
        await using var provider = services.BuildServiceProvider();
        var runtime = provider.GetRequiredService<ZLinkFrameworkRuntime>();
        var state = provider.GetRequiredService<BlockingRuntimeEventHandlerState>();
        await runtime.StartAsync(CancellationToken.None);

        var publish = provider.GetRequiredService<IZLinkRuntimeEventPublisher>()
            .PublishAsync(new TestRuntimeEvent(), CancellationToken.None).AsTask();
        await state.Entered.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await runtime.StopAsync(CancellationToken.None);
        state.Release.TrySetResult();

        await publish.WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task BoundSessionDeferredScope_DrainsOperationAddedWhileAnotherOperationIsRunning()
    {
        var firstStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseFirst = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var order = new List<string>();
        await using var scope = ZLinkBoundSessionDispatchScope.Enter("actor-1");

        Assert.True(ZLinkBoundSessionDispatchScope.TryDefer(
            "actor-1",
            async _ =>
            {
                firstStarted.TrySetResult();
                await releaseFirst.Task;
                lock (order) order.Add("first");
            }));

        var drain = scope.DrainAsync(CancellationToken.None).AsTask();
        await firstStarted.Task;
        var added = await Task.Run(() => ZLinkBoundSessionDispatchScope.TryDefer(
            "actor-1",
            _ =>
            {
                lock (order) order.Add("second");
                return ValueTask.CompletedTask;
            }));
        Assert.True(added);

        releaseFirst.TrySetResult();
        await drain;
        Assert.Equal(["first", "second"], order);
    }

    [Fact]
    public async Task RuntimeTaskRunner_RejectsStoppingItselfInsteadOfDeadlocking()
    {
        using var shutdown = new CancellationTokenSource();
        var runner = new ZLinkRuntimeTaskRunner(
            new ZLinkRuntimeErrorSink(),
            shutdown.Token);
        var observed = new TaskCompletionSource<Exception>(
            TaskCreationOptions.RunContinuationsAsynchronously);

        runner.RunDetached(
            "self-stop",
            async _ =>
            {
                try
                {
                    await runner.StopAsync();
                }
                catch (Exception exception)
                {
                    observed.TrySetResult(exception);
                }
            });

        var exception = await observed.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.IsType<InvalidOperationException>(exception);
        shutdown.Cancel();
        await runner.StopAsync();
    }

    [Fact]
    public async Task RuntimeExecutionOwner_IsSharedAcrossNestedTaskRunners()
    {
        using var shutdown = new CancellationTokenSource();
        var owner = new object();
        var root = new ZLinkRuntimeTaskRunner(
            new ZLinkRuntimeErrorSink(),
            shutdown.Token,
            owner);
        var nested = new ZLinkRuntimeTaskRunner(
            new ZLinkRuntimeErrorSink(),
            shutdown.Token,
            owner);
        var observed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        nested.RunDetached(
            "nested",
            _ =>
            {
                Assert.True(root.IsCurrentExecution);
                Assert.True(ZLinkRuntimeTaskRunner.IsCurrentExecutionFor(owner));
                observed.TrySetResult();
                return ValueTask.CompletedTask;
            });

        await observed.Task.WaitAsync(TimeSpan.FromSeconds(5));
        shutdown.Cancel();
        await nested.StopAsync();
        await root.StopAsync();
    }

    [Fact]
    public async Task RuntimeTaskRunner_StopDrainsChildrenScheduledByAnAdmittedTask()
    {
        using var shutdown = new CancellationTokenSource();
        var runner = new ZLinkRuntimeTaskRunner(new ZLinkRuntimeErrorSink(), shutdown.Token);
        var parentStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var scheduleChild = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var childRan = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        runner.RunDetached(
            "parent",
            async _ =>
            {
                parentStarted.TrySetResult();
                await scheduleChild.Task;
                Assert.True(runner.TryRunDetached(
                    "child",
                    _ =>
                    {
                        childRan.TrySetResult();
                        return ValueTask.CompletedTask;
                    }));
            });

        await parentStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var stop = runner.StopAsync().AsTask();
        scheduleChild.TrySetResult();
        await childRan.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await stop.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.False(runner.TryRunDetached("external", _ => ValueTask.CompletedTask));
    }

    [Fact]
    public async Task RuntimeTaskSupervisor_DrainsChildRunnerAndItsLateRootCleanup()
    {
        using var shutdown = new CancellationTokenSource();
        var scope = new ZLinkRuntimeExecutionScope();
        var root = new ZLinkRuntimeTaskRunner(
            new ZLinkRuntimeErrorSink(),
            shutdown.Token,
            scope,
            ownsSupervisor: true);
        var child = new ZLinkRuntimeTaskRunner(
            new ZLinkRuntimeErrorSink(),
            shutdown.Token,
            scope);
        var childStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseChild = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var cleanupRan = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        child.RunDetached(
            "child",
            async _ =>
            {
                childStarted.TrySetResult();
                await releaseChild.Task;
                Assert.True(root.TryRunDetached(
                    "root-cleanup",
                    _ =>
                    {
                        cleanupRan.TrySetResult();
                        return ValueTask.CompletedTask;
                    }));
            });

        await childStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var stop = root.StopAsync().AsTask();
        releaseChild.TrySetResult();
        await cleanupRan.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await stop.WaitAsync(TimeSpan.FromSeconds(5));
        await child.StopAsync();
    }

    [Fact]
    public async Task BoundSessionDeferredScope_RetainsFailedHeadBeforeLaterOperations()
    {
        var attempts = 0;
        var order = new List<string>();
        await using var scope = ZLinkBoundSessionDispatchScope.Enter("actor-1");
        Assert.True(ZLinkBoundSessionDispatchScope.TryDefer(
            "actor-1",
            _ =>
            {
                attempts++;
                if (attempts == 1) throw new InvalidOperationException("transient");
                order.Add("first");
                return ValueTask.CompletedTask;
            }));
        Assert.True(ZLinkBoundSessionDispatchScope.TryDefer(
            "actor-1",
            _ =>
            {
                order.Add("second");
                return ValueTask.CompletedTask;
            }));

        await Assert.ThrowsAsync<InvalidOperationException>(() =>
            scope.DrainAsync(CancellationToken.None).AsTask());
        Assert.Empty(order);

        await scope.DrainAsync(CancellationToken.None);
        Assert.Equal(2, attempts);
        Assert.Equal(["first", "second"], order);
    }

    private sealed record TestRuntimeEvent : IZLinkRuntimeEvent
    {
        public string SourceName => "test";

        public DateTimeOffset Timestamp { get; } = DateTimeOffset.UtcNow;
    }

    private sealed class ThrowingRuntimeEventHandler : IZLinkRuntimeEventHandler<TestRuntimeEvent>
    {
        public ValueTask HandleAsync(TestRuntimeEvent @event, CancellationToken cancellationToken) =>
            ValueTask.FromException(new InvalidOperationException("handler failed"));
    }

    private sealed class BlockingRuntimeEventHandlerState
    {
        public TaskCompletionSource Entered { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource Release { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed class BlockingThrowingRuntimeEventHandler(BlockingRuntimeEventHandlerState state)
        : IZLinkRuntimeEventHandler<TestRuntimeEvent>
    {
        public async ValueTask HandleAsync(TestRuntimeEvent @event, CancellationToken cancellationToken)
        {
            state.Entered.TrySetResult();
            await state.Release.Task.WaitAsync(cancellationToken);
            throw new InvalidOperationException("handler failed after runtime stop");
        }
    }
}
