using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using Zlink;
using Zlink.Service;

namespace SampleCommon;

public static class SampleSupport
{
    public static bool IsNativeAvailable()
    {
        try
        {
            _ = ZlinkVersion.Get();
            return true;
        }
        catch (DllNotFoundException)
        {
            return false;
        }
        catch (EntryPointNotFoundException)
        {
            return false;
        }
        catch (TypeInitializationException ex) when (ex.InnerException
                is DllNotFoundException or EntryPointNotFoundException)
        {
            return false;
        }
    }

    public static string NewEndpoint(string transport, string prefix)
    {
        int port = ReservePort();
        return $"{transport}://127.0.0.1:{port}";
    }

    public static int ReservePort()
    {
        TcpListener listener = new(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        return port;
    }

    public static void WaitConnected(params SocketMonitor[] monitors)
    {
        foreach (SocketMonitor monitor in monitors)
            WaitMonitorEvent(monitor, 5000, SocketEvent.ConnectionReady);
    }

    public static SocketMonitorEvent WaitMonitorEvent(SocketMonitor monitor,
        int timeoutMs, params SocketEvent[] expectedEvents)
    {
        if (expectedEvents == null || expectedEvents.Length == 0)
        {
            throw new ArgumentException("Expected monitor events are required.",
                nameof(expectedEvents));
        }

        _ = timeoutMs;
        SocketMonitorEvent evt = monitor.Recv();
        for (int i = 0; i < expectedEvents.Length; i++)
        {
            if (evt.Event == expectedEvents[i])
                return evt;
        }

        throw new InvalidOperationException(
            $"Unexpected monitor event {evt.Event}.");
    }

    public static void WaitOrThrow(Func<bool> predicate, int timeoutMs,
        string message)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            if (predicate())
                return;
            Thread.Sleep(10);
        }

        throw new TimeoutException(message);
    }

    public static void WaitSpotPeerConnected(SpotNode node, int timeoutMs = 5000)
    {
        WaitOrThrow(
            () => node.StatusSnapshot().ConnectedPeerCount > 0,
            timeoutMs,
            "spot peer connection");
    }

    public static string ReceiveUtf8(MessageSocketBase socket, int timeoutMs)
    {
        _ = timeoutMs;
        Received received = socket.Recv();
        if (received.Parts.Count == 0)
            throw new InvalidOperationException(
                "Expected at least one message part.");
        using Message message = received.Parts[0];
        return Encoding.UTF8.GetString(message.AsReadOnlySpan());
    }

    public static string SubscribeUtf8(SubscriberSocketBase socket, out string topic,
        int timeoutMs)
    {
        _ = timeoutMs;
        Subscribed subscribed = socket.Subscribe();
        topic = subscribed.Topic;
        if (subscribed.Parts.Count == 0)
            throw new InvalidOperationException(
                "Expected at least one subscribed message part.");
        using Message message = subscribed.Parts[0];
        return Encoding.UTF8.GetString(message.AsReadOnlySpan());
    }

    public static TcpClient ConnectRawClient(int port)
    {
        var client = new TcpClient();
        client.NoDelay = true;
        client.Connect(IPAddress.Loopback, port);
        return client;
    }

    public static void SendAll(NetworkStream stream, ReadOnlySpan<byte> payload)
    {
        stream.Write(payload);
        stream.Flush();
    }

    public static byte[] ReceiveExact(NetworkStream stream, int size)
    {
        byte[] buffer = new byte[size];
        int read = 0;
        while (read < size)
        {
            int n = stream.Read(buffer, read, size - read);
            if (n <= 0)
                throw new IOException("stream receive timeout");
            read += n;
        }

        return buffer;
    }

    public static int ExtractPort(string endpoint)
    {
        int idx = endpoint.LastIndexOf(':');
        return int.Parse(endpoint.AsSpan(idx + 1));
    }

    public static void EnsureEqual(string expected, string actual, string name)
    {
        if (!string.Equals(expected, actual, StringComparison.Ordinal))
        {
            throw new InvalidOperationException(
                $"Expected {name} \"{expected}\" but received \"{actual}\".");
        }
    }

}
