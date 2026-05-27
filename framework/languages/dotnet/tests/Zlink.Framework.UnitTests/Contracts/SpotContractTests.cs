namespace Zlink.Framework.UnitTests;

public sealed class SpotContractTests
{
    [Fact]
    public void SpotCreateResult_And_Info_Expose_SpotRid()
    {
        var spotRid = global::Systems.Zlink.RoutingId.From("0a0b0c");

        var created = new ZLinkSpotCreateResult(spotRid, Created: true);
        var info = new ZLinkSpotInfo(spotRid);

        Assert.Equal(spotRid, created.SpotRid);
        Assert.True(created.Created);
        Assert.Equal(spotRid, info.SpotRid);
    }
}
