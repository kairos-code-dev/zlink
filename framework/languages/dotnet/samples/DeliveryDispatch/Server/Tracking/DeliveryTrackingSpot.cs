using DeliveryDispatch.Shared.Contracts;
using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.Tracking;

internal sealed class DeliveryTrackingSpot(
    IZLinkSpotContext context,
    DeliverySpotDirectory directory) : IZLinkSpot<CustomerActor>
{
    private readonly Dictionary<string, CustomerActor> _customers = new(StringComparer.Ordinal);
    private readonly List<DeliveryStatusChanged> _history = [];
    private string _deliveryId = string.Empty;

    public IZLinkSpotContext Context { get; } = context;

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        Message request,
        CancellationToken cancellationToken)
    {
        var create = request.FromJson<DeliverySpotCreate>();
        _deliveryId = create.DeliveryId;
        directory.Add(_deliveryId, this);
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept(new DeliverySpotCreated(_deliveryId).ToJson()));
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        CustomerActor actor,
        Message request,
        CancellationToken cancellationToken)
    {
        var join = request.FromJson<DeliverySpotJoin>();
        cancellationToken.ThrowIfCancellationRequested();
        if (!string.Equals(join.DeliveryId, _deliveryId, StringComparison.Ordinal))
        {
            return ValueTask.FromResult(ZLinkSpotActorJoinResult.Reject());
        }

        _customers[actor.ActorId] = actor;
        Console.Error.WriteLine($"deliverydispatch tracking spot: joined delivery={join.DeliveryId} customer={actor.ActorId}");
        return ValueTask.FromResult(
            ZLinkSpotActorJoinResult.Accept(new DeliverySpotJoined(join.DeliveryId, actor.ActorId).ToJson()));
    }

    public void Record(DeliveryStatusChanged status)
    {
        _history.Add(status);
    }
}

internal sealed class DeliverySpotDirectory
{
    private readonly Dictionary<string, DeliveryTrackingSpot> _spots = new(StringComparer.Ordinal);

    public void Add(string deliveryId, DeliveryTrackingSpot spot)
    {
        _spots[deliveryId] = spot;
    }

    public DeliveryTrackingSpot Require(string deliveryId)
    {
        return _spots.TryGetValue(deliveryId, out var spot)
            ? spot
            : throw new InvalidOperationException($"Delivery spot '{deliveryId}' was not created.");
    }
}
