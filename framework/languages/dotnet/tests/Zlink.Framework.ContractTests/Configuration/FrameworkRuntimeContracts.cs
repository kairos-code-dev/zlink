using System.Runtime.CompilerServices;
using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Configuration;

/// <summary>
///     Worked example for the host maintenance singleton. Per
///     10-topology-monitoring §6, <see cref="IZLinkFrameworkRuntime" /> owns
///     host state, readiness and the two terminal intents; it takes no
///     MeshName or ChannelName because every topology is retired or shut down
///     together.
/// </summary>
public sealed class FrameworkRuntimeContracts
{
    [Fact]
    [ContractExample(typeof(IZLinkFrameworkRuntime))]
    public async Task Host_runtime_retires_with_continuity_and_reports_one_terminal_result()
    {
        // A rolling deployment drains this pod: Retire moves the objects to a
        // peer node so sessions keep their logical continuity, unlike Shutdown
        // which only tears the host down.
        var runtime = new ExampleFrameworkRuntime();

        Assert.Equal(ZLinkFrameworkRuntimeState.Serving, runtime.State);
        Assert.True(runtime.IsReady);

        var serving = runtime.Snapshot();
        Assert.Null(serving.EffectiveIntent);
        Assert.False(serving.WorkSealed);
        Assert.Equal(12UL, serving.PendingRequestCount);

        var observed = new List<ZLinkFrameworkRuntimeEvent>();
        var observer = Task.Run(async () =>
        {
            await foreach (var runtimeEvent in runtime.ObserveAsync(capacity: 64))
                observed.Add(runtimeEvent);
        });

        var result = await runtime.RetireAsync(TimeSpan.FromSeconds(45));
        Assert.Equal(ZLinkFrameworkTerminationIntent.Retire, result.EffectiveIntent);
        Assert.Equal(ZLinkFrameworkTerminationOutcome.Stopped, result.Outcome);
        Assert.Equal(ZLinkFrameworkTerminationReason.None, result.Reason);

        // A Shutdown that arrives after the host already moved to Draining
        // joins the running operation instead of starting a second one: the
        // effective intent and the deadline were fixed by the first caller.
        var joined = await runtime.ShutdownAsync();
        Assert.Equal(result, joined);

        await observer;
        Assert.Equal(
            [ZLinkFrameworkRuntimeState.Retiring, ZLinkFrameworkRuntimeState.Draining,
                ZLinkFrameworkRuntimeState.Stopped],
            observed.Select(runtimeEvent => runtimeEvent.State));
        Assert.Equal(
            [1UL, 2UL, 3UL],
            observed.Select(runtimeEvent => runtimeEvent.Sequence));
        Assert.All(
            observed,
            runtimeEvent => Assert.Equal(
                ZLinkFrameworkTerminationIntent.Retire,
                runtimeEvent.EffectiveIntent));

        var stopped = runtime.Snapshot();
        Assert.Equal(ZLinkFrameworkRuntimeState.Stopped, stopped.State);
        Assert.True(stopped.WorkSealed);
        Assert.Equal(0UL, stopped.PendingRequestCount);
        Assert.Equal(0UL, stopped.PendingRelocationCount);
        Assert.Equal(result, stopped.TerminalResult);
        Assert.False(runtime.IsReady);
    }

    [Fact]
    [ContractExample(typeof(IZLinkFrameworkRuntime))]
    public async Task Retire_before_readiness_is_blocked_while_shutdown_still_cleans_up()
    {
        // Retire needs somewhere to hand the objects to, so a host that never
        // reached Serving reports Blocked/RuntimeNotReady and leaves admission
        // untouched. Shutdown has no such precondition.
        var runtime = new ExampleFrameworkRuntime(ZLinkFrameworkRuntimeState.Preparing);

        var blocked = await runtime.RetireAsync();
        Assert.Equal(ZLinkFrameworkTerminationOutcome.Blocked, blocked.Outcome);
        Assert.Equal(ZLinkFrameworkTerminationReason.RuntimeNotReady, blocked.Reason);
        Assert.Equal(ZLinkFrameworkRuntimeState.Preparing, runtime.State);

        // Blocked is shared with the concurrent preflight waiters only; it is
        // never stored as the host's terminal result.
        Assert.Null(runtime.Snapshot().TerminalResult);

        var stopped = await runtime.ShutdownAsync(TimeSpan.FromSeconds(10));
        Assert.Equal(ZLinkFrameworkTerminationIntent.Shutdown, stopped.EffectiveIntent);
        Assert.Equal(ZLinkFrameworkTerminationOutcome.Stopped, stopped.Outcome);
        Assert.Equal(ZLinkFrameworkRuntimeState.Stopped, runtime.State);
        Assert.Equal(stopped, runtime.Snapshot().TerminalResult);
    }

    private sealed class ExampleFrameworkRuntime(
        ZLinkFrameworkRuntimeState initialState = ZLinkFrameworkRuntimeState.Serving)
        : IZLinkFrameworkRuntime
    {
        private static readonly DateTimeOffset ObservedAt =
            new(2026, 7, 25, 9, 30, 0, TimeSpan.Zero);

        private readonly List<ZLinkFrameworkRuntimeEvent> _events = [];
        private ZLinkFrameworkTerminationIntent? _effectiveIntent;
        private ZLinkFrameworkTerminationResult? _terminalResult;
        private DateTimeOffset? _deadline;
        private ulong _pendingRequests = 12;
        private ulong _pendingRelocations = 2;
        private ulong _sequence;

        public ZLinkFrameworkRuntimeState State { get; private set; } = initialState;

        public bool IsReady => State == ZLinkFrameworkRuntimeState.Serving;

        public ZLinkFrameworkRuntimeSnapshot Snapshot() => new(
            State,
            _effectiveIntent,
            _deadline,
            WorkSealed: State is ZLinkFrameworkRuntimeState.Draining
                or ZLinkFrameworkRuntimeState.Stopped,
            BlockerReason: null,
            _pendingRequests,
            _pendingRelocations,
            PendingStreamBarrierCount: 0,
            _terminalResult,
            _sequence,
            ObservedAt);

        public async IAsyncEnumerable<ZLinkFrameworkRuntimeEvent> ObserveAsync(
            int capacity = 1024,
            [EnumeratorCancellation] CancellationToken cancellationToken = default)
        {
            ArgumentOutOfRangeException.ThrowIfLessThan(capacity, 1);
            var index = 0;
            while (State != ZLinkFrameworkRuntimeState.Stopped || index < _events.Count)
            {
                if (index == _events.Count)
                {
                    await Task.Yield();
                    continue;
                }

                yield return _events[index++];
            }
        }

        public ValueTask<ZLinkFrameworkTerminationResult> RetireAsync(
            TimeSpan? deadline = null,
            CancellationToken cancellationToken = default) =>
            TerminateAsync(ZLinkFrameworkTerminationIntent.Retire, deadline);

        public ValueTask<ZLinkFrameworkTerminationResult> ShutdownAsync(
            TimeSpan? deadline = null,
            CancellationToken cancellationToken = default) =>
            TerminateAsync(ZLinkFrameworkTerminationIntent.Shutdown, deadline);

        private ValueTask<ZLinkFrameworkTerminationResult> TerminateAsync(
            ZLinkFrameworkTerminationIntent intent,
            TimeSpan? deadline)
        {
            // Once a terminal result exists, every later caller joins it
            // regardless of the intent it asked for.
            if (_terminalResult is { } terminal)
                return ValueTask.FromResult(terminal);

            if (intent == ZLinkFrameworkTerminationIntent.Retire
                && State is ZLinkFrameworkRuntimeState.Preparing or ZLinkFrameworkRuntimeState.Error)
            {
                return ValueTask.FromResult(new ZLinkFrameworkTerminationResult(
                    intent,
                    ZLinkFrameworkTerminationOutcome.Blocked,
                    ZLinkFrameworkTerminationReason.RuntimeNotReady));
            }

            // deadline == null means 30 seconds.
            _deadline = ObservedAt + (deadline ?? TimeSpan.FromSeconds(30));
            _effectiveIntent = intent;

            if (intent == ZLinkFrameworkTerminationIntent.Retire)
                Publish("zlink.runtime.framework.retiring", ZLinkFrameworkRuntimeState.Retiring);

            Publish("zlink.runtime.framework.draining", ZLinkFrameworkRuntimeState.Draining);
            _pendingRequests = 0;
            _pendingRelocations = 0;

            var result = new ZLinkFrameworkTerminationResult(
                intent,
                ZLinkFrameworkTerminationOutcome.Stopped,
                ZLinkFrameworkTerminationReason.None);
            _terminalResult = result;
            Publish("zlink.runtime.framework.stopped", ZLinkFrameworkRuntimeState.Stopped, result);
            return ValueTask.FromResult(result);
        }

        private void Publish(
            string identifier,
            ZLinkFrameworkRuntimeState state,
            ZLinkFrameworkTerminationResult? result = null)
        {
            State = state;
            _events.Add(new ZLinkFrameworkRuntimeEvent(
                identifier,
                ++_sequence,
                ObservedAt,
                state,
                _effectiveIntent,
                result?.Outcome,
                result?.Reason));
        }
    }
}
