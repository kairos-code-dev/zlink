using System.Text;
using Xunit;

namespace Zlink.Tests;

public class MultipartScenarioTests
{
    [Fact]
    public void MultipartPair()
    {
        if (!NativeTests.IsNativeAvailable())
            return;

        using var ctx = new Context();
        foreach (var (name, endpoint) in TransportTestHelpers.Transports("multipart"))
        {
            TransportTestHelpers.TryTransport(name, () =>
            {
                using var a = new Socket(ctx, SocketType.Pair);
                using var b = new Socket(ctx, SocketType.Pair);
                var ep = TransportTestHelpers.EndpointFor(name, endpoint, "-mp");
                a.Bind(ep);
                b.Connect(ep);
                System.Threading.Thread.Sleep(50);
                TransportTestHelpers.SendWithRetry(b, Encoding.UTF8.GetBytes("a"),
                    SendFlags.SendMore, 2000);
                TransportTestHelpers.SendWithRetry(b, Encoding.UTF8.GetBytes("b"),
                    SendFlags.None, 2000);
                var p1 = TransportTestHelpers.ReceiveWithTimeout(a, 16, 2000);
                var p2 = TransportTestHelpers.ReceiveWithTimeout(a, 16, 2000);
                Assert.Equal("a", Encoding.UTF8.GetString(p1).Trim('\0'));
                Assert.Equal("b", Encoding.UTF8.GetString(p2).Trim('\0'));
            });
        }
    }
}
