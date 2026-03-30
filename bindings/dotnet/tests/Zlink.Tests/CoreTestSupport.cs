using System;
using System.Collections.Concurrent;
using System.Globalization;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;

namespace Zlink.Tests;

internal static class CoreTestSupport
{
    private static readonly Regex VersionRegex = new(
        @"^#define\s+ZLINK_VERSION_(MAJOR|MINOR|PATCH)\s+(\d+)\s*$",
        RegexOptions.Compiled);

    internal static bool IsNativeAvailable()
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

    internal static (int major, int minor, int patch) ReadCoreHeaderVersion()
    {
        string? header = FindCoreHeader();
        if (header == null)
            throw new FileNotFoundException("core/include/zlink.h not found.");

        int major = -1;
        int minor = -1;
        int patch = -1;

        foreach (string line in File.ReadLines(header))
        {
            Match m = VersionRegex.Match(line);
            if (!m.Success)
                continue;

            int value = int.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture);
            switch (m.Groups[1].Value)
            {
                case "MAJOR":
                    major = value;
                    break;
                case "MINOR":
                    minor = value;
                    break;
                case "PATCH":
                    patch = value;
                    break;
            }
        }

        if (major < 0 || minor < 0 || patch < 0)
            throw new InvalidOperationException("Failed to parse zlink version macros.");

        return (major, minor, patch);
    }

    internal static string NewEndpoint(string transport, string prefix)
    {
        if (transport == "inproc")
            return $"inproc://{prefix}-{Guid.NewGuid():N}";
        if (transport == "ipc")
        {
            string file = $"{prefix}-{Guid.NewGuid():N}.sock";
            string path = Path.Combine(Path.GetTempPath(), file);
            return $"ipc://{path}";
        }

        int port = ReservePort();
        return $"{transport}://127.0.0.1:{port}";
    }

    internal static bool IsTransportSupported(string transport)
    {
        if (transport == "tcp" || transport == "inproc")
            return true;
        if (transport == "ipc")
            return Runtime.Has("ipc");
        if (transport == "ws")
            return Runtime.Has("ws");
        if (transport == "wss")
            return Runtime.Has("wss");
        if (transport == "tls")
            return Runtime.Has("tls");
        return false;
    }

    internal static bool WaitUntil(Func<bool> predicate, int timeoutMs,
        int sleepMs = 10)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            if (predicate())
                return true;
            Thread.Sleep(sleepMs);
        }
        return false;
    }

    internal static void SendWithRetry(Zlink.Socket socket,
        ReadOnlySpan<byte> payload, SendFlags flags, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            using Message message = Message.FromBytes(payload);
            try
            {
                socket.Send(message, flags | SendFlags.DontWait);
                return;
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(10);
        }
        throw new TimeoutException("send timeout");
    }

    internal static void SendWithRetry(MessageSocketBase socket,
        ReadOnlySpan<byte> payload, SendFlags flags, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            using Message message = Message.FromBytes(payload);
            try
            {
                socket.Send(message, flags | SendFlags.DontWait);
                return;
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(10);
        }
        throw new TimeoutException("send timeout");
    }

    internal static void PublishWithRetry(Zlink.Socket socket, string topic,
        ReadOnlySpan<byte> payload, SendFlags flags, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            using Message message = Message.FromBytes(payload);
            try
            {
                socket.Publish(topic, message, flags | SendFlags.DontWait);
                return;
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(10);
        }
        throw new TimeoutException("publish timeout");
    }

    internal static void PublishWithRetry(PublisherSocketBase socket, string topic,
        ReadOnlySpan<byte> payload, SendFlags flags, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            using Message message = Message.FromBytes(payload);
            try
            {
                socket.Publish(topic, message, flags | SendFlags.DontWait);
                return;
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(10);
        }
        throw new TimeoutException("publish timeout");
    }

    internal static Message ReceiveMessageWithTimeout(Zlink.Socket socket,
        int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.Receive(out Message message, ReceiveFlags.DontWait);
                return message;
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(10);
        }
        throw new TimeoutException("receive timeout");
    }

    internal static Message ReceiveMessageWithTimeout(MessageSocketBase socket,
        int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.Receive(out Message message, ReceiveFlags.DontWait);
                return message;
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(10);
        }
        throw new TimeoutException("receive timeout");
    }

    internal static byte[] ReceiveBytesWithTimeout(Zlink.Socket socket,
        int maxSize, int timeoutMs)
    {
        _ = maxSize;
        using Message message = ReceiveMessageWithTimeout(socket, timeoutMs);
        return message.AsReadOnlySpan().ToArray();
    }

    internal static byte[] ReceiveBytesWithTimeout(MessageSocketBase socket,
        int maxSize, int timeoutMs)
    {
        _ = maxSize;
        using Message message = ReceiveMessageWithTimeout(socket, timeoutMs);
        return message.AsReadOnlySpan().ToArray();
    }

    internal static string ReceiveUtf8WithTimeout(Zlink.Socket socket, int timeoutMs)
    {
        using Message message = ReceiveMessageWithTimeout(socket, timeoutMs);
        return Encoding.UTF8.GetString(message.AsReadOnlySpan()).Trim('\0');
    }

    internal static string ReceiveUtf8WithTimeout(MessageSocketBase socket,
        int timeoutMs)
    {
        using Message message = ReceiveMessageWithTimeout(socket, timeoutMs);
        return Encoding.UTF8.GetString(message.AsReadOnlySpan()).Trim('\0');
    }

    internal static string SubscribeUtf8WithTimeout(Zlink.Socket socket,
        out string topic, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.Subscribe(out topic, out Message message,
                    ReceiveFlags.DontWait);
                using Message _ = message;
                return Encoding.UTF8.GetString(message.AsReadOnlySpan()).Trim('\0');
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(10);
        }

        throw new TimeoutException("subscribe timeout");
    }

    internal static string SubscribeUtf8WithTimeout(SubscriberSocketBase socket,
        out string topic, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.Subscribe(out topic, out Message message,
                    ReceiveFlags.DontWait);
                using Message _ = message;
                return Encoding.UTF8.GetString(message.AsReadOnlySpan()).Trim('\0');
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(10);
        }

        throw new TimeoutException("subscribe timeout");
    }

    internal static byte[] ReceiveSubscriptionEventWithTimeout(Zlink.Socket socket,
        out bool subscribed, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.ReceiveSubscriptionEvent(out string topic,
                    out subscribed, ReceiveFlags.DontWait);
                return Encoding.UTF8.GetBytes(topic);
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(10);
        }

        throw new TimeoutException("subscription event timeout");
    }

    internal static byte[] ReceiveSubscriptionEventWithTimeout(XPubSocket socket,
        out bool subscribed, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.ReceiveSubscriptionEvent(out string topic,
                    out subscribed, ReceiveFlags.DontWait);
                return Encoding.UTF8.GetBytes(topic);
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(10);
        }

        throw new TimeoutException("subscription event timeout");
    }

    internal static bool ExpectNoSubscriptionEvent(Zlink.Socket socket, int probeMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(probeMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.ReceiveSubscriptionEvent(out _, out _, ReceiveFlags.DontWait);
                return false;
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(5);
        }
        return true;
    }

    internal static bool ExpectNoSubscriptionEvent(XPubSocket socket, int probeMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(probeMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.ReceiveSubscriptionEvent(out _, out _, ReceiveFlags.DontWait);
                return false;
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(5);
        }
        return true;
    }

    internal static string Utf8(Message message)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        return Encoding.UTF8.GetString(message.AsReadOnlySpan()).Trim('\0');
    }

    internal static string Utf8(ReadOnlySpan<byte> payload)
    {
        return Encoding.UTF8.GetString(payload).Trim('\0');
    }

    internal static bool TryReceiveMultipartLastPart(Zlink.Socket socket,
        int maxSize, out byte[] lastPart)
    {
        _ = maxSize;
        try
        {
            socket.Receive(out Message[] parts, ReceiveFlags.DontWait);
            if (parts.Length == 0)
            {
                lastPart = Array.Empty<byte>();
                return true;
            }

            try
            {
                lastPart = parts[parts.Length - 1].AsReadOnlySpan().ToArray();
                return true;
            }
            finally
            {
                foreach (Message part in parts)
                    part.Dispose();
            }
        }
        catch (ZlinkException ex) when (IsRetryable(ex))
        {
            lastPart = Array.Empty<byte>();
            return false;
        }
    }

    internal static bool TryReceiveMultipartLastPart(MessageSocketBase socket,
        int maxSize, out byte[] lastPart)
    {
        _ = maxSize;
        try
        {
            socket.Receive(out Message[] parts, ReceiveFlags.DontWait);
            if (parts.Length == 0)
            {
                lastPart = Array.Empty<byte>();
                return true;
            }

            try
            {
                lastPart = parts[parts.Length - 1].AsReadOnlySpan().ToArray();
                return true;
            }
            finally
            {
                foreach (Message part in parts)
                    part.Dispose();
            }
        }
        catch (ZlinkException ex) when (IsRetryable(ex))
        {
            lastPart = Array.Empty<byte>();
            return false;
        }
    }

    internal static bool ExpectNoMessage(Zlink.Socket socket, int probeMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(probeMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.Receive(out Message message, ReceiveFlags.DontWait);
                using Message _ = message;
                return false;
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(5);
        }
        return true;
    }

    internal static bool ExpectNoMessage(MessageSocketBase socket, int probeMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(probeMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.Receive(out Message message, ReceiveFlags.DontWait);
                using Message _ = message;
                return false;
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(5);
        }
        return true;
    }

    internal static bool ExpectNoSubscribedMessage(Zlink.Socket socket, int probeMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(probeMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.Subscribe(out _, out Message message, ReceiveFlags.DontWait);
                using Message received = message;
                return false;
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(5);
        }
        return true;
    }

    internal static bool ExpectNoSubscribedMessage(SubscriberSocketBase socket,
        int probeMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(probeMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.Subscribe(out _, out Message message, ReceiveFlags.DontWait);
                using Message received = message;
                return false;
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(5);
        }
        return true;
    }

    internal static (string routingId, string payload) ReceiveRoutedUtf8WithTimeout(
        Zlink.Socket socket, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.Receive(out string routingId, out Message message,
                    ReceiveFlags.DontWait);
                using Message received = message;
                return (routingId, Encoding.UTF8.GetString(
                    message.AsReadOnlySpan()).Trim('\0'));
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(10);
        }

        throw new TimeoutException("receive timeout");
    }

    internal static (string routingId, string payload) ReceiveRoutedUtf8WithTimeout(
        RoutedMessageSocketBase socket, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.Receive(out string routingId, out Message message,
                    ReceiveFlags.DontWait);
                using Message received = message;
                return (routingId, Encoding.UTF8.GetString(
                    message.AsReadOnlySpan()).Trim('\0'));
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(10);
        }

        throw new TimeoutException("receive timeout");
    }

    internal static int ExtractPort(string endpoint)
    {
        int idx = endpoint.LastIndexOf(':');
        if (idx <= 0 || idx == endpoint.Length - 1)
            throw new ArgumentException($"invalid endpoint: {endpoint}",
                nameof(endpoint));
        return int.Parse(endpoint.AsSpan(idx + 1), CultureInfo.InvariantCulture);
    }

    private static int ReservePort()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        return port;
    }

    private static bool IsRetryable(ZlinkException ex)
    {
        ErrorCode code = ZlinkException.MapErrorCode(ex.Errno);
        return code == ErrorCode.EAgain || code == ErrorCode.EIntr;
    }

    private static string? FindCoreHeader()
    {
        DirectoryInfo? current = new DirectoryInfo(Directory.GetCurrentDirectory());
        for (int i = 0; i < 10 && current != null; i++)
        {
            string candidate = Path.Combine(current.FullName, "core", "include",
                "zlink.h");
            if (File.Exists(candidate))
                return candidate;
            current = current.Parent;
        }
        return null;
    }
}

internal sealed class CallbackEventQueue<T> : IDisposable
{
    private readonly ConcurrentQueue<T> _queue = new();
    private readonly ManualResetEventSlim _signal = new(false);

    internal void Enqueue(T value)
    {
        _queue.Enqueue(value);
        _signal.Set();
    }

    internal bool TryDequeue(int timeoutMs, out T value)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            if (_queue.TryDequeue(out T? queued))
            {
                value = queued!;
                if (_queue.IsEmpty)
                    _signal.Reset();
                return true;
            }

            TimeSpan remaining = deadline - DateTime.UtcNow;
            if (remaining <= TimeSpan.Zero)
                break;

            _signal.Wait(remaining);
        }

        value = default!;
        return false;
    }

    public void Dispose()
    {
        _signal.Dispose();
    }
}
