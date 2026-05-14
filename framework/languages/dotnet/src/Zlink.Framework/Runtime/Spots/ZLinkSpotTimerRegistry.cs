using Zlink.Framework.Runtime.Timers;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotTimerRegistry : IAsyncDisposable
{
    private readonly List<IZLinkTimer> _timers = [];

    public ValueTask<IZLinkTimer> AddAsync(
        string name,
        TimeSpan period,
        Type handlerType,
        Type spotType,
        CancellationToken stopToken,
        Func<ZLinkSpotTimerDescriptor, CancellationToken, ValueTask> dispatchAsync,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (string.IsNullOrWhiteSpace(name))
        {
            throw new ZLinkConfigurationException("SPOT timer name must not be empty.");
        }

        if (period <= TimeSpan.Zero)
        {
            throw new ZLinkConfigurationException("SPOT timer period must be greater than zero.");
        }

        var descriptor = ZLinkSpotDescriptorFactory.CreateTimerDescriptor(name, handlerType, spotType);
        var timer = new ZLinkTimer(
            period,
            stopToken,
            ct => dispatchAsync(descriptor, ct));
        _timers.Add(timer);
        return ValueTask.FromResult<IZLinkTimer>(timer);
    }

    public async ValueTask DisposeAsync()
    {
        foreach (var timer in _timers)
        {
            await timer.DisposeAsync().ConfigureAwait(false);
        }
    }
}
