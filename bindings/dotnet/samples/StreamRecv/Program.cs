using System.Net.Sockets;
using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var stream = new Zlink.StreamSocket(ctx);
string endpoint = SampleSupport.NewEndpoint("tcp", "stream-recv");
int port = SampleSupport.ExtractPort(endpoint);
stream.Bind(endpoint);

using var client = SampleSupport.ConnectRawClient(port);
NetworkStream network = client.GetStream();
byte[] request = "stream-recv"u8.ToArray();
SampleSupport.SendAll(network, request);

Received received = stream.Receive();
string routingId = received.RoutingId;
using (Message payload = received.Parts[0])
{
    Console.WriteLine(payload.GetString());
}

using var reply = Message.FromString("stream-reply");
stream.Send(routingId, reply);
Console.WriteLine(System.Text.Encoding.UTF8.GetString(
    SampleSupport.ReceiveExact(network, "stream-reply".Length)));
