using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using Zlink;

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
        if (transport == "inproc")
            return $"inproc://{prefix}-{Guid.NewGuid():N}";

        int port = ReservePort();
        return $"{transport}://127.0.0.1:{port}";
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

    public static void SendUtf8UntilReady(MessageSocketBase socket, string payload,
        int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            using var message = Message.FromString(payload);
            SendResult result = socket.TrySend(message);
            if (result == SendResult.Sent)
                return;
            if (result != SendResult.Backpressured
                && result != SendResult.NotReady)
            {
                throw new InvalidOperationException(
                    $"Unexpected send result: {result}");
            }

            Thread.Sleep(10);
        }

        throw new TimeoutException("send timeout");
    }

    public static void PublishUtf8UntilReady(PublisherSocketBase socket, string topic,
        string payload, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            using var message = Message.FromString(payload);
            SendResult result = socket.TryPublish(topic, message);
            if (result == SendResult.Sent)
                return;
            if (result != SendResult.Backpressured
                && result != SendResult.NotReady)
            {
                throw new InvalidOperationException(
                    $"Unexpected publish result: {result}");
            }

            Thread.Sleep(10);
        }

        throw new TimeoutException("publish timeout");
    }

    public static string ReceiveUtf8(MessageSocketBase socket, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            Received? received = socket.TryReceive();
            if (received != null)
            {
                if (received.Parts.Count == 0)
                    throw new InvalidOperationException(
                        "Expected at least one message part.");
                using Message message = received.Parts[0];
                return Encoding.UTF8.GetString(message.AsReadOnlySpan());
            }

            Thread.Sleep(10);
        }

        throw new TimeoutException("receive timeout");
    }

    public static string SubscribeUtf8(SubscriberSocketBase socket, out string topic,
        int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            Subscribed? subscribed = socket.TrySubscribe();
            if (subscribed != null)
            {
                topic = subscribed.Topic;
                if (subscribed.Parts.Count == 0)
                    throw new InvalidOperationException(
                        "Expected at least one subscribed message part.");
                using Message message = subscribed.Parts[0];
                return Encoding.UTF8.GetString(message.AsReadOnlySpan());
            }

            Thread.Sleep(10);
        }

        throw new TimeoutException("subscribe timeout");
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

    private static int ReservePort()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        return port;
    }
}
