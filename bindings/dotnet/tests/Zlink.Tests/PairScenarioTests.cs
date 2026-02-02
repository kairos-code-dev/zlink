using System.Text;
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
}
