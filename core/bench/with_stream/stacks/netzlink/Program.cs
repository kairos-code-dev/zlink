using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Zlink;

namespace NetZlinkStreamServer;

internal sealed class ServerOptions
{
    public string Host { get; set; } = "0.0.0.0";
    public int Port { get; set; } = 38007;
    public int Size { get; set; } = 1024;
    public int SndBuf { get; set; } = 1024 * 1024;
    public int RcvBuf { get; set; } = 1024 * 1024;
    public int Backlog { get; set; } = 32768;
    public int TcpNoDelay { get; set; } = 1;
    public int IoThreads { get; set; } = 4;

    public const int MinPayloadSize = 16;
    public const int MaxPayloadSize = 4 * 1024 * 1024;

    public static bool TryParse(string[] args, out ServerOptions options)
    {
        options = new ServerOptions();
        for (int i = 0; i < args.Length; i++)
        {
            string key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
                continue;
            if ((i + 1) >= args.Length)
                break;

            string value = args[++i];
            switch (key)
            {
                case "--host":
                    options.Host = value;
                    break;
                case "--port":
                    options.Port = ParseInt(value, options.Port, 1);
                    break;
                case "--size":
                    options.Size = ParseInt(value, options.Size, MinPayloadSize);
                    break;
                case "--sndbuf":
                    options.SndBuf = ParseInt(value, options.SndBuf, 1);
                    break;
                case "--rcvbuf":
                    options.RcvBuf = ParseInt(value, options.RcvBuf, 1);
                    break;
                case "--backlog":
                    options.Backlog = ParseInt(value, options.Backlog, 1);
                    break;
                case "--tcp-nodelay":
                    options.TcpNoDelay = ParseInt(value, options.TcpNoDelay, 0);
                    break;
                case "--io-threads":
                    options.IoThreads = ParseInt(value, options.IoThreads, 1);
                    break;
            }
        }

        if (options.Size > MaxPayloadSize)
        {
            Console.Error.WriteLine($"netzlink stream: size too large {options.Size}");
            return false;
        }

        return true;
    }

    private static int ParseInt(string text, int fallback, int min)
    {
        if (!int.TryParse(text, out int parsed))
            return fallback;
        if (parsed < min)
            return min;
        return parsed;
    }
}

internal sealed class Metrics
{
    private long _recvMsgs;
    private long _parseError;
    private long _protocolError;
    private long _sendError;

    public long RecvMsgs => Interlocked.Read(ref _recvMsgs);
    public long ParseError => Interlocked.Read(ref _parseError);
    public long ProtocolError => Interlocked.Read(ref _protocolError);
    public long SendError => Interlocked.Read(ref _sendError);

    public void AddRecvMsg() => Interlocked.Increment(ref _recvMsgs);
    public void AddParseError() => Interlocked.Increment(ref _parseError);
    public void AddProtocolError() => Interlocked.Increment(ref _protocolError);
    public void AddSendError() => Interlocked.Increment(ref _sendError);
}

internal sealed class StreamEchoServer : IDisposable
{
    private const byte StreamEventConnect = 0x01;
    private const byte StreamEventDisconnect = 0x00;
    private const int PrefixSize = 6;
    private static readonly byte[] MessageName = Encoding.ASCII.GetBytes("stream.echo");
    private readonly StreamSocket _socket;
    private readonly Metrics _metrics;
    private readonly ConcurrentDictionary<string, FrameBuffer> _buffers = new();
    private bool _attached;

    private sealed class FrameBuffer
    {
        private byte[] _bytes = new byte[4096];
        public int Count;
        public int Consumed;
        public object Sync { get; } = new object();

        public void Clear()
        {
            Count = 0;
            Consumed = 0;
        }

        public void Append(ReadOnlySpan<byte> chunk)
        {
            if (chunk.IsEmpty)
                return;

            int needed = Count + chunk.Length;
            if (needed > _bytes.Length)
            {
                int next = _bytes.Length;
                while (next < needed)
                    next *= 2;
                Array.Resize(ref _bytes, next);
            }

            chunk.CopyTo(_bytes.AsSpan(Count));
            Count = needed;
        }

        public ReadOnlySpan<byte> Span => _bytes.AsSpan(0, Count);

        public void Compact()
        {
            if (Consumed <= 0)
                return;

            if (Consumed >= Count)
            {
                Clear();
                return;
            }

            int remaining = Count - Consumed;
            if (Consumed < 4096 && (Consumed * 2) < Count)
                return;

            Buffer.BlockCopy(_bytes, Consumed, _bytes, 0, remaining);
            Count = remaining;
            Consumed = 0;
        }

        public void CopyTo(int sourceOffset, byte[] destination, int destinationOffset, int count)
        {
            Buffer.BlockCopy(_bytes, sourceOffset, destination, destinationOffset, count);
        }
    }

    internal StreamEchoServer(StreamSocket socket, Metrics metrics)
    {
        _socket = socket ?? throw new ArgumentNullException(nameof(socket));
        _metrics = metrics ?? throw new ArgumentNullException(nameof(metrics));
    }

    internal void Attach()
    {
        if (_attached)
            return;
        _socket.OnPacket(OnRawPacket);
        _attached = true;
    }

    private int OnRawPacket(string routingId, Message payload)
    {
        try
        {
            int payloadSize = payload.Size;
            if (payloadSize <= 0)
                return 0;

            ReadOnlySpan<byte> chunk = payload.AsReadOnlySpan();
            if (chunk.Length != payloadSize)
            {
                _metrics.AddParseError();
                _metrics.AddProtocolError();
                return 0;
            }

            if (chunk.Length == 1
                && (chunk[0] == StreamEventConnect
                    || chunk[0] == StreamEventDisconnect)) {
                if (chunk[0] == StreamEventDisconnect)
                    _buffers.TryRemove(routingId, out _);
                return 0;
            }

            FrameBuffer state = _buffers.GetOrAdd(routingId, static _ => new FrameBuffer());
            lock (state.Sync)
            {
                state.Append(chunk);

                ReadOnlySpan<byte> buffer = state.Span;
                while (true)
                {
                    int available = state.Count - state.Consumed;
                    if (available < PrefixSize)
                        break;

                    int consumed = state.Consumed;
                    int headerSize = (buffer[consumed] << 8) | buffer[consumed + 1];
                    int bodySize = (buffer[consumed + 2] << 24)
                                   | (buffer[consumed + 3] << 16)
                                   | (buffer[consumed + 4] << 8)
                                   | buffer[consumed + 5];
                    if (headerSize != MessageName.Length
                        || bodySize < ServerOptions.MinPayloadSize
                        || bodySize > ServerOptions.MaxPayloadSize)
                    {
                        _metrics.AddParseError();
                        _metrics.AddProtocolError();
                        state.Clear();
                        return 0;
                    }

                    int total = PrefixSize + headerSize + bodySize;
                    if (available < total)
                        break;

                    bool headerMatch =
                        buffer.Slice(consumed + PrefixSize, MessageName.Length)
                            .SequenceEqual(MessageName);
                    if (!headerMatch)
                    {
                        _metrics.AddParseError();
                        _metrics.AddProtocolError();
                        state.Clear();
                        return 0;
                    }

                    _metrics.AddRecvMsg();
                    byte[] replyBytes = new byte[total];
                    state.CopyTo(consumed, replyBytes, 0, total);
                    using Message reply = Message.FromBytes(replyBytes);
                    _socket.Send(routingId, reply);
                    state.Consumed += total;
                }

                state.Compact();
            }
            return 0;
        }
        catch
        {
            _metrics.AddSendError();
            return 0;
        }
        finally
        {
            payload.Dispose();
        }
    }

    public void Dispose()
    {
        if (!_attached)
            return;
        try
        {
            _socket.DetachStream();
        }
        finally
        {
            _attached = false;
        }
    }
}

internal static class Program
{

    private static string Endpoint(string host, int port) => $"tcp://{host}:{port}";

    private static void ApplySocketTuning(StreamSocket socket,
                                                 ServerOptions options)
    {
        socket.Options.SendBufferSize = options.SndBuf;
        socket.Options.ReceiveBufferSize = options.RcvBuf;
        socket.Options.TcpNoDelay = options.TcpNoDelay != 0;
        socket.Options.SendHighWaterMark = 100;
        socket.Options.ReceiveHighWaterMark = 100;
    }

    public static int Main(string[] args)
    {
        if (args.Length == 0)
        {
            Console.WriteLine("test_scenario_stream_netzlink: no args -> skip");
            return 0;
        }

        if (!ServerOptions.TryParse(args, out ServerOptions options))
            return 2;

        int rc = 0;
        Metrics metrics = new();
        using CancellationTokenSource cts = new();
        PosixSignalRegistration? sigIntReg = null;
        PosixSignalRegistration? sigTermReg = null;

        void RequestCancel()
        {
            if (!cts.IsCancellationRequested)
                cts.Cancel();
        }

        ConsoleCancelEventHandler cancelHandler = (_, e) =>
        {
            e.Cancel = true;
            RequestCancel();
        };
        EventHandler processExitHandler = (_, _) => RequestCancel();
        Console.CancelKeyPress += cancelHandler;
        AppDomain.CurrentDomain.ProcessExit += processExitHandler;

        try
        {
            if (OperatingSystem.IsLinux() || OperatingSystem.IsMacOS())
            {
                sigIntReg = PosixSignalRegistration.Create(PosixSignal.SIGINT, context =>
                {
                    context.Cancel = true;
                    RequestCancel();
                });
                sigTermReg = PosixSignalRegistration.Create(PosixSignal.SIGTERM, context =>
                {
                    context.Cancel = true;
                    RequestCancel();
                });
            }
        }
        catch
        {
        }

        try
        {
            using Context ctx = new();
            ctx.Options.IoThreads = options.IoThreads;
            using StreamSocket server = new(ctx);
            ApplySocketTuning(server, options);
            server.Bind(Endpoint(options.Host, options.Port));
            using StreamEchoServer echo = new(server, metrics);
            echo.Attach();

            while (!cts.IsCancellationRequested)
                Thread.Sleep(200);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"netzlink stream: {ex.Message}");
            rc = 2;
        }
        finally
        {
            Console.WriteLine(
                $"METRIC stack=netzlink mode=echo size={options.Size} recv_msgs={metrics.RecvMsgs} " +
                $"parse_error={metrics.ParseError} protocol_error={metrics.ProtocolError} " +
                $"send_error={metrics.SendError} connections=0");

            Console.CancelKeyPress -= cancelHandler;
            AppDomain.CurrentDomain.ProcessExit -= processExitHandler;
            sigIntReg?.Dispose();
            sigTermReg?.Dispose();
        }

        return rc;
    }
}
