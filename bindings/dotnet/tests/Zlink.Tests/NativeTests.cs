using Xunit;

namespace Zlink.Tests;

public static class NativeTests
{
    public static bool IsNativeAvailable()
    {
        try
        {
            _ = ZlinkVersion.Get();
            return true;
        }
        catch (DllNotFoundException)
        {
            return false;
        }
        catch (EntryPointNotFoundException)
        {
            return false;
        }
    }
}
