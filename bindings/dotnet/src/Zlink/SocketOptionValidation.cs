// SPDX-License-Identifier: MPL-2.0

using System;

namespace Zlink;

internal static class SocketOptionValidation
{
    internal static void ExpectInt32(SocketOptionValueKind valueKind,
        string paramName)
    {
        Ensure(valueKind, SocketOptionValueKind.Int32, "int", paramName);
    }

    internal static void ExpectInt64(SocketOptionValueKind valueKind,
        string paramName)
    {
        Ensure(valueKind, SocketOptionValueKind.Int64, "long", paramName);
    }

    internal static void ExpectUInt64(SocketOptionValueKind valueKind,
        string paramName)
    {
        Ensure(valueKind, SocketOptionValueKind.UInt64, "ulong", paramName);
    }

    internal static void ExpectBytes(SocketOptionValueKind valueKind,
        string paramName)
    {
        Ensure(valueKind, SocketOptionValueKind.Bytes, "byte[]", paramName);
    }

    internal static void ExpectString(SocketOptionValueKind valueKind,
        string paramName)
    {
        Ensure(valueKind, SocketOptionValueKind.String, "string", paramName);
    }

    private static void Ensure(SocketOptionValueKind valueKind,
        SocketOptionValueKind expected, string expectedName, string paramName)
    {
        if (valueKind != expected)
            throw new ArgumentException(
                $"Expected {expectedName} socket option key.", paramName);
    }
}
