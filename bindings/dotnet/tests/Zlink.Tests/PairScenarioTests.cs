using System.Text;
using System.Threading;
using Xunit;

namespace Zlink.Tests;

public class PairScenarioTests
{
    [Fact]
    public void PairMessaging()
    {
        if (!NativeTests.IsNativeAvailable())
            return;

        using var ctx = new Context();
        foreach (var (name, endpoint) in TransportTestHelpers.Transports("pair"))
        {
            TransportTestHelpers.TryTransport(name, () =>
            {
                using var a = new Socket(ctx, SocketType.Pair);
                using var b = new Socket(ctx, SocketType.Pair);
                var ep = TransportTestHelpers.EndpointFor(name, endpoint, "-pair");
                a.Bind(ep);
                b.Connect(ep);
                System.Threading.Thread.Sleep(50);
                TransportTestHelpers.SendWithRetry(b, Encoding.UTF8.GetBytes("ping"),
                    SendFlags.None, 2000);
                var outBuf = TransportTestHelpers.ReceiveWithTimeout(a, 16, 2000);
                Assert.Equal("ping", Encoding.UTF8.GetString(outBuf, 0, 4));
            });
        }
    }

    [Fact]
    public void PairMessagingWithSpanBuffers()
    {
        if (!NativeTests.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var a = new Socket(ctx, SocketType.Pair);
        using var b = new Socket(ctx, SocketType.Pair);
        string endpoint = $"inproc://pair-span-{Guid.NewGuid()}";
        a.Bind(endpoint);
        b.Connect(endpoint);
        Thread.Sleep(50);

        ReadOnlySpan<byte> payload = stackalloc byte[] { 1, 2, 3, 4, 5, 6 };
        b.Send(payload, SendFlags.None);
        Span<byte> recv = stackalloc byte[16];
        int received = a.Receive(recv, ReceiveFlags.None);

        Assert.Equal(payload.Length, received);
        Assert.Equal(payload.ToArray(), recv.Slice(0, received).ToArray());
    }

    [Fact]
    public void PairTrySendTryReceive()
    {
        if (!NativeTests.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var a = new Socket(ctx, SocketType.Pair);
        using var b = new Socket(ctx, SocketType.Pair);
        string endpoint = $"inproc://pair-try-{Guid.NewGuid()}";
        a.Bind(endpoint);
        b.Connect(endpoint);
        Thread.Sleep(50);

        ReadOnlySpan<byte> payload = "try-path"u8;
        Assert.True(b.TrySend(payload, out int sent, SendFlags.None));
        Assert.Equal(payload.Length, sent);

        Span<byte> recv = stackalloc byte[32];
        var deadline = DateTime.UtcNow.AddMilliseconds(2000);
        while (DateTime.UtcNow < deadline)
        {
            if (a.TryReceive(recv, out int received, ReceiveFlags.DontWait))
            {
                Assert.Equal(payload.Length, received);
                Assert.Equal(payload.ToArray(), recv.Slice(0, received).ToArray());
                return;
            }
            Thread.Sleep(1);
        }

        throw new TimeoutException("TryReceive timed out.");
    }
}
