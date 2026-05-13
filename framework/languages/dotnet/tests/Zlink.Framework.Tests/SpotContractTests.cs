namespace Zlink.Framework.Tests;

public sealed class SpotContractTests
{
    [Fact]
    public void ZLinkSpotId_RoundTrips_RoutingId()
    {
        var routingId = global::Systems.Zlink.RoutingId.FromString("01020304");

        var spotId = ZLinkSpotId.FromRoutingId(routingId);

        Assert.Equal("01020304", spotId.Value);
        Assert.Equal(routingId, spotId.ToRoutingId());
        Assert.Equal(spotId.Value, spotId.ToString());
    }

    [Fact]
    public void SpotCreateResult_And_Info_Expose_SpotId()
    {
        var spotId = new ZLinkSpotId("0a0b0c");

        var created = new ZLinkSpotCreateResult(spotId, "stage", created: true);
        var info = new ZLinkSpotInfo(spotId, "stage");

        Assert.Equal(spotId, created.SpotId);
        Assert.Equal(spotId.ToRoutingId(), created.SpotRid);
        Assert.Equal("stage", created.SpotName);
        Assert.True(created.Created);
        Assert.Equal(spotId, info.SpotId);
        Assert.Equal(spotId.ToRoutingId(), info.SpotRid);
        Assert.Equal("stage", info.SpotName);
    }
}
