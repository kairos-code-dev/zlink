using Zlink.Framework.Runtime.Timers;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotTimerRegistry(Func<bool> flowGenerationEnabled) : IAsyncDisposable
{
    private readonly List<IZLinkTimer> _timers = [];

    public async ValueTask DisposeAsync()
    {
        foreach (var timer in _timers) await timer.DisposeAsync().ConfigureAwait(false);
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
        cancellationToken.ThrowIfCancellationRequested();

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
                flowGenerationEnabled(),
                ZLinkFlowOrigin.Timer));
        _timers.Add(timer);
        return ValueTask.FromResult<IZLinkTimer>(timer);
    }
}
