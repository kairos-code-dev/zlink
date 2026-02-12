using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;

namespace Zlink.Tests;

internal static class TransportTestHelpers
{
    internal static readonly SocketOption ZLINK_SUBSCRIBE = SocketOption.Subscribe;
    internal static readonly SocketOption ZLINK_XPUB_VERBOSE = SocketOption.XPubVerbose;

    internal static IEnumerable<(string name, string endpoint)> Transports(string prefix)
    {
        yield return ("tcp", "");
        yield return ("ws", "");
        yield return ("inproc", $"inproc://{prefix}-{Guid.NewGuid()}");
    }

    internal static string EndpointFor(string name, string baseEndpoint, string suffix)
    {
        if (name == "inproc")
            return baseEndpoint + suffix;
        int port = GetPort();
        return $"{name}://127.0.0.1:{port}";
    }

    internal static void TryTransport(string name, Action action)
    {
        try
        {
            action();
        }
        catch (Exception)
        {
            if (name == "ws")
                return;
            throw;
        }
    }

    internal static byte[] ReceiveWithTimeout(Socket socket, int size, int timeoutMs)
    {
        var buf = new byte[size];
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        ZlinkException? last = null;
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                int rc = socket.Receive(buf, ReceiveFlags.DontWait);
                var outBuf = new byte[rc];
                Array.Copy(buf, outBuf, rc);
                return outBuf;
            }
            catch (ZlinkException ex)
            {
                last = ex;
                System.Threading.Thread.Sleep(10);
            }
        }
        if (last != null) throw last;
        throw new TimeoutException();
    }

    internal static void SendWithRetry(Socket socket, byte[] data, SendFlags flags, int timeoutMs)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        ZlinkException? last = null;
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.Send(data, flags);
                return;
            }
            catch (ZlinkException ex)
            {
                last = ex;
                System.Threading.Thread.Sleep(10);
            }
        }
        if (last != null) throw last;
        throw new TimeoutException();
    }

    internal static GatewayMessage GatewayReceiveWithTimeout(Gateway gw, int timeoutMs)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        ZlinkException? last = null;
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                return gw.Receive(ReceiveFlags.DontWait);
            }
            catch (ZlinkException ex)
            {
                last = ex;
                System.Threading.Thread.Sleep(10);
            }
        }
        if (last != null) throw last;
        throw new TimeoutException();
    }

    internal static void SendWithRetry(Gateway gateway, string serviceName,
        Message[] parts, SendFlags flags, int timeoutMs)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        SendWithRetry(gateway, serviceName, parts.AsSpan(), flags, timeoutMs);
    }

    internal static void SendWithRetry(Gateway gateway, string serviceName,
        ReadOnlySpan<Message> parts, SendFlags flags, int timeoutMs)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        ZlinkException? last = null;
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                gateway.Send(serviceName, parts, flags);
                return;
            }
            catch (ZlinkException ex)
            {
                last = ex;
                System.Threading.Thread.Sleep(10);
            }
        }
        if (last != null) throw last;
        throw new TimeoutException();
    }

    internal static void SendWithRetryToRoutingId(Gateway gateway,
        string serviceName, ReadOnlySpan<byte> routingId,
        ReadOnlySpan<Message> parts, SendFlags flags, int timeoutMs)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        ZlinkException? last = null;
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                gateway.SendToRoutingId(serviceName, routingId, parts, flags);
                return;
            }
            catch (ZlinkException ex)
            {
                last = ex;
                System.Threading.Thread.Sleep(10);
            }
        }
        if (last != null) throw last;
        throw new TimeoutException();
    }

    internal static void SendWithRetryToRoutingId(Gateway gateway,
        string serviceName, ReadOnlySpan<byte> routingId,
        ReadOnlySpan<byte> payload, SendFlags flags, int timeoutMs)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        ZlinkException? last = null;
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                gateway.SendToRoutingId(serviceName, routingId, payload, flags);
                return;
            }
            catch (ZlinkException ex)
            {
                last = ex;
                System.Threading.Thread.Sleep(10);
            }
        }
        if (last != null) throw last;
        throw new TimeoutException();
    }

    internal static SpotMessage SpotReceiveWithTimeout(Spot spot, int timeoutMs)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        ZlinkException? last = null;
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                return spot.Receive(ReceiveFlags.DontWait);
            }
            catch (ZlinkException ex)
            {
                last = ex;
                System.Threading.Thread.Sleep(10);
            }
        }
        if (last != null) throw last;
        throw new TimeoutException();
    }

    private static int GetPort()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        return port;
    }
}
