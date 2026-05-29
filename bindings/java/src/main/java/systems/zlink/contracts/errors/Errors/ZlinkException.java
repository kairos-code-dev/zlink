/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.errors;

import systems.zlink.contracts.sockets.RecvResult;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.internal.ContractAccess;
import java.util.Locale;

public abstract class ZlinkException extends RuntimeException {
    private final int code;
    private final int internalErrno;

    protected ZlinkException(int code) {
        this(code, 0);
    }

    protected ZlinkException(int code, int internalErrno) {
        this(null, code, internalErrno);
    }

    protected ZlinkException(String message, int code, int internalErrno) {
        super(message);
        this.code = code;
        this.internalErrno = internalErrno;
    }

    public int getCode() {
        return code;
    }

    public int getInternalErrno() {
        return internalErrno;
    }

    int errno() {
        return internalErrno;
    }

    ErrorCode errorCode() {
        for (ErrorCode code : ErrorCode.values()) {
            if (code.getValue() == internalErrno) {
                return code;
            }
        }
        return null;
    }

    public static ZlinkException fromLastError(String operation) {
        int errno = safeErrno();
        return fromErrno(operation, errno);
    }

    public static ZlinkException fromErrno(String operation, int errno) {
        String op = operation == null ? "" : operation.toLowerCase(Locale.ROOT);
        if (containsAny(op, "handler")) {
            return new HandlerException(mapHandlerResult(errno), errno);
        }
        if (containsAny(op, "recv", "receive", "subscription_event",
                "socket_monitor_recv", "monitor_recv")
            || (op.contains("subscribe")
                && !containsAny(op, "set_subscription", "unset_subscription",
                    "subscribe_handler"))) {
            return new RecvException(mapRecvResult(errno), errno);
        }
        if (containsAny(op, "request")) {
            return new RequestException(mapRequestResult(errno), errno);
        }
        if (containsAny(op, "bind")) {
            return new BindException(mapBindResult(errno), errno);
        }
        if (containsAny(op, "connect", "disconnect", "unbind")) {
            return new ConnectException(mapConnectResult(errno), errno);
        }
        if (containsAny(op, "close", "destroy")) {
            return new CloseException(mapCloseResult(errno), errno);
        }
        if (containsAny(op, "send", "publish", "reply", "request", "proxy")) {
            return new SubmitException(mapSubmitResult(errno), errno);
        }
        return new ConfigException(mapConfigResult(errno), errno);
    }

    private static int safeErrno() {
        try {
            return ContractAccess.nativeErrno();
        } catch (RuntimeException ignored) {
            return -1;
        }
    }

    private static String safeStrerror(int errno) {
        if (errno < 0) {
            return "";
        }
        try {
            String message = ContractAccess.nativeStrerror(errno);
            return message == null ? "" : message;
        } catch (RuntimeException ignored) {
            return "";
        }
    }

    private static boolean containsAny(String value, String... needles) {
        for (String needle : needles) {
            if (value.contains(needle)) {
                return true;
            }
        }
        return false;
    }

    private static SubmitResult mapSubmitResult(int errno) {
        return switch (errno) {
            case 14 -> SubmitResult.INVALID_HANDLE;
            case 11, 10035 -> SubmitResult.BACKPRESSURED;
            case 107, 10057 -> SubmitResult.NOT_CONNECTED;
            case 113, 10065 -> SubmitResult.NOT_FOUND;
            case 111, 10061 -> SubmitResult.NOT_ADMITTED;
            case 22 -> SubmitResult.INVALID_ARGUMENT;
            case 9 -> SubmitResult.INVALID_HANDLE;
            case 95 -> SubmitResult.NOT_SUPPORTED;
            case 12 -> SubmitResult.OUT_OF_MEMORY;
            default -> SubmitResult.INTERNAL_ERROR;
        };
    }

    private static RecvResult mapRecvResult(int errno) {
        return switch (errno) {
            case 14 -> RecvResult.INVALID_HANDLE;
            case 11, 10035 -> RecvResult.NO_DATA;
            case 4 -> RecvResult.BUSY;
            case 107, 10057 -> RecvResult.TERMINATED;
            case 9 -> RecvResult.INVALID_HANDLE;
            case 95 -> RecvResult.NOT_SUPPORTED;
            default -> RecvResult.INTERNAL_ERROR;
        };
    }

    private static BindResult mapBindResult(int errno) {
        return switch (errno) {
            case 14 -> BindResult.INVALID_HANDLE;
            case 22 -> BindResult.INVALID_ARGUMENT;
            case 98 -> BindResult.ADDR_IN_USE;
            case 95 -> BindResult.NOT_SUPPORTED;
            case 9 -> BindResult.INVALID_HANDLE;
            default -> BindResult.INVALID_ARGUMENT;
        };
    }

    private static ConnectResult mapConnectResult(int errno) {
        return switch (errno) {
            case 14 -> ConnectResult.INVALID_HANDLE;
            case 22 -> ConnectResult.INVALID_ARGUMENT;
            case 95 -> ConnectResult.NOT_SUPPORTED;
            case 9 -> ConnectResult.INVALID_HANDLE;
            default -> ConnectResult.INVALID_ARGUMENT;
        };
    }

    private static CloseResult mapCloseResult(int errno) {
        return switch (errno) {
            case 14 -> CloseResult.INVALID_HANDLE;
            case 11, 10035 -> CloseResult.BUSY;
            case 107, 10057 -> CloseResult.SHUTDOWN;
            case 9 -> CloseResult.INVALID_HANDLE;
            default -> CloseResult.BUSY;
        };
    }

    private static HandlerResult mapHandlerResult(int errno) {
        return switch (errno) {
            case 14 -> HandlerResult.INVALID_HANDLE;
            case 22 -> HandlerResult.INVALID_ARGUMENT;
            case 11, 10035 -> HandlerResult.BUSY;
            case 95 -> HandlerResult.NOT_SUPPORTED;
            case 16 -> HandlerResult.DEADLOCK;
            case 9 -> HandlerResult.INVALID_HANDLE;
            default -> HandlerResult.BUSY;
        };
    }

    private static ConfigResult mapConfigResult(int errno) {
        return switch (errno) {
            case 14 -> ConfigResult.INVALID_HANDLE;
            case 22 -> ConfigResult.INVALID_ARGUMENT;
            case 95 -> ConfigResult.NOT_SUPPORTED;
            case 9 -> ConfigResult.INVALID_HANDLE;
            default -> ConfigResult.INTERNAL_ERROR;
        };
    }

    private static RequestResult mapRequestResult(int errno) {
        return switch (errno) {
            case 11, 10035 -> RequestResult.TIMED_OUT;
            case 107, 10057 -> RequestResult.NOT_FOUND;
            case 113, 10065 -> RequestResult.NOT_FOUND;
            case 4 -> RequestResult.PROTOCOL_ERROR;
            default -> RequestResult.PROTOCOL_ERROR;
        };
    }
}
