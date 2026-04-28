namespace Zlink.Framework.Streams;

public interface IZLinkStream
{
    string SessionId { get; }

    global::Zlink.RoutingId? RoutingId { get; }

    string? LocalAddr { get; }

    string? RemoteAddr { get; }

    bool Write(
        global::Zlink.Message payload,
        global::Zlink.SendFlags flags = global::Zlink.SendFlags.None);

    bool Write(
        global::Zlink.Message header,
        global::Zlink.Message body,
        global::Zlink.SendFlags flags = global::Zlink.SendFlags.None);
}
