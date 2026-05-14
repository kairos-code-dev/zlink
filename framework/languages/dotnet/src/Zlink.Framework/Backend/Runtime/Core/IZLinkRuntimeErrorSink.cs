namespace Zlink.Framework.Runtime.Core;

internal interface IZLinkRuntimeErrorSink
{
    void ReportHandlerException(Exception exception);

    void ReportRuntimeTaskException(
        string taskName,
        Exception exception);
}
