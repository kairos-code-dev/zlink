using System.Text;
using Xunit;

namespace Zlink.Tests;

public class DiscoveryGatewaySpotScenarioTests
{
    [Fact]
    public void DiscoveryGatewaySpotFlow()
    {
        if (!NativeTests.IsNativeAvailable())
            return;

        using var ctx = new Context();
        foreach (var (name, endpoint) in TransportTestHelpers.Transports("discovery"))
        {
            TransportTestHelpers.TryTransport(name, () =>
            {
                using var registry = new Registry(ctx);
                string regPub = $"inproc://reg-pub-{System.Guid.NewGuid()}";
                string regRouter = $"inproc://reg-router-{System.Guid.NewGuid()}";
                registry.SetEndpoints(regPub, regRouter);
                registry.Start();

                using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
                discovery.ConnectRegistry(regPub);
                discovery.Subscribe("svc");

                using var receiver = new Receiver(ctx);
                var serviceEp = TransportTestHelpers.EndpointFor(name, endpoint, "-svc");
                receiver.Bind(serviceEp);
                using var receiverRouter = receiver.CreateRouterSocket();
                string advertise = serviceEp;
                receiver.ConnectRegistry(regRouter);
                receiver.Register("svc", advertise, 1);
                int status = -1;
                for (int i = 0; i < 20; i++)
                {
                    status = receiver.RegisterResult("svc").Status;
                    if (status == 0)
                        break;
                    System.Threading.Thread.Sleep(50);
                }
                Assert.Equal(0, status);
                var discoverDeadline = DateTime.UtcNow.AddMilliseconds(5000);
                while (DateTime.UtcNow < discoverDeadline &&
                       discovery.ReceiverCount("svc") <= 0)
                {
                    System.Threading.Thread.Sleep(10);
                }
                Assert.True(discovery.ReceiverCount("svc") > 0);

                using var gateway = new Gateway(ctx, discovery);
                var connDeadline = DateTime.UtcNow.AddMilliseconds(5000);
                while (DateTime.UtcNow < connDeadline &&
                       gateway.ConnectionCount("svc") <= 0)
                {
                    System.Threading.Thread.Sleep(10);
                }
                Assert.True(gateway.ConnectionCount("svc") > 0);

                byte[] targetRoutingId = Array.Empty<byte>();
                var ridDeadline = DateTime.UtcNow.AddMilliseconds(5000);
                while (DateTime.UtcNow < ridDeadline)
                {
                    var receivers = discovery.GetReceivers("svc");
                    if (receivers.Length > 0 && receivers[0].RoutingId.Length > 0)
                    {
                        targetRoutingId = receivers[0].RoutingId;
                        break;
                    }
                    System.Threading.Thread.Sleep(10);
                }
                Assert.NotEmpty(targetRoutingId);

                byte[] helloPayload = Encoding.UTF8.GetBytes("hello");
                TransportTestHelpers.SendWithRetryToRoutingId(gateway, "svc",
                    targetRoutingId.AsSpan(), helloPayload.AsSpan(),
                    SendFlags.None, 5000);

                var rid = TransportTestHelpers.ReceiveWithTimeout(receiverRouter, 256, 2000);
                var payload = Array.Empty<byte>();
                for (int i = 0; i < 3; i++)
                {
                    payload = TransportTestHelpers.ReceiveWithTimeout(receiverRouter, 64, 2000);
                    if (Encoding.UTF8.GetString(payload).Trim('\0') == "hello")
                        break;
                }
                Assert.Equal("hello", Encoding.UTF8.GetString(payload).Trim('\0'));

                Assert.NotEmpty(rid);

                using var node = new SpotNode(ctx);
                var spotEp = TransportTestHelpers.EndpointFor(name, endpoint, "-spot");
                node.Bind(spotEp);
                string spotAdvertise = spotEp;
                node.ConnectRegistry(regRouter);
                node.Register("spot", spotAdvertise);
                System.Threading.Thread.Sleep(100);

                using var peerNode = new SpotNode(ctx);
                peerNode.ConnectRegistry(regRouter);
                peerNode.ConnectPeerPub(spotAdvertise);
                using var spot = new Spot(peerNode);
                System.Threading.Thread.Sleep(100);
                spot.Subscribe("topic");

                spot.Publish("topic",
                    new[] { Message.FromBytes(Encoding.UTF8.GetBytes("spot-msg")) });
                var spotMsg = TransportTestHelpers.SpotReceiveWithTimeout(spot, 2000);
                Assert.Equal("topic", spotMsg.TopicId);
                Assert.Single(spotMsg.Parts);
                Assert.Equal("spot-msg",
                    Encoding.UTF8.GetString(spotMsg.Parts[0].ToArray()));

                byte[] fastPayload = Encoding.UTF8.GetBytes("spot-fast");
                spot.Publish("topic", fastPayload.AsSpan(), SendFlags.None);
                Span<byte> fastRecv = stackalloc byte[64];
                int fastLen = spot.ReceiveSinglePayload(fastRecv,
                    ReceiveFlags.None);
                Assert.Equal("spot-fast",
                    Encoding.UTF8.GetString(fastRecv.Slice(0, fastLen)));
            });
        }
    }
}
