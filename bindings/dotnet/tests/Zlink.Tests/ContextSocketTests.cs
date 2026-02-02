using Xunit;

namespace Zlink.Tests;

public class ContextSocketTests
{
    [Fact]
    public void CreateContextAndSocket()
    {
        if (!NativeTests.IsNativeAvailable())
            return;
        using var ctx = new Context();
        using var sock = new Socket(ctx, 0); // ZLINK_PAIR
        Assert.NotEqual(IntPtr.Zero, sock.Handle);
    }
}
