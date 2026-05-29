// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

internal static class Runtime
{
    public static event Action<Exception>? UnhandledCallbackException;

    internal static void ReportUnhandledCallbackException(Exception exception)
    {
        try
        {
            UnhandledCallbackException?.Invoke(exception);
        }
        catch
        {
        }
    }

}
