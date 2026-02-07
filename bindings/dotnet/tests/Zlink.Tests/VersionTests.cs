using Xunit;

namespace Zlink.Tests;

public class VersionTests
{
    [Fact]
    public void VersionMatchesCore()
    {
        if (!NativeTests.IsNativeAvailable())
            return;
        var (major, minor, patch) = ZlinkVersion.Get();
        Assert.Equal(0, major);
        Assert.Equal(7, minor);
        Assert.Equal(0, patch);
    }
}
