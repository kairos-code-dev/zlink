namespace DeliveryDispatch.Server.Tracking.Spots.DeliveryTrackingSpot;

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
