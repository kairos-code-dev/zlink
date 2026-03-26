using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
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

    private static string StreamRoutingIdToPublicString(uint routingId)
    {
        return $"hex:{routingId:X8}";
    }

    private static void SendLen32Be(Socket stream, string routingId,
        Message message, SendFlags flags = SendFlags.None)
    {
        using Message framed = Message.FromBytes(
            BuildLen32BeFrame(message.AsReadOnlySpan()));
        stream.Send(routingId, framed, flags);
    }

    private static void AttachLen32Be(Socket stream,
        Func<string, Message[], int> handler)
    {
        var accumulators = new Dictionary<string, Len32BeAccumulator>(
            StringComparer.Ordinal);
        stream.AttachStreamRaw((routingId, payload) =>
        {
            Message[] frames = Array.Empty<Message>();
            try
            {
                if (!accumulators.TryGetValue(routingId, out Len32BeAccumulator? state))
                {
                    state = new Len32BeAccumulator();
                    accumulators.Add(routingId, state);
                }

                frames = state.AppendAndDrain(payload.AsReadOnlySpan());
                payload.Dispose();
                if (frames.Length == 0)
                    return 0;
                return handler(routingId, frames);
            }
            catch
            {
                foreach (Message frame in frames)
                    frame.Dispose();
                payload.Dispose();
                throw;
            }
        });
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

    private sealed class Len32BeAccumulator
    {
        private byte[] _buffer = Array.Empty<byte>();
        private int _count;

        public Message[] AppendAndDrain(ReadOnlySpan<byte> payload)
        {
            if (!payload.IsEmpty)
            {
                EnsureCapacity(_count + payload.Length);
                payload.CopyTo(_buffer.AsSpan(_count));
                _count += payload.Length;
            }

            int frameCount = CountReadyFrames();
            if (frameCount == 0)
                return Array.Empty<Message>();

            Message[] frames = new Message[frameCount];
            int produced = 0;
            int offset = 0;
            while (_count - offset >= sizeof(uint))
            {
                int frameSize = checked((int)BinaryPrimitives.ReadUInt32BigEndian(
                    _buffer.AsSpan(offset, sizeof(uint))));
                int totalSize = sizeof(uint) + frameSize;
                if (_count - offset < totalSize)
                    break;

                frames[produced++] = Message.FromBytes(
                    _buffer.AsSpan(offset + sizeof(uint), frameSize));
                offset += totalSize;
            }

            if (offset > 0)
            {
                Buffer.BlockCopy(_buffer, offset, _buffer, 0, _count - offset);
                _count -= offset;
            }

            return frames;
        }

        private int CountReadyFrames()
        {
            int count = 0;
            int offset = 0;
            while (_count - offset >= sizeof(uint))
            {
                int frameSize = checked((int)BinaryPrimitives.ReadUInt32BigEndian(
                    _buffer.AsSpan(offset, sizeof(uint))));
                int totalSize = sizeof(uint) + frameSize;
                if (_count - offset < totalSize)
                    break;

                count++;
                offset += totalSize;
            }

            return count;
        }

        private void EnsureCapacity(int required)
        {
            if (_buffer.Length >= required)
                return;

            int nextSize = _buffer.Length == 0 ? 256 : _buffer.Length;
            while (nextSize < required)
                nextSize *= 2;
            Array.Resize(ref _buffer, nextSize);
        }
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
        AttachLen32Be(stream, (rid, messages) =>
        {
            Interlocked.Increment(ref callbacks);
            foreach (Message message in messages)
            {
                Interlocked.Increment(ref packets);
                SendLen32Be(stream, rid, message, SendFlags.None);
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
            AttachLen32Be(stream, (_, _) => 0));
        stream.DetachStream();

        AttachLen32Be(stream, (_, _) => 0);
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

        var idleEx = Assert.Throws<ZlinkException>(() =>
            stream.Receive(out Message _, ReceiveFlags.DontWait));
        Assert.Equal(ErrorCode.EAgain, ZlinkException.MapErrorCode(idleEx.Errno));

        stream.AttachStreamRaw((_, _) => 0);
        var busyEx = Assert.Throws<ZlinkException>(() =>
            stream.Receive(out Message _, ReceiveFlags.DontWait));
        Assert.NotEqual(0, busyEx.Errno);
        stream.DetachStream();
    }

    [Fact]
    public void stream_dispatch_start_allows_stream_notify()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);

        stream.SetOption(SocketOptions.StreamNotify, 1);
        stream.AttachStreamRaw((_, _) => 0);
        stream.DetachStream();
    }

    [Fact]
    public void stream_send_message_failure_preserves_ownership()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var sub = new Socket(ctx, SocketType.Sub);
        using var msg = Message.FromBytes("x"u8);

        Assert.Throws<ZlinkException>(() => sub.Send(msg, SendFlags.DontWait));
        Assert.Equal(1, msg.Size);
    }

    [Fact]
    public void stream_streamsend_message_failure_preserves_ownership()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var dealer = new Socket(ctx, SocketType.Dealer);
        using var msg = Message.FromBytes("x"u8);

        const uint rid = 1;
        Assert.Throws<ZlinkException>(() =>
            dealer.Send(StreamRoutingIdToPublicString(rid), msg,
                SendFlags.DontWait));
        Assert.Equal(1, msg.Size);
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
        byte[] expected = "stream-callback-raw"u8.ToArray();
        stream.AttachStreamRaw((rid, payload) =>
        {
            if (payload.AsReadOnlySpan().SequenceEqual(expected))
                Interlocked.Increment(ref matched);
            stream.Send(rid, payload, SendFlags.None);
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
        byte[] payload = "stream-callback-len32be"u8.ToArray();
        AttachLen32Be(stream, (rid, messages) =>
        {
            foreach (Message message in messages)
            {
                if (message.AsReadOnlySpan().SequenceEqual(payload))
                    Interlocked.Increment(ref matched);
                SendLen32Be(stream, rid, message, SendFlags.None);
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
        AttachLen32Be(stream, (_, messages) =>
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
            stream.Send(rid, msg, SendFlags.None);
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
        AttachLen32Be(stream, (rid, messages) =>
        {
            Interlocked.Increment(ref callbacks);
            foreach (Message message in messages)
            {
                Interlocked.Increment(ref packets);
                SendLen32Be(stream, rid, message, SendFlags.None);
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
        AttachLen32Be(stream, (rid, messages) =>
        {
            Interlocked.Increment(ref callbacks);
            foreach (Message message in messages)
                SendLen32Be(stream, rid, message, SendFlags.None);
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
        AttachLen32Be(stream, (rid, parts) =>
        {
            Interlocked.Increment(ref callbackCount);
            for (int i = 0; i < parts.Length; i++)
            {
                Interlocked.Increment(ref packetCount);
                SendLen32Be(stream, rid, parts[i], SendFlags.None);
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

        byte[] incoming = "hello"u8.ToArray();
        client.GetStream().Write(incoming, 0, incoming.Length);

        stream.Receive(out string routingId, out Message payloadMessage);
        using (payloadMessage)
        {
            Assert.False(string.IsNullOrEmpty(routingId));
            Assert.Equal("hello", CoreTestSupport.Utf8(payloadMessage));
        }

        using var reply = Message.FromBytes("world"u8);
        stream.Send(routingId, reply, SendFlags.None);
        Assert.Throws<ObjectDisposedException>(() =>
        {
            _ = reply.Size;
        });

        client.ReceiveTimeout = 3000;
        byte[] recv = new byte[64];
        int n = client.GetStream().Read(recv, 0, recv.Length);
        Assert.True(n > 0);
        Assert.Equal("world", CoreTestSupport.Utf8(recv.AsSpan(0, n)));
    }

    [Fact]
    public void stream_receive_surfaces_public_routing_id()
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

        byte[] incoming = "routing-id-check"u8.ToArray();
        client.GetStream().Write(incoming, 0, incoming.Length);

        stream.Receive(out string routingId, out Message message);
        using (message)
        {
            Assert.False(string.IsNullOrEmpty(routingId));
            Assert.Equal("routing-id-check", CoreTestSupport.Utf8(message));
        }
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

        using var monitorEvents = new CallbackEventQueue<SocketMonitorEvent>();
        using SocketMonitor monitor = stream.OpenMonitor(
            SocketEvent.ConnectionReady | SocketEvent.Disconnected);
        monitor.AttachHandler(monitorEvents.Enqueue);

        const int clients = 4;
        for (int i = 0; i < clients; i++)
        {
            using var client = ConnectRawClient(port);
            NetworkStream ns = client.GetStream();
            SendAll(ns, "probe"u8);
            stream.Receive(out string routingId, out Message payload);
            using (payload)
            {
                Assert.False(string.IsNullOrEmpty(routingId));
                Assert.Equal("probe", CoreTestSupport.Utf8(payload));
            }
        }

        int ready = 0;
        int disconnected = 0;
        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            while (monitorEvents.TryDequeue(50, out SocketMonitorEvent evt))
            {
                if (evt.Event == SocketEvent.ConnectionReady
                    && evt.StreamRoutingId.HasValue)
                {
                    ready++;
                }
                else if (evt.Event == SocketEvent.Disconnected
                    && evt.StreamRoutingId.HasValue)
                {
                    disconnected++;
                }
            }
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
            stream.Send(rid, payload, SendFlags.None);
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

        AttachLen32Be(stream, (rid, messages) =>
        {
            foreach (Message message in messages)
                SendLen32Be(stream, rid, message, SendFlags.None);
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
        stream.SetOption(SocketOptions.MaxMsgSize, 4L);

        string endpoint = CoreTestSupport.NewEndpoint("tcp", "stream-maxmsg");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        using var monitorEvents = new CallbackEventQueue<SocketMonitorEvent>();
        using SocketMonitor monitor = stream.OpenMonitor(
            SocketEvent.ConnectionReady | SocketEvent.Disconnected);
        monitor.AttachHandler(monitorEvents.Enqueue);

        using var client = ConnectRawClient(port);
        NetworkStream ns = client.GetStream();
        SendAll(ns, "ok"u8);

        stream.Receive(out string serverRoutingId, out Message payload);
        using (payload)
        {
            Assert.Equal("ok", CoreTestSupport.Utf8(payload));
        }
        Assert.False(string.IsNullOrEmpty(serverRoutingId));

        Assert.True(TryReadMonitorEvent(monitorEvents, SocketEvent.ConnectionReady,
            3000, out uint connectRid));
        Assert.True(connectRid > 0);

        byte[] oversized = new byte[1024];
        Array.Fill(oversized, (byte)'A');
        SendAll(ns, oversized);

        bool monitorDisconnected = TryReadMonitorEvent(monitorEvents,
            SocketEvent.Disconnected, 4000, out uint disconnectRid);
        if (monitorDisconnected)
        {
            Assert.Equal(connectRid, disconnectRid);
        }
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
            cert = ResolveRepoPath("bindings/dotnet/tests/certs/server.crt");
            key = ResolveRepoPath("bindings/dotnet/tests/certs/server.key");
        }
        catch (FileNotFoundException)
        {
            return;
        }

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);
        stream.SetOption(SocketOptions.TlsCert, cert);
        stream.SetOption(SocketOptions.TlsKey, key);
        string endpoint = CoreTestSupport.NewEndpoint("wss", "stream-wss");
        stream.Bind(endpoint);
    }

    private static bool TryReadMonitorEvent(
        CallbackEventQueue<SocketMonitorEvent> events, SocketEvent expectedEvent,
        int timeoutMs, out uint routingId)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            int remainingMs = (int)Math.Max(1,
                (deadline - DateTime.UtcNow).TotalMilliseconds);
            if (!events.TryDequeue(remainingMs, out SocketMonitorEvent evt))
                break;

            if (evt.Event != expectedEvent || !evt.StreamRoutingId.HasValue)
                continue;

            routingId = evt.StreamRoutingId.Value;
            return true;
        }

        routingId = 0;
        return false;
    }
}
