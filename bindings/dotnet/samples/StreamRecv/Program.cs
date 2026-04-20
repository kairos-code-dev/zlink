using System.Net.Sockets;
using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var stream = new Zlink.StreamSocket(ctx);
string endpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
int port = SampleSupport.ExtractPort(endpoint);
using var monitor = stream.MonitorOpen(SocketEvent.Accepted);
stream.Bind(endpoint);

using var client = SampleSupport.ConnectRawClient(port);
SampleSupport.WaitMonitorEvent(monitor, 5000, SocketEvent.Accepted);
NetworkStream network = client.GetStream();
byte[] request = "hello-stream"u8.ToArray();
SampleSupport.SendAll(network, request);

using Received received = stream.Recv();
RoutingId routingId = received.RoutingId
    ?? throw new InvalidOperationException("missing routing id");
string payload = received.Parts[0].GetString();
SampleSupport.EnsureEqual("hello-stream", payload, "payload");

using var reply = Message.FromString("hello-stream");
stream.Send(routingId, reply);
string echoed = System.Text.Encoding.UTF8.GetString(
    SampleSupport.ReceiveExact(network, "hello-stream".Length));
Console.WriteLine(
    $"[stream/recv] send: \"hello-stream\" -> recv: \"{echoed}\"");
