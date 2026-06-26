using DeliveryDispatch.Server.Tracking;
using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.Tracking.Spots.DeliveryTrackingSpot;

internal sealed class DeliveryTrackingSpot(
    IZLinkSpotContext context,
    DeliverySpotDirectory directory) : IZLinkSpot<CustomerActor>
{
    private readonly Dictionary<string, CustomerActor> _customers = new(StringComparer.Ordinal);
    private readonly List<DeliveryStatusChanged> _history = [];
    private string _deliveryId = string.Empty;

    public IZLinkSpotContext Context { get; } = context;

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var create = request.Decode<DeliverySpotCreate>();
        _deliveryId = create.DeliveryId;
        directory.Add(_deliveryId, this);
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept(new DeliverySpotCreated(_deliveryId)));
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        CustomerActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var join = request.Decode<DeliverySpotJoin>();
        cancellationToken.ThrowIfCancellationRequested();
        if (!string.Equals(join.DeliveryId, _deliveryId, StringComparison.Ordinal))
        {
            return ValueTask.FromResult(ZLinkSpotActorJoinResult.Reject());
        }

        _customers[actor.ActorId] = actor;
        Console.Error.WriteLine($"deliverydispatch tracking spot: joined delivery={join.DeliveryId} customer={actor.ActorId}");
        return ValueTask.FromResult(
            ZLinkSpotActorJoinResult.Accept(new DeliverySpotJoined(join.DeliveryId, actor.ActorId)));
    }

    public void Record(DeliveryStatusChanged status)
    {
        _history.Add(status);
    }
}
