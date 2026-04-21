using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using Xunit;

namespace Zlink.Tests;

public sealed class test_callback_contract
{
    private static TcpClient ConnectRawClient(int port)
    {
        var client = new TcpClient();
        client.NoDelay = true;
        client.ReceiveTimeout = 15000;
        client.SendTimeout = 15000;
        client.Connect(IPAddress.Loopback, port);
        return client;
    }

    private static void SendAll(NetworkStream stream, ReadOnlySpan<byte> payload)
    {
        stream.Write(payload);
        stream.Flush();
    }

    [Fact]
    public void stream_packet_handler_transfers_message_ownership_to_application()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new StreamSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("tcp",
            "callback-owned");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        using var receivedSignal = new ManualResetEventSlim(false);
        Message? owned = null;
        stream.OnPacket((StreamPacketHandler)((_, payload) =>
        {
            owned = payload;
            receivedSignal.Set();
            return 0;
        }));

        using var client = ConnectRawClient(port);
        SendAll(client.GetStream(), "callback-owned"u8);

        Assert.True(receivedSignal.Wait(20000));
        Assert.NotNull(owned);
        Assert.Equal("callback-owned",
            Encoding.UTF8.GetString(owned!.AsReadOnlySpan()));
        owned.Dispose();
        Assert.Throws<ObjectDisposedException>(() => _ = owned.Size);
    }

    [Fact]
    public void stream_packet_handler_exception_reports_unhandled_callback_exception()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new StreamSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("tcp",
            "callback-packet-ex");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        using var observedSignal = new ManualResetEventSlim(false);
        Exception? observed = null;
        void OnUnhandled(Exception ex)
        {
            observed = ex;
            observedSignal.Set();
        }

        Runtime.UnhandledCallbackException += OnUnhandled;
        try
        {
            stream.OnPacket((StreamPacketHandler)((_, payload) =>
            {
                payload.Dispose();
                throw new InvalidOperationException("stream-packet-fail");
            }));

            using var client = ConnectRawClient(port);
            SendAll(client.GetStream(), "stream-packet-fail"u8);

            Assert.True(observedSignal.Wait(10000));
            Assert.NotNull(observed);
            Assert.Equal("stream-packet-fail", observed!.Message);
        }
        finally
        {
            Runtime.UnhandledCallbackException -= OnUnhandled;
            try
            {
                stream.DetachStream();
            }
            catch (ZlinkException)
            {
            }
        }
    }
}
