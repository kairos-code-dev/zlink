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

    [Fact]
    public void GetOptionWithSpanBuffer()
    {
        if (!NativeTests.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var a = new Socket(ctx, SocketType.Pair);
        using var b = new Socket(ctx, SocketType.Pair);
        string endpoint = $"inproc://ctx-opt-{Guid.NewGuid()}";
        a.Bind(endpoint);
        b.Connect(endpoint);

        Span<byte> optionBuffer = stackalloc byte[256];
        int actualSize = a.GetOption(SocketOption.LastEndpoint, optionBuffer);
        Assert.True(actualSize > 0);
    }
}
