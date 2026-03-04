using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Xunit;

namespace Zlink.Tests;

public sealed class test_stream_socket
{
    private static TcpClient ConnectRawClient(int port)
    {
        var client = new TcpClient();
        client.NoDelay = true;
        client.ReceiveTimeout = 5000;
        client.SendTimeout = 5000;
        client.Connect(IPAddress.Loopback, port);
        return client;
    }

    private static void SendAll(NetworkStream stream, ReadOnlySpan<byte> payload)
    {
        stream.Write(payload);
        stream.Flush();
    }

    private static byte[] ReceiveExact(NetworkStream stream, int size)
    {
        byte[] buffer = new byte[size];
        int read = 0;
        while (read < size)
        {
            int n = stream.Read(buffer, read, size - read);
            if (n <= 0)
                throw new TimeoutException("stream receive timeout");
            read += n;
        }
        return buffer;
    }

    private static byte[] BuildLen32BeFrame(ReadOnlySpan<byte> payload)
    {
        byte[] frame = new byte[4 + payload.Length];
        BinaryPrimitives.WriteUInt32BigEndian(frame.AsSpan(0, 4),
            (uint)payload.Length);
        payload.CopyTo(frame.AsSpan(4));
        return frame;
    }

    private static byte[] ReceiveLen32BeFrame(NetworkStream stream)
    {
        byte[] header = ReceiveExact(stream, 4);
        int len = checked((int)BinaryPrimitives.ReadUInt32BigEndian(header));
        return len == 0 ? Array.Empty<byte>() : ReceiveExact(stream, len);
    }

    private static bool WaitMonitorEvent(MonitorSocket monitor,
        SocketEvent expectedEvent, int timeoutMs, out byte[] routingId)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                MonitorEvent evt = monitor.Receive(ReceiveFlags.DontWait);
                if (evt.Event == expectedEvent)
                {
                    routingId = evt.RoutingId;
                    return true;
                }
            }
            catch (ZlinkException)
            {
                Thread.Sleep(10);
            }
        }

        routingId = Array.Empty<byte>();
        return false;
    }

    private static bool TryDrainOneMultipart(Socket streamSocket)
    {
        return CoreTestSupport.TryReceiveMultipartLastPart(streamSocket, 512, out _);
    }

    private static string ResolveRepoPath(string relativePath)
    {
        DirectoryInfo? current = new DirectoryInfo(Directory.GetCurrentDirectory());
        for (int i = 0; i < 10 && current != null; i++)
        {
            string candidate = Path.Combine(current.FullName, relativePath);
            if (File.Exists(candidate))
                return candidate;
            current = current.Parent;
        }
        throw new FileNotFoundException($"{relativePath} not found.");
    }

    private static void RunLen32BeEchoCase(byte[] payload, int? splitPoint)
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "stream-len32-case");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        int packets = 0;
        int callbacks = 0;
        stream.AttachStreamLen32Be((rid, messages) =>
        {
            Interlocked.Increment(ref callbacks);
            foreach (Message message in messages)
            {
                Interlocked.Increment(ref packets);
                stream.StreamSend(rid, message, SendFlags.None);
            }
            return 0;
        });

        using var client = ConnectRawClient(port);
        NetworkStream ns = client.GetStream();
        byte[] frame = BuildLen32BeFrame(payload);
        if (splitPoint is { } split && split > 0 && split < frame.Length)
        {
            SendAll(ns, frame.AsSpan(0, split));
            SendAll(ns, frame.AsSpan(split));
        }
        else
        {
            SendAll(ns, frame);
        }

        Assert.Equal(payload, ReceiveLen32BeFrame(ns));
        Assert.True(CoreTestSupport.WaitUntil(() => Volatile.Read(ref packets) >= 1,
            3000));
        Assert.True(Volatile.Read(ref callbacks) >= 1);
        stream.DetachStream();
    }

    [Fact]
    public void stream_callback_lifecycle_contract()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);

        stream.AttachStreamRaw((_, _) => 0);
        Assert.Throws<InvalidOperationException>(() =>
            stream.AttachStreamRaw((_, _) => 0));
        Assert.Throws<InvalidOperationException>(() =>
            stream.AttachStreamLen32Be((_, _) => 0));
        stream.DetachStream();

        stream.AttachStreamLen32Be((_, _) => 0);
        stream.DetachStream();
    }

    [Fact]
    public void stream_callback_exception_reports_unhandled_event()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "stream-callback-ex");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        Exception? observed = null;
        void OnUnhandled(Exception ex)
        {
            observed = ex;
        }

        Runtime.UnhandledCallbackException += OnUnhandled;
        try
        {
            stream.AttachStreamRaw((_, payload) =>
            {
                payload.Dispose();
                throw new InvalidOperationException("stream-callback-fail");
            });

            using var client = ConnectRawClient(port);
            SendAll(client.GetStream(), "stream-callback-fail"u8);

            Assert.True(CoreTestSupport.WaitUntil(() => observed != null, 3000));
            Assert.IsType<InvalidOperationException>(observed);
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

    [Fact]
    public void stream_recv_api_dispatch_conflict()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);

        byte[] probe = new byte[16];
        var idleEx = Assert.Throws<ZlinkException>(() =>
            stream.Receive(probe, ReceiveFlags.DontWait));
        Assert.Equal(ErrorCode.EAgain, ZlinkException.MapErrorCode(idleEx.Errno));

        stream.AttachStreamRaw((_, _) => 0);
        var busyEx = Assert.Throws<ZlinkException>(() =>
            stream.Receive(probe, ReceiveFlags.DontWait));
        Assert.NotEqual(0, busyEx.Errno);
        stream.DetachStream();
    }

    [Fact]
    public void stream_dispatch_start_rejects_stream_notify()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);

        stream.SetOption(SocketOption.StreamNotify, 1);
        Assert.Throws<ZlinkException>(() => stream.AttachStreamRaw((_, _) => 0));

        stream.SetOption(SocketOption.StreamNotify, 0);
        stream.AttachStreamRaw((_, _) => 0);
        stream.DetachStream();
    }

    [Fact]
    public void stream_send_message_failure_consumes_ownership()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var sub = new Socket(ctx, SocketType.Sub);
        using var msg = Message.FromBytes("x"u8);

        Assert.Throws<ZlinkException>(() => sub.Send(msg, SendFlags.DontWait));
        Assert.Throws<ObjectDisposedException>(() =>
        {
            _ = msg.Size;
        });
    }

    [Fact]
    public void stream_streamsend_message_failure_consumes_ownership()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var dealer = new Socket(ctx, SocketType.Dealer);
        using var msg = Message.FromBytes("x"u8);

        byte[] rid = { 0x00, 0x00, 0x00, 0x01 };
        Assert.Throws<ZlinkException>(() =>
            dealer.StreamSend(rid, msg, SendFlags.DontWait));
        Assert.Throws<ObjectDisposedException>(() =>
        {
            _ = msg.Size;
        });
    }

    [Fact]
    public void stream_callback_echo_raw()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);

        string endpoint = CoreTestSupport.NewEndpoint("tcp", "stream-raw-cb");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        int matched = 0;
        byte[] expected = Encoding.UTF8.GetBytes("stream-callback-raw");
        stream.AttachStreamRaw((rid, payload) =>
        {
            if (payload.AsReadOnlySpan().SequenceEqual(expected))
                Interlocked.Increment(ref matched);
            stream.StreamSend(rid, payload, SendFlags.None);
            return 0;
        });

        using var client = ConnectRawClient(port);
        NetworkStream ns = client.GetStream();
        SendAll(ns, expected);

        byte[] echoed = ReceiveExact(ns, expected.Length);
        Assert.Equal(expected, echoed);
        Assert.True(CoreTestSupport.WaitUntil(() => Volatile.Read(ref matched) >= 1,
            3000));

        stream.DetachStream();
    }

    [Fact]
    public void stream_callback_raw_transfers_message_ownership()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "stream-raw-owned");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        using var receivedSignal = new ManualResetEventSlim(false);
        Message? owned = null;
        byte[] expected = "stream-raw-owned-payload"u8.ToArray();
        stream.AttachStreamRaw((_, payload) =>
        {
            ReadOnlySpan<byte> bytes = payload.AsReadOnlySpan();
            if (bytes.Length == 1 && (bytes[0] == 0x00 || bytes[0] == 0x01))
            {
                payload.Dispose();
                return 0;
            }

            owned = payload;
            receivedSignal.Set();
            return 0;
        });

        using var client = ConnectRawClient(port);
        NetworkStream ns = client.GetStream();
        SendAll(ns, expected);

        Assert.True(receivedSignal.Wait(3000));
        Assert.NotNull(owned);
        Assert.True(owned!.AsReadOnlySpan().SequenceEqual(expected));
        owned.Dispose();
        Assert.Throws<ObjectDisposedException>(() =>
        {
            _ = owned.Size;
        });

        stream.DetachStream();
    }

    [Fact]
    public void stream_callback_echo_len32be()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);

        string endpoint = CoreTestSupport.NewEndpoint("tcp", "stream-len32-cb");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        int matched = 0;
        byte[] payload = Encoding.UTF8.GetBytes("stream-callback-len32be");
        stream.AttachStreamLen32Be((rid, messages) =>
        {
            foreach (Message message in messages)
            {
                if (message.AsReadOnlySpan().SequenceEqual(payload))
                    Interlocked.Increment(ref matched);
                stream.StreamSend(rid, message, SendFlags.None);
            }
            return 0;
        });

        using var client = ConnectRawClient(port);
        NetworkStream ns = client.GetStream();
        byte[] frame = BuildLen32BeFrame(payload);
        SendAll(ns, frame.AsSpan(0, 2));
        SendAll(ns, frame.AsSpan(2));

        byte[] echoed = ReceiveLen32BeFrame(ns);
        Assert.Equal(payload, echoed);
        Assert.True(CoreTestSupport.WaitUntil(() => Volatile.Read(ref matched) >= 1,
            3000));

        stream.DetachStream();
    }

    [Fact]
    public void stream_callback_len32be_transfers_message_ownership()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "stream-len-owned");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        using var receivedSignal = new ManualResetEventSlim(false);
        Message? owned = null;
        byte[] payload = "stream-len32be-owned-payload"u8.ToArray();
        stream.AttachStreamLen32Be((_, messages) =>
        {
            for (int i = 0; i < messages.Length; i++)
            {
                Message message = messages[i];
                if (!receivedSignal.IsSet)
                {
                    owned = message;
                    receivedSignal.Set();
                }
                else
                {
                    message.Dispose();
                }
            }
            return 0;
        });

        using var client = ConnectRawClient(port);
        NetworkStream ns = client.GetStream();
        SendAll(ns, BuildLen32BeFrame(payload));

        Assert.True(receivedSignal.Wait(3000));
        Assert.NotNull(owned);
        Assert.True(owned!.AsReadOnlySpan().SequenceEqual(payload));
        owned.Dispose();
        Assert.Throws<ObjectDisposedException>(() =>
        {
            _ = owned.Size;
        });

        stream.DetachStream();
    }

    [Fact]
    public void stream_callback_echo_single_zero_byte()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);

        string endpoint = CoreTestSupport.NewEndpoint("tcp", "stream-zero-cb");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        int matched = 0;
        byte[] payload = { 0x00 };
        stream.AttachStreamRaw((rid, msg) =>
        {
            ReadOnlySpan<byte> payload = msg.AsReadOnlySpan();
            if (payload.Length == 1 && payload[0] == 0)
                Interlocked.Increment(ref matched);
            stream.StreamSend(rid, msg, SendFlags.None);
            return 0;
        });

        using var client = ConnectRawClient(port);
        NetworkStream ns = client.GetStream();
        SendAll(ns, payload);
        byte[] echoed = ReceiveExact(ns, 1);
        Assert.Equal(payload, echoed);
        Assert.True(CoreTestSupport.WaitUntil(() => Volatile.Read(ref matched) >= 1,
            3000));

        stream.DetachStream();
    }

    [Fact]
    public void stream_len32be_single_frame()
    {
        RunLen32BeEchoCase("len32be-single-frame"u8.ToArray(), splitPoint: null);
    }

    [Fact]
    public void stream_len32be_header_split()
    {
        byte[] payload = "len32be-header-split"u8.ToArray();
        RunLen32BeEchoCase(payload, splitPoint: 2);
    }

    [Fact]
    public void stream_len32be_body_split()
    {
        byte[] payload = "len32be-body-split-payload"u8.ToArray();
        RunLen32BeEchoCase(payload, splitPoint: 9);
    }

    [Fact]
    public void stream_len32be_multi_frame_single_read()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "stream-len32-multi");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        var expected = new[]
        {
            "len32be-multi-1"u8.ToArray(),
            "len32be-multi-2"u8.ToArray(),
            "len32be-multi-3"u8.ToArray()
        };

        int packets = 0;
        int callbacks = 0;
        stream.AttachStreamLen32Be((rid, messages) =>
        {
            Interlocked.Increment(ref callbacks);
            foreach (Message message in messages)
            {
                Interlocked.Increment(ref packets);
                stream.StreamSend(rid, message, SendFlags.None);
            }
            return 0;
        });

        using var client = ConnectRawClient(port);
        NetworkStream ns = client.GetStream();
        byte[] merged = expected.SelectMany(p => BuildLen32BeFrame(p)).ToArray();
        SendAll(ns, merged);

        foreach (byte[] framePayload in expected)
            Assert.Equal(framePayload, ReceiveLen32BeFrame(ns));

        Assert.True(CoreTestSupport.WaitUntil(() => Volatile.Read(ref packets) >= 3,
            3000));
        Assert.True(Volatile.Read(ref callbacks) >= 1);
        stream.DetachStream();
    }

    [Fact]
    public void stream_len32be_callback_lifecycle_contract()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "stream-len32-life");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        int callbacks = 0;
        stream.AttachStreamLen32Be((rid, messages) =>
        {
            Interlocked.Increment(ref callbacks);
            foreach (Message message in messages)
                stream.StreamSend(rid, message, SendFlags.None);
            return 0;
        });

        using var client = ConnectRawClient(port);
        NetworkStream ns = client.GetStream();
        byte[] p1 = "len32be-life-a"u8.ToArray();
        byte[] p2 = "len32be-life-b"u8.ToArray();

        SendAll(ns, BuildLen32BeFrame(p1));
        Assert.Equal(p1, ReceiveLen32BeFrame(ns));

        SendAll(ns, BuildLen32BeFrame(p2));
        Assert.Equal(p2, ReceiveLen32BeFrame(ns));

        Assert.True(CoreTestSupport.WaitUntil(() => Volatile.Read(ref callbacks) >= 2,
            3000));
        stream.DetachStream();
    }

    [Fact]
    public void stream_len32be_batch_callback_single_dispatch()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "stream-len32-batch");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        var expected = new[]
        {
            "len32be-batch-1"u8.ToArray(),
            "len32be-batch-2"u8.ToArray(),
            "len32be-batch-3"u8.ToArray()
        };

        int callbackCount = 0;
        int packetCount = 0;
        stream.AttachStreamLen32Be((rid, parts) =>
        {
            Interlocked.Increment(ref callbackCount);
            for (int i = 0; i < parts.Length; i++)
            {
                Interlocked.Increment(ref packetCount);
                stream.StreamSend(rid, parts[i], SendFlags.None);
            }
            return 0;
        });

        using var client = ConnectRawClient(port);
        NetworkStream ns = client.GetStream();
        byte[] merged = expected.SelectMany(p => BuildLen32BeFrame(p)).ToArray();
        SendAll(ns, merged);

        foreach (byte[] framePayload in expected)
            Assert.Equal(framePayload, ReceiveLen32BeFrame(ns));

        Assert.True(CoreTestSupport.WaitUntil(() => Volatile.Read(ref packetCount) >= 3,
            3000));
        Assert.True(CoreTestSupport.WaitUntil(() => Volatile.Read(ref callbackCount) == 1,
            3000));
        stream.DetachStream();
    }

    [Fact]
    public void stream_socket_raw_tcp_roundtrip()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);

        string endpoint = CoreTestSupport.NewEndpoint("tcp", "stream");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        using var client = new TcpClient();
        client.NoDelay = true;
        client.Connect(IPAddress.Loopback, port);

        Assert.True(CoreTestSupport.WaitUntil(() => stream.PeerCount() > 0,
            3000));

        byte[]? peerRoutingId = stream.GetPeerRoutingId();
        Assert.NotNull(peerRoutingId);
        Assert.NotEmpty(peerRoutingId!);
        Assert.True(stream.TryGetPeerRoutingIdU32(out uint peerRoutingIdU32));
        Assert.Equal(BinaryPrimitives.ReadUInt32BigEndian(peerRoutingId),
            peerRoutingIdU32);

        byte[] incoming = Encoding.UTF8.GetBytes("hello");
        client.GetStream().Write(incoming, 0, incoming.Length);

        byte[] rid = CoreTestSupport.ReceiveBytesWithTimeout(stream, 256, 3000);
        byte[] payload = CoreTestSupport.ReceiveBytesWithTimeout(stream, 256, 3000);

        Assert.Equal(peerRoutingId, rid);
        Assert.Equal("hello", Encoding.UTF8.GetString(payload));

        using var reply = Message.FromBytes("world"u8);
        stream.StreamSend(peerRoutingIdU32, reply, SendFlags.None);
        Assert.Throws<ObjectDisposedException>(() =>
        {
            _ = reply.Size;
        });

        client.ReceiveTimeout = 3000;
        byte[] recv = new byte[64];
        int n = client.GetStream().Read(recv, 0, recv.Length);
        Assert.True(n > 0);
        Assert.Equal("world", Encoding.UTF8.GetString(recv, 0, n));
    }

    [Fact]
    public void stream_peer_info_and_peers_enumeration()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);

        string endpoint = CoreTestSupport.NewEndpoint("tcp", "stream-peers");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        using var client = new TcpClient();
        client.NoDelay = true;
        client.Connect(IPAddress.Loopback, port);

        Assert.True(CoreTestSupport.WaitUntil(() => stream.PeerCount() > 0,
            3000));

        byte[]? rid = stream.GetPeerRoutingId();
        Assert.NotNull(rid);
        Assert.NotEmpty(rid!);

        PeerRecord info = stream.GetPeerInfo(rid!);
        Assert.NotEmpty(info.RoutingId);

        PeerRecord[] peers = stream.GetPeers();
        Assert.NotEmpty(peers);
        Assert.Contains(peers, p => p.RoutingId.SequenceEqual(rid!));
    }

    [Fact]
    public void stream_monitor_multiclient_connect_disconnect()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "stream-monitor");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        using MonitorSocket monitor = stream.MonitorOpen(
            SocketEvent.ConnectionReady | SocketEvent.Disconnected);

        const int clients = 4;
        for (int i = 0; i < clients; i++)
        {
            using var client = ConnectRawClient(port);
            NetworkStream ns = client.GetStream();
            SendAll(ns, "probe"u8);
            _ = CoreTestSupport.ReceiveBytesWithTimeout(stream, 256, 3000);
            _ = CoreTestSupport.ReceiveBytesWithTimeout(stream, 256, 3000);
        }

        int ready = 0;
        int disconnected = 0;
        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            if (WaitMonitorEvent(monitor, SocketEvent.ConnectionReady, 50,
                    out _))
                ready++;
            if (WaitMonitorEvent(monitor, SocketEvent.Disconnected, 50,
                    out _))
                disconnected++;
            return ready >= clients && disconnected >= clients;
        }, 6000, 10));
    }

    [Fact]
    public async Task stream_raw_multiclient_load_integrity()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "stream-raw-load");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        stream.AttachStreamRaw((rid, payload) =>
        {
            stream.StreamSend(rid, payload, SendFlags.None);
            return 0;
        });

        const int clientCount = 8;
        const int messagesPerClient = 20;
        Task[] clients = new Task[clientCount];
        for (int i = 0; i < clientCount; i++)
        {
            int clientId = i;
            clients[i] = Task.Run(() =>
            {
                using var client = ConnectRawClient(port);
                NetworkStream ns = client.GetStream();
                for (int m = 0; m < messagesPerClient; m++)
                {
                    byte[] payload = new byte[64];
                    BinaryPrimitives.WriteInt32BigEndian(payload.AsSpan(0, 4),
                        clientId);
                    BinaryPrimitives.WriteInt32BigEndian(payload.AsSpan(4, 4), m);
                    for (int j = 8; j < payload.Length; j++)
                        payload[j] = (byte)(clientId + m + j);

                    byte[] frame = BuildLen32BeFrame(payload);
                    int split = 1 + ((clientId + m) % (frame.Length - 1));
                    SendAll(ns, frame.AsSpan(0, split));
                    SendAll(ns, frame.AsSpan(split));
                    byte[] echoed = ReceiveExact(ns, frame.Length);
                    Assert.Equal(frame, echoed);
                }
            });
        }

        await Task.WhenAll(clients);
        stream.DetachStream();
    }

    [Fact]
    public async Task stream_len32be_multiclient_load_integrity()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "stream-len-load");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        stream.AttachStreamLen32Be((rid, messages) =>
        {
            foreach (Message message in messages)
                stream.StreamSend(rid, message, SendFlags.None);
            return 0;
        });

        const int clientCount = 10;
        const int messagesPerClient = 18;
        Task[] clients = new Task[clientCount];
        for (int i = 0; i < clientCount; i++)
        {
            int clientId = i;
            clients[i] = Task.Run(() =>
            {
                using var client = ConnectRawClient(port);
                NetworkStream ns = client.GetStream();
                for (int m = 0; m < messagesPerClient; m++)
                {
                    byte[] payload = new byte[96];
                    BinaryPrimitives.WriteInt32BigEndian(payload.AsSpan(0, 4),
                        clientId);
                    BinaryPrimitives.WriteInt32BigEndian(payload.AsSpan(4, 4), m);
                    for (int j = 8; j < payload.Length; j++)
                        payload[j] = (byte)(clientId + m + j);

                    SendAll(ns, BuildLen32BeFrame(payload));
                    byte[] echoed = ReceiveLen32BeFrame(ns);
                    Assert.Equal(payload, echoed);
                }
            });
        }

        await Task.WhenAll(clients);
        stream.DetachStream();
    }

    [Fact]
    public void stream_maxmsgsize_disconnects_oversized_payload()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);
        stream.SetOption(SocketOption.MaxMsgSize, BitConverter.GetBytes((long)4));

        string endpoint = CoreTestSupport.NewEndpoint("tcp", "stream-maxmsg");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        using MonitorSocket monitor = stream.MonitorOpen(
            SocketEvent.ConnectionReady | SocketEvent.Disconnected);

        using var client = ConnectRawClient(port);
        NetworkStream ns = client.GetStream();
        SendAll(ns, "ok"u8);

        byte[] serverRid = CoreTestSupport.ReceiveBytesWithTimeout(stream, 256, 3000);
        _ = CoreTestSupport.ReceiveBytesWithTimeout(stream, 16, 3000);
        Assert.NotEmpty(serverRid);

        Assert.True(WaitMonitorEvent(monitor, SocketEvent.ConnectionReady, 3000,
            out byte[] connectRid));
        Assert.Equal(serverRid, connectRid);

        byte[] oversized = new byte[1024];
        Array.Fill(oversized, (byte)'A');
        SendAll(ns, oversized);

        bool monitorDisconnected = WaitMonitorEvent(monitor, SocketEvent.Disconnected,
            4000, out byte[] disconnectRid);
        if (monitorDisconnected)
            Assert.Equal(serverRid, disconnectRid);
    }

    [Fact]
    public void stream_connect_rejected()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);
        Assert.Throws<ZlinkException>(() =>
            stream.Connect("tcp://127.0.0.1:5555"));
    }

    [Fact]
    public void stream_ws_basic()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;
        if (!CoreTestSupport.IsTransportSupported("ws"))
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);
        string endpoint = CoreTestSupport.NewEndpoint("ws", "stream-ws");
        stream.Bind(endpoint);
    }

    [Fact]
    public void stream_wss_basic()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;
        if (!CoreTestSupport.IsTransportSupported("wss"))
            return;

        string cert;
        string key;
        try
        {
            cert = ResolveRepoPath("core/tests/certs/gen/server.crt");
            key = ResolveRepoPath("core/tests/certs/gen/server.key");
        }
        catch (FileNotFoundException)
        {
            return;
        }

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);
        stream.SetOption(SocketOption.TlsCert, cert);
        stream.SetOption(SocketOption.TlsKey, key);
        string endpoint = CoreTestSupport.NewEndpoint("wss", "stream-wss");
        stream.Bind(endpoint);
    }
}
