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

                using var discovery = new Discovery(ctx, DiscoveryServiceType.GatewayReceiver);
                discovery.ConnectRegistry(regPub);
                discovery.Subscribe("svc");

                using var provider = new Receiver(ctx);
                var serviceEp = TransportTestHelpers.EndpointFor(name, endpoint, "-svc");
                provider.Bind(serviceEp);
                using var providerRouter = provider.CreateRouterSocket();
                string advertise = serviceEp;
                provider.ConnectRegistry(regRouter);
                provider.Register("svc", advertise, 1);

                using var gateway = new Gateway(ctx, discovery);
                gateway.Send("svc",
                    new[] { Message.FromBytes(Encoding.UTF8.GetBytes("hello")) });

                var rid = TransportTestHelpers.ReceiveWithTimeout(providerRouter, 256, 2000);
                var payload = Array.Empty<byte>();
                for (int i = 0; i < 3; i++)
                {
                    payload = TransportTestHelpers.ReceiveWithTimeout(providerRouter, 64, 2000);
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

                using var peerNode = new SpotNode(ctx);
                peerNode.ConnectRegistry(regRouter);
                peerNode.ConnectPeerPub(spotAdvertise);
                using var spot = new Spot(peerNode);
                spot.Subscribe("topic");

                spot.Publish("topic",
                    new[] { Message.FromBytes(Encoding.UTF8.GetBytes("spot-msg")) });
                var spotMsg = TransportTestHelpers.SpotReceiveWithTimeout(spot, 2000);
                Assert.Equal("topic", spotMsg.TopicId);
                Assert.Single(spotMsg.Parts);
                Assert.Equal("spot-msg",
                    Encoding.UTF8.GetString(spotMsg.Parts[0].ToArray()));
            });
        }
    }
}
