namespace Zlink.Framework;

public readonly record struct ZLinkStreamDiagnostic(
    int NativeCode,
    string? Message);

public readonly record struct ZLinkStreamError(
    ZLinkStreamSessionError Error,
    ZLinkStreamDiagnostic? Diagnostic);
