using Systems.Zlink;
using System.Text.Json;

var version = Zlink.Version();
if (version != (11, 0, 0))
    throw new InvalidOperationException($"Expected Core 11.0.0, loaded {version}.");

var loadedRuntime = File.ReadLines("/proc/self/maps")
    .Select(line => line.Split(' ', StringSplitOptions.RemoveEmptyEntries).LastOrDefault())
    .FirstOrDefault(path => path?.Contains("libzlink.so", StringComparison.Ordinal) == true);
if (loadedRuntime is null)
    throw new InvalidOperationException("The loaded Core runtime was not visible in /proc/self/maps.");

Func<IPairSocket, SendOperation> multipartSend = static socket => socket.Send();
Func<SendOperation, Message, SendSubmitOperation> appendPart =
    static (operation, part) => operation.Message(part);
Func<Received, IReadOnlyList<Message>> multipartReceive =
    static received => received.Parts;

Func<ISocket, ISocketMonitor> monitorOpen = static socket => socket.MonitorOpen();
Func<ISocketMonitor, MonitorStatus> monitorStatus = static monitor => monitor.Status();
Func<MonitorStatus, bool> monitorReady = static status => status.IsReady;

Func<IStreamSocket, RoutingId, SendOperation> streamSend =
    static (socket, routingId) => socket.Send(routingId);
Func<IStreamSocket, Received, bool> streamReceive =
    static (socket, received) => socket.Recv(received);
Action<IStreamSocket, StreamPacketHandler> streamDispatch =
    static (socket, handler) => socket.OnPacket(handler);

Action<IContext> shutdown = static context => context.Shutdown();
Action<ISocket> socketClose = static socket => socket.Close();
Action<ISocketMonitor> monitorClose = static monitor => monitor.Close();

_ = new Delegate[]
{
    multipartSend, appendPart, multipartReceive,
    monitorOpen, monitorStatus, monitorReady,
    streamSend, streamReceive, streamDispatch,
    shutdown, socketClose, monitorClose
};

Console.WriteLine(JsonSerializer.Serialize(new
{
    version = $"{version.Major}.{version.Minor}.{version.Patch}",
    loadedRuntime
}));
