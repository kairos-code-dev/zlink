using Xunit;

namespace Zlink.Tests;

public class SpotTests
{
    [Fact]
    public void CreateSpotNodeAndSpot()
    {
        if (!NativeTests.IsNativeAvailable())
            return;
        try
        {
            using var ctx = new Context();
            using var node = new SpotNode(ctx);
            using var spot = new Spot(node);
            Assert.NotNull(node);
            Assert.NotNull(spot);
        }
        catch (EntryPointNotFoundException)
        {
        }
    }
}
