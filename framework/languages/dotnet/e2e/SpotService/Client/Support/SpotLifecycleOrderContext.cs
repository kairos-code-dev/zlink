namespace SpotService.Client.Support;

internal sealed class SpotLifecycleOrderContext
{
    public string SpotRid { get; } = $"spot-owner-order-sm-a4-{Guid.NewGuid():N}";

    public int CurrentValue { get; set; }
}