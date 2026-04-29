using System.Buffers.Binary;
using System.Collections.Concurrent;

using System.Net.Security;

using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Authentication;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using System.Threading.Channels;

using K4os.Compression.LZ4;

namespace Systems.Zlink.Stream.Connector.Protocol;

public sealed class ZlinkStreamMessageNameResolver : IZlinkStreamMessageNameResolver
{
    public string Resolve(Type bodyType)
    {
        ArgumentNullException.ThrowIfNull(bodyType);
        var attribute = bodyType.GetCustomAttributes(typeof(ZlinkStreamMessageNameAttribute), false)
            .OfType<ZlinkStreamMessageNameAttribute>()
            .FirstOrDefault();
        return attribute?.Name ?? bodyType.Name;
    }
}
