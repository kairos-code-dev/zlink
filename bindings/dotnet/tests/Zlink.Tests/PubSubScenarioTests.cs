using System.Text;
using Xunit;

namespace Zlink.Tests;

public class PubSubScenarioTests
{
    [Fact]
    public void PubSubMessaging()
    {
        if (!NativeTests.IsNativeAvailable())
            return;

        using var ctx = new Context();
        foreach (var (name, endpoint) in TransportTestHelpers.Transports("pubsub"))
        {
            TransportTestHelpers.TryTransport(name, () =>
            {
                using var pub = new Socket(ctx, SocketType.Pub);
                using var sub = new Socket(ctx, SocketType.Sub);
                var ep = TransportTestHelpers.EndpointFor(name, endpoint, "-pubsub");
                pub.Bind(ep);
                sub.Connect(ep);
                sub.SetOption(TransportTestHelpers.ZLINK_SUBSCRIBE,
                    Encoding.UTF8.GetBytes("topic"));
                System.Threading.Thread.Sleep(50);
                TransportTestHelpers.SendWithRetry(pub,
                    Encoding.UTF8.GetBytes("topic payload"), SendFlags.None, 2000);
                var buf = TransportTestHelpers.ReceiveWithTimeout(sub, 64, 2000);
                Assert.StartsWith("topic", Encoding.UTF8.GetString(buf).Trim('\0'));
            });
        }
    }
}
