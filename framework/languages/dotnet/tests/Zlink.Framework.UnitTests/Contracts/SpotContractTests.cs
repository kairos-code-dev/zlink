namespace Zlink.Framework.UnitTests;

public sealed class SpotContractTests
{
    [Fact]
    public void SpotCreateResult_And_Info_Expose_SpotRid()
    {
        var spotRid = global::Systems.Zlink.RoutingId.FromString("0a0b0c");

        var created = new ZLinkSpotCreateResult(spotRid, "stage", Created: true);
        var info = new ZLinkSpotInfo(spotRid, "stage");

        Assert.Equal(spotRid, created.SpotRid);
        Assert.Equal("stage", created.SpotName);
        Assert.True(created.Created);
        Assert.Equal(spotRid, info.SpotRid);
        Assert.Equal("stage", info.SpotName);
    }
}
