using Zlink.Framework.Runtime.Timers;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotTimerRegistry(Func<bool> flowCaptureEnabled) : IAsyncDisposable
{
    private readonly object _lifecycleGate = new();
    private readonly List<IZLinkTimer> _timers = [];
    private Task? _finalization;
    private bool _closed;

    public ValueTask DisposeAsync()
    {
        TaskCompletionSource completion;
        IZLinkTimer[] timers;
        lock (_lifecycleGate)
        {
            if (_finalization is not null) return new ValueTask(_finalization);

            _closed = true;
            timers = _timers.ToArray();
            _timers.Clear();
            completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            _finalization = completion.Task;
        }

        _ = CompleteFinalizationAsync(timers, completion);
        return new ValueTask(completion.Task);
    }

    public ValueTask<IZLinkTimer> AddAsync(
        string name,
        TimeSpan period,
        ZLinkTimerOptions? options,
        Type handlerType,
        Type spotType,
        CancellationToken stopToken,
        Func<ZLinkSpotTimerDescriptor, ZLinkTimerTick, CancellationToken, ValueTask> dispatchAsync,
        Func<ZLinkSpotTimerDescriptor, ZLinkTimerTick, Exception, bool, CancellationToken, ValueTask>
            reportFailureAsync,
        CancellationToken cancellationToken)
    {
        lock (_lifecycleGate)
        {
            ObjectDisposedException.ThrowIf(_closed, this);
            cancellationToken.ThrowIfCancellationRequested();

            var timerOptions = ValidateRegistration(name, period, options);

            var descriptor = ZLinkSpotDescriptorFactory.CreateTimerDescriptor(name, period, handlerType, spotType);
            var timer = new ZLinkTimer(
                name,
                period,
                timerOptions,
                stopToken,
                (tick, ct) => dispatchAsync(descriptor, tick, ct),
                (tick, error, stopped, ct) =>
                    reportFailureAsync(descriptor, tick, error, stopped, ct),
                () => ZLinkFlowContext.Enter(
                    null,
                    null,
                    flowCaptureEnabled(),
                    ZLinkFlowOrigin.Timer));
            _timers.Add(timer);
            return ValueTask.FromResult<IZLinkTimer>(timer);
        }
    }

    internal static ZLinkTimerOptions ValidateRegistration(
        string name,
        TimeSpan period,
        ZLinkTimerOptions? options)
    {
        if (string.IsNullOrWhiteSpace(name))
            throw new ZLinkConfigurationException("SPOT timer name must not be empty.");

        if (period <= TimeSpan.Zero)
            throw new ZLinkConfigurationException("SPOT timer period must be greater than zero.");

        var timerOptions = options ?? new ZLinkTimerOptions();
        if (!Enum.IsDefined(timerOptions.OverrunPolicy))
            throw new ZLinkConfigurationException("SPOT timer overrun policy is not supported.");

        if (timerOptions.OverrunPolicy == ZLinkTimerOverrunPolicy.CatchUpBounded
            && timerOptions.MaxCatchUpTicks <= 0)
            throw new ZLinkConfigurationException("SPOT timer MaxCatchUpTicks must be greater than zero.");

        return timerOptions;
    }

    private static async Task DisposeTimersAsync(IReadOnlyList<IZLinkTimer> timers)
    {
        List<Exception>? failures = null;
        foreach (var timer in timers)
        {
            try
            {
                await timer.DisposeAsync().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                (failures ??= []).Add(exception);
            }
        }

        if (failures is [var failure])
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failure).Throw();
        if (failures is { Count: > 1 }) throw new AggregateException(failures);
    }

    private static async Task CompleteFinalizationAsync(
        IReadOnlyList<IZLinkTimer> timers,
        TaskCompletionSource completion)
    {
        try
        {
            await DisposeTimersAsync(timers).ConfigureAwait(false);
            completion.TrySetResult();
        }
        catch (Exception exception)
        {
            completion.TrySetException(exception);
        }
    }
}
