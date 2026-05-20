using System.Net;
using System.Net.Sockets;

namespace Bingo.SessionGateway.Infrastructure;

public static class EphemeralTcpEndpoint
{
    public static string Create()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        return $"tcp://127.0.0.1:{endpoint.Port}";
    }
}
