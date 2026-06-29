using System.Threading.Channels;
using DeliveryDispatch.Shared.Contracts;

namespace DeliveryDispatch.Server.Dispatch;

internal sealed class DispatchWorkQueue
{
    private readonly Channel<AssignDelivery> _queue = Channel.CreateUnbounded<AssignDelivery>();

    public ValueTask EnqueueAsync(
        AssignDelivery request,
        CancellationToken cancellationToken)
    {
        return _queue.Writer.WriteAsync(request, cancellationToken);
    }

    public IAsyncEnumerable<AssignDelivery> ReadAllAsync(CancellationToken cancellationToken)
    {
        return _queue.Reader.ReadAllAsync(cancellationToken);
    }
}
