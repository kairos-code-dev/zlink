namespace Zlink.Framework.UnitTests;

public sealed class LocationContractTests
{
    [Theory]
    [InlineData(ZLinkLocationAutoConnectType.RouteMesh, "route-mesh")]
    [InlineData(ZLinkLocationAutoConnectType.ClientServer, "client-server")]
    [InlineData(ZLinkLocationAutoConnectType.DealerMesh, "dealer-mesh")]
    [InlineData(ZLinkLocationAutoConnectType.Fanout, "fanout")]
    [InlineData(ZLinkLocationAutoConnectType.SpotMesh, "spot-mesh")]
    public void AutoConnectType_Canonical_String_RoundTrips(
        ZLinkLocationAutoConnectType type,
        string canonical)
    {
        Assert.Equal(canonical, type.ToCanonicalString());
        Assert.True(ZLinkLocationCanonicalNames.TryParseAutoConnectType(canonical, out var parsed));
        Assert.Equal(type, parsed);
    }

    [Theory]
    [InlineData(ZLinkLocationRole.Router, "router")]
    [InlineData(ZLinkLocationRole.Dealer, "dealer")]
    [InlineData(ZLinkLocationRole.Pub, "pub")]
    [InlineData(ZLinkLocationRole.Sub, "sub")]
    [InlineData(ZLinkLocationRole.Spot, "spot")]
    public void Role_Canonical_String_RoundTrips(ZLinkLocationRole role, string canonical)
    {
        Assert.Equal(canonical, role.ToCanonicalString());
        Assert.True(ZLinkLocationCanonicalNames.TryParseRole(canonical, out var parsed));
        Assert.Equal(role, parsed);
    }

    [Theory]
    [InlineData("ROUTE-MESH")]
    [InlineData("RouteMesh")]
    [InlineData("route_mesh")]
    [InlineData("")]
    public void AutoConnectType_Parse_Rejects_NonCanonical_Values(string value)
    {
        Assert.False(ZLinkLocationCanonicalNames.TryParseAutoConnectType(value, out _));
    }

    [Theory]
    [InlineData("Router")]
    [InlineData("PUB")]
    [InlineData("unknown")]
    public void Role_Parse_Rejects_NonCanonical_Values(string value)
    {
        Assert.False(ZLinkLocationCanonicalNames.TryParseRole(value, out _));
    }

    [Fact]
    public void Invalid_Values_Have_No_Canonical_String()
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => ZLinkLocationAutoConnectType.Invalid.ToCanonicalString());
        Assert.Throws<ArgumentOutOfRangeException>(
            () => ZLinkLocationRole.Invalid.ToCanonicalString());
    }

    [Fact]
    public void WriteResult_Stored_Carries_StoreIssued_Generation_And_UpdatedAt()
    {
        var updatedAt = new DateTimeOffset(2026, 7, 2, 0, 0, 0, TimeSpan.Zero);

        var stored = ZLinkLocationWriteResult.Stored(5, updatedAt);

        Assert.Equal(ZLinkLocationWriteStatus.Stored, stored.Status);
        Assert.Equal(5, stored.Generation);
        Assert.Equal(updatedAt, stored.UpdatedAt);
        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, ZLinkLocationWriteResult.IgnoredStale.Status);
        Assert.Equal(ZLinkLocationWriteStatus.RejectedConflict, ZLinkLocationWriteResult.RejectedConflict.Status);
        Assert.Equal(ZLinkLocationWriteStatus.StoreUnavailable, ZLinkLocationWriteResult.StoreUnavailable.Status);
    }

    [Fact]
    public void PageRequest_Default_Means_Configured_Default_Page_Size()
    {
        ZLinkPageRequest page = default;

        Assert.Equal(0, page.PageSize);
        Assert.Null(page.ContinuationToken);
    }
}
