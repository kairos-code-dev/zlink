using System.Threading.Channels;
using DeliveryDispatch.Shared.Contracts;

namespace DeliveryDispatch.Server.DispatchCenter;

internal sealed class DispatchWorkQueue
{
    private readonly Channel<AssignDelivery> _queue = Channel.CreateUnbounded<AssignDelivery>(
        new UnboundedChannelOptions
        {
            SingleReader = true,
            SingleWriter = false
        });

    public ValueTask EnqueueAsync(
        AssignDelivery delivery,
        CancellationToken cancellationToken)
    {
        return _queue.Writer.WriteAsync(delivery, cancellationToken);
    }

    public IAsyncEnumerable<AssignDelivery> ReadAllAsync(CancellationToken cancellationToken)
    {
        return _queue.Reader.ReadAllAsync(cancellationToken);
    }
}
