using System.Threading;
using System.Net.Sockets;
using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var stream = new Zlink.StreamSocket(ctx);
string endpoint = SampleSupport.NewEndpoint("tcp", "stream-callback");
int port = SampleSupport.ExtractPort(endpoint);
stream.Bind(endpoint);

using var signal = new ManualResetEventSlim(false);
stream.AttachStreamRaw((routingId, payload) =>
{
    using (payload)
        Console.WriteLine(payload.GetString());
    using var reply = Message.FromString("stream-callback-reply");
    stream.Send(routingId, reply);
    signal.Set();
    return 0;
});

using var client = SampleSupport.ConnectRawClient(port);
NetworkStream network = client.GetStream();
SampleSupport.SendAll(network, "stream-callback"u8);
Console.WriteLine(System.Text.Encoding.UTF8.GetString(
    SampleSupport.ReceiveExact(network, "stream-callback-reply".Length)));
SampleSupport.WaitOrThrow(() => signal.IsSet, 2000, "stream callback timeout");
