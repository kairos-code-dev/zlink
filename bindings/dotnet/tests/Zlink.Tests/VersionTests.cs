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
        Assert.Equal(1, major);
        Assert.Equal(0, minor);
        Assert.Equal(0, patch);
    }
}
