using System.Diagnostics;

namespace Zlink.Framework.Runtime.Timers;

internal sealed class ZLinkTimer : IZLinkTimer
{
    private readonly CancellationTokenSource _stopSource;
    private readonly Task _pump;
    private int _disposed;

    public ZLinkTimer(
        string name,
        TimeSpan period,
        ZLinkTimerOptions options,
        CancellationToken spotStopToken,
        Func<ZLinkTimerTick, CancellationToken, ValueTask> onTickAsync,
        Func<ZLinkTimerTick, Exception, bool, CancellationToken, ValueTask> onUnhandledExceptionAsync)
    {
        _stopSource = CancellationTokenSource.CreateLinkedTokenSource(spotStopToken);
        _pump = RunAsync(
            name,
            period,
            new ZLinkTimerCallbacks(options, onTickAsync, onUnhandledExceptionAsync),
            _stopSource.Token);
    }

    public bool IsDisposed => Volatile.Read(ref _disposed) != 0;

    public async ValueTask CancelAsync(CancellationToken cancellationToken = default)
    {
        _ = cancellationToken;

        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return;
        }

        _stopSource.Cancel();
        try
        {
            await _pump.ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (_stopSource.IsCancellationRequested)
        {
        }
        finally
        {
            _stopSource.Dispose();
        }
    }

    public ValueTask DisposeAsync()
    {
        return CancelAsync();
    }

    private static Task RunAsync(
        string name,
        TimeSpan period,
        ZLinkTimerCallbacks callbacks,
        CancellationToken cancellationToken)
    {
        return callbacks.Options.OverrunPolicy == ZLinkTimerOverrunPolicy.DelayNextTick
            ? RunDelayNextTickAsync(
                name,
                period,
                callbacks,
                cancellationToken)
            : RunFixedRateAsync(
                name,
                period,
                callbacks,
                cancellationToken);
    }

    private static async Task RunDelayNextTickAsync(
        string name,
        TimeSpan period,
        ZLinkTimerCallbacks callbacks,
        CancellationToken cancellationToken)
    {
        var clock = ZLinkTimerClock.Start();
        ulong deliveryIndex = 0;

        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            await Task.Delay(period, cancellationToken).ConfigureAwait(false);

            deliveryIndex++;
            var started = clock.Elapsed();
            var tick = clock.CreateTick(
                name,
                deliveryIndex,
                deliveryIndex,
                period,
                started,
                started,
                skippedTicks: 0);

            if (!await callbacks.DispatchTickAsync(tick, cancellationToken).ConfigureAwait(false))
            {
                return;
            }
        }
    }

    private static async Task RunFixedRateAsync(
        string name,
        TimeSpan period,
        ZLinkTimerCallbacks callbacks,
        CancellationToken cancellationToken)
    {
        var clock = ZLinkTimerClock.Start();
        var periodStopwatchTicks = StopwatchTicksFromTimeSpan(period);
        ulong lastScheduledIndex = 0;
        ulong deliveryIndex = 0;

        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var nextScheduledIndex = lastScheduledIndex + 1;
            await clock.DelayUntilAsync(nextScheduledIndex, periodStopwatchTicks, cancellationToken)
                .ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();

            var startedStopwatchTicks = clock.ElapsedStopwatchTicks();
            var dueScheduledIndex = Math.Max(
                nextScheduledIndex,
                ScheduledIndexAt(startedStopwatchTicks, periodStopwatchTicks));

            var scheduledIndex = SelectScheduledIndex(
                callbacks.Options,
                lastScheduledIndex,
                dueScheduledIndex);
            var skippedTicks = scheduledIndex - lastScheduledIndex - 1;
            deliveryIndex++;

            var scheduledElapsed = clock.ElapsedForScheduledIndex(
                scheduledIndex,
                periodStopwatchTicks);
            var startedElapsed = ZLinkTimerClock.ToElapsed(startedStopwatchTicks);
            var tick = clock.CreateTick(
                name,
                deliveryIndex,
                scheduledIndex,
                period,
                scheduledElapsed,
                startedElapsed,
                skippedTicks);

            if (!await callbacks.DispatchTickAsync(tick, cancellationToken).ConfigureAwait(false))
            {
                return;
            }

            lastScheduledIndex = scheduledIndex;
        }
    }

    private static ulong SelectScheduledIndex(
        ZLinkTimerOptions options,
        ulong lastScheduledIndex,
        ulong dueScheduledIndex)
    {
        if (options.OverrunPolicy == ZLinkTimerOverrunPolicy.SkipLateTicks)
        {
            return dueScheduledIndex;
        }

        var availableTicks = dueScheduledIndex - lastScheduledIndex;
        var maxCatchUpTicks = (ulong)options.MaxCatchUpTicks;
        if (availableTicks > maxCatchUpTicks)
        {
            return dueScheduledIndex - maxCatchUpTicks + 1;
        }

        return lastScheduledIndex + 1;
    }

    private static ulong ScheduledIndexAt(
        long elapsedStopwatchTicks,
        long periodStopwatchTicks)
    {
        if (elapsedStopwatchTicks <= 0)
        {
            return 1;
        }

        return Math.Max(1, (ulong)(elapsedStopwatchTicks / periodStopwatchTicks));
    }

    private static long StopwatchTicksFromTimeSpan(TimeSpan period)
    {
        return Math.Max(1, (long)Math.Round(period.TotalSeconds * Stopwatch.Frequency));
    }

    private readonly struct ZLinkTimerCallbacks(
        ZLinkTimerOptions options,
        Func<ZLinkTimerTick, CancellationToken, ValueTask> onTickAsync,
        Func<ZLinkTimerTick, Exception, bool, CancellationToken, ValueTask> onUnhandledExceptionAsync)
    {
        public ZLinkTimerOptions Options => options;

        public async ValueTask<bool> DispatchTickAsync(
            ZLinkTimerTick tick,
            CancellationToken cancellationToken)
        {
            try
            {
                await onTickAsync(tick, cancellationToken).ConfigureAwait(false);
                return true;
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                throw;
            }
            catch (Exception ex)
            {
                var stopped = options.StopOnUnhandledException;
                try
                {
                    await onUnhandledExceptionAsync(tick, ex, stopped, cancellationToken)
                        .ConfigureAwait(false);
                }
                catch
                {
                    // Monitoring failures must not change the timer exception policy.
                }

                return !stopped;
            }
        }
    }

    private readonly struct ZLinkTimerClock
    {
        private readonly long _startedTimestamp;
        private readonly DateTimeOffset _startedAt;

        private ZLinkTimerClock(long startedTimestamp, DateTimeOffset startedAt)
        {
            _startedTimestamp = startedTimestamp;
            _startedAt = startedAt;
        }

        public static ZLinkTimerClock Start()
        {
            return new ZLinkTimerClock(Stopwatch.GetTimestamp(), DateTimeOffset.UtcNow);
        }

        public long ElapsedStopwatchTicks()
        {
            return Stopwatch.GetTimestamp() - _startedTimestamp;
        }

        public TimeSpan Elapsed()
        {
            return ToElapsed(ElapsedStopwatchTicks());
        }

        public TimeSpan ElapsedForScheduledIndex(
            ulong scheduledIndex,
            long periodStopwatchTicks)
        {
            var scheduledStopwatchTicks = (long)Math.Min(
                long.MaxValue,
                scheduledIndex * (double)periodStopwatchTicks);
            return ToElapsed(scheduledStopwatchTicks);
        }

        public async ValueTask DelayUntilAsync(
            ulong scheduledIndex,
            long periodStopwatchTicks,
            CancellationToken cancellationToken)
        {
            var scheduledStopwatchTicks = (long)Math.Min(
                long.MaxValue,
                scheduledIndex * (double)periodStopwatchTicks);
            while (true)
            {
                var remainingStopwatchTicks = scheduledStopwatchTicks - ElapsedStopwatchTicks();
                if (remainingStopwatchTicks <= 0)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    return;
                }

                await Task.Delay(ToElapsed(remainingStopwatchTicks), cancellationToken)
                    .ConfigureAwait(false);
            }
        }

        public ZLinkTimerTick CreateTick(
            string name,
            ulong deliveryIndex,
            ulong scheduledIndex,
            TimeSpan period,
            TimeSpan scheduledElapsed,
            TimeSpan startedElapsed,
            ulong skippedTicks)
        {
            return new ZLinkTimerTick(
                name,
                deliveryIndex,
                scheduledIndex,
                period,
                _startedAt + scheduledElapsed,
                _startedAt + startedElapsed,
                scheduledElapsed,
                startedElapsed,
                startedElapsed - scheduledElapsed,
                skippedTicks);
        }

        public static TimeSpan ToElapsed(long stopwatchTicks)
        {
            return TimeSpan.FromSeconds(stopwatchTicks / (double)Stopwatch.Frequency);
        }
    }
}
