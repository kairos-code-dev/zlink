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
        using var sock = new Socket(ctx, SocketType.Pair);
        Assert.NotEqual(IntPtr.Zero, sock.Handle);
    }
}
