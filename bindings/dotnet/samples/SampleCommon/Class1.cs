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

    public static void SendUtf8UntilReady(Zlink.Socket socket, string payload,
        int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            using var message = Message.FromString(payload);
            try
            {
                socket.Send(message, SendFlags.DontWait);
                return;
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
                Thread.Sleep(10);
            }
        }

        throw new TimeoutException("send timeout");
    }

    public static void PublishUtf8UntilReady(Zlink.Socket socket, string topic,
        string payload, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            using var message = Message.FromString(payload);
            try
            {
                socket.Publish(topic, message, SendFlags.DontWait);
                return;
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
                Thread.Sleep(10);
            }
        }

        throw new TimeoutException("publish timeout");
    }

    public static string ReceiveUtf8(Zlink.Socket socket, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.Receive(out Message message, ReceiveFlags.DontWait);
                using (message)
                    return Encoding.UTF8.GetString(message.AsReadOnlySpan());
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
                Thread.Sleep(10);
            }
        }

        throw new TimeoutException("receive timeout");
    }

    public static string SubscribeUtf8(Zlink.Socket socket, out string topic,
        int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.Subscribe(out topic, out Message message,
                    ReceiveFlags.DontWait);
                using (message)
                    return Encoding.UTF8.GetString(message.AsReadOnlySpan());
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
                Thread.Sleep(10);
            }
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

    private static bool IsRetryable(ZlinkException ex)
    {
        ErrorCode code = ZlinkException.MapErrorCode(ex.Errno);
        return code == ErrorCode.EAgain || code == ErrorCode.EIntr;
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
