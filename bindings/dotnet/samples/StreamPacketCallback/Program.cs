using System.Threading;
using SampleCommon;
using Systems.Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var stream = new Systems.Zlink.StreamSocket(ctx);
string endpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
int port = SampleSupport.ExtractPort(endpoint);
using var monitor = stream.MonitorOpen(SocketEvent.Accepted);
stream.Bind(endpoint);

using var signal = new ManualResetEventSlim(false);
string? callbackPayload = null;
StreamPacketHandler handler = (routingId, header, body) =>
{
    using (header)
    using (body)
    {
        callbackPayload = body.GetString();
    }
    signal.Set();
};
stream.OnPacket(handler);

using var client = SampleSupport.ConnectRawClient(port);
SampleSupport.WaitMonitorEvent(monitor, 5000, SocketEvent.Accepted);
SampleSupport.SendStreamPacket(client.GetStream(), "hello-stream"u8);
SampleSupport.WaitOrThrow(() => signal.IsSet, 2000,
    "stream packet callback timeout");
Console.WriteLine(
    $"[stream/packet-callback] recv: \"{callbackPayload}\"");
