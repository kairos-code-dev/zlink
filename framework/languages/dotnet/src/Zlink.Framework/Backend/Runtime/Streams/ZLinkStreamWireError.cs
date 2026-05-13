namespace Zlink.Framework.Runtime.Streams;

internal sealed record ZLinkStreamWireError(
    string? Code,
    string? Message);
