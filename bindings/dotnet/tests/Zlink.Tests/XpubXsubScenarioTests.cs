using Xunit;

namespace Zlink.Tests;

public class XpubXsubScenarioTests
{
    [Fact]
    public void XpubXsubSubscription()
    {
        if (!NativeTests.IsNativeAvailable())
            return;

        using var ctx = new Context();
        foreach (var (name, endpoint) in TransportTestHelpers.Transports("xpub"))
        {
            TransportTestHelpers.TryTransport(name, () =>
            {
                using var xpub = new Socket(ctx, SocketType.XPub);
                using var xsub = new Socket(ctx, SocketType.XSub);
                xpub.SetOption(TransportTestHelpers.ZLINK_XPUB_VERBOSE, 1);
                var ep = TransportTestHelpers.EndpointFor(name, endpoint, "-xpub");
                xpub.Bind(ep);
                xsub.Connect(ep);
                TransportTestHelpers.SendWithRetry(xsub,
                    new byte[] { 1, (byte)'t', (byte)'o', (byte)'p', (byte)'i', (byte)'c' },
                    SendFlags.None, 2000);
                var sub = TransportTestHelpers.ReceiveWithTimeout(xpub, 64, 2000);
                Assert.Equal(1, sub[0]);
            });
        }
    }
}
