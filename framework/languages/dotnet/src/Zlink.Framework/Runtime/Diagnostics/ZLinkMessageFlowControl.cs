namespace Zlink.Framework.Runtime.Diagnostics;

// Public runtime toggle (IZLinkMessageFlowControl). Writes the shared live-mode cell
// so every surface that reads EffectiveMessageFlow observes the change immediately.
internal sealed class ZLinkMessageFlowControl(ZLinkDiagnosticsOptionsModel diagnostics)
    : IZLinkMessageFlowControl
{
    public void SetMessageFlowMode(ZLinkMessageFlowLogMode mode)
    {
        if (diagnostics.LiveMode is { } cell)
        {
            cell.Mode = mode;
        }
        else
        {
            diagnostics.MessageFlow = mode;
        }
    }

    public ZLinkMessageFlowLogMode MessageFlowMode => diagnostics.EffectiveMessageFlow;
}
