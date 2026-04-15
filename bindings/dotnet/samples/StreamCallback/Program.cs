using System.Threading;
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

using var signal = new ManualResetEventSlim(false);
string? callbackPayload = null;
stream.OnPacket((routingId, payload) =>
{
    using (payload)
        callbackPayload = payload.GetString();
    using var reply = Message.FromString("hello-stream");
    stream.Send(routingId, reply);
    signal.Set();
    return 0;
});

using var client = SampleSupport.ConnectRawClient(port);
SampleSupport.WaitMonitorEvent(monitor, 5000, SocketEvent.Accepted);
NetworkStream network = client.GetStream();
SampleSupport.SendAll(network, "hello-stream"u8);
string echoed = System.Text.Encoding.UTF8.GetString(
    SampleSupport.ReceiveExact(network, "hello-stream".Length));
SampleSupport.WaitOrThrow(() => signal.IsSet, 2000, "stream callback timeout");
Console.WriteLine(
    $"[stream/callback] send: \"hello-stream\" -> recv: \"{callbackPayload}\"");
