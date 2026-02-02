using System.Text;
using Xunit;

namespace Zlink.Tests;

public class DealerRouterScenarioTests
{
    [Fact]
    public void DealerRouterMessaging()
    {
        if (!NativeTests.IsNativeAvailable())
            return;

        using var ctx = new Context();
        foreach (var (name, endpoint) in TransportTestHelpers.Transports("dealer-router"))
        {
            TransportTestHelpers.TryTransport(name, () =>
            {
                using var router = new Socket(ctx, SocketType.Router);
                using var dealer = new Socket(ctx, SocketType.Dealer);
                var ep = TransportTestHelpers.EndpointFor(name, endpoint, "-dr");
                router.Bind(ep);
                dealer.Connect(ep);
                System.Threading.Thread.Sleep(50);
                TransportTestHelpers.SendWithRetry(dealer, Encoding.UTF8.GetBytes("hello"),
                    SendFlags.None, 2000);
                var rid = TransportTestHelpers.ReceiveWithTimeout(router, 256, 2000);
                var payload = TransportTestHelpers.ReceiveWithTimeout(router, 256, 2000);
                Assert.Equal("hello", Encoding.UTF8.GetString(payload).Trim('\0'));
                router.Send(rid, SendFlags.SendMore);
                TransportTestHelpers.SendWithRetry(router, Encoding.UTF8.GetBytes("world"),
                    SendFlags.None, 2000);
                var resp = TransportTestHelpers.ReceiveWithTimeout(dealer, 64, 2000);
                Assert.Equal("world", Encoding.UTF8.GetString(resp).Trim('\0'));
            });
        }
    }
}
