// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public sealed partial class ZlinkSubmitException
{
    internal ZlinkSubmitException(SubmitResult result)
        : this((ErrorCode)result, 0)
    {
    }

    internal ZlinkSubmitException(SubmitResult result, int nativeErrno)
        : this((ErrorCode)result, nativeErrno)
    {
    }
}

public sealed partial class ZlinkRequestException
{
    internal ZlinkRequestException(RequestResult result)
        : this((ErrorCode)result, 0)
    {
    }

    internal ZlinkRequestException(RequestResult result, int nativeErrno)
        : this((ErrorCode)result, nativeErrno)
    {
    }
}

public sealed partial class ZlinkRecvException
{
    internal ZlinkRecvException(RecvResult result)
        : this((ErrorCode)result, 0)
    {
    }

    internal ZlinkRecvException(RecvResult result, int nativeErrno)
        : this((ErrorCode)result, nativeErrno)
    {
    }
}

public sealed partial class ZlinkHandlerException
{
    internal ZlinkHandlerException(HandlerResult result, int nativeErrno)
        : this((ErrorCode)result, nativeErrno)
    {
    }

    internal ZlinkHandlerException(HandlerResult result)
        : this((ErrorCode)result, 0)
    {
    }
}

public sealed partial class ZlinkCloseException
{
    internal ZlinkCloseException(CloseResult result, int nativeErrno)
        : this((ErrorCode)result, nativeErrno)
    {
    }

    internal ZlinkCloseException(CloseResult result)
        : this((ErrorCode)result, 0)
    {
    }
}

public sealed partial class ZlinkBindException
{
    internal ZlinkBindException(BindResult result, int nativeErrno)
        : this((ErrorCode)result, nativeErrno)
    {
    }

    internal ZlinkBindException(BindResult result)
        : this((ErrorCode)result, 0)
    {
    }
}

public sealed partial class ZlinkConnectException
{
    internal ZlinkConnectException(ConnectResult result, int nativeErrno)
        : this((ErrorCode)result, nativeErrno)
    {
    }

    internal ZlinkConnectException(ConnectResult result)
        : this((ErrorCode)result, 0)
    {
    }
}

public sealed partial class ZlinkConfigException
{
    internal ZlinkConfigException(ConfigResult result, int nativeErrno)
        : this((ErrorCode)result, nativeErrno)
    {
    }

    internal ZlinkConfigException(ConfigResult result)
        : this((ErrorCode)result, 0)
    {
    }
}
