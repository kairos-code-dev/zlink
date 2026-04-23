using System.Globalization;
using System.Reflection;
using System.Runtime.Versioning;

namespace Zlink.Framework.Tests.Common;

internal static class FrameworkTestEnvironment
{
    public static string GetFrameworkRoot()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);

        while (current is not null)
        {
            if (File.Exists(Path.Combine(current.FullName, "Zlink.Framework.sln")))
            {
                return current.FullName;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate framework/languages/dotnet root.");
    }

    public static string GetRepoRoot()
    {
        var frameworkRoot = new DirectoryInfo(GetFrameworkRoot());
        var repoRoot = frameworkRoot.Parent?.Parent?.Parent;

        if (repoRoot is null)
        {
            throw new DirectoryNotFoundException("Could not locate repository root from framework root.");
        }

        return repoRoot.FullName;
    }

    public static string GetTargetFrameworkMoniker()
    {
        var attribute = Assembly.GetExecutingAssembly().GetCustomAttribute<TargetFrameworkAttribute>()
            ?? throw new InvalidOperationException("Target framework attribute is missing.");
        var frameworkName = new System.Runtime.Versioning.FrameworkName(attribute.FrameworkName);
        return string.Create(
            CultureInfo.InvariantCulture,
            $"net{frameworkName.Version.Major}.{frameworkName.Version.Minor}");
    }

    public static string GetBuildConfiguration()
    {
        return Assembly.GetExecutingAssembly()
            .GetCustomAttribute<AssemblyConfigurationAttribute>()?.Configuration ?? "Debug";
    }

    public static string GetTestHostProjectPath()
    {
        return Path.Combine(
            GetFrameworkRoot(),
            "testapps",
            "Zlink.Framework.TestHost",
            "Zlink.Framework.TestHost.csproj");
    }

    public static string GetTestHostAssemblyPath()
    {
        return Path.Combine(
            GetFrameworkRoot(),
            "testapps",
            "Zlink.Framework.TestHost",
            "bin",
            GetBuildConfiguration(),
            GetTargetFrameworkMoniker(),
            "Zlink.Framework.TestHost.dll");
    }

    public static string GetTestHostExecutablePath()
    {
        return Path.Combine(
            GetFrameworkRoot(),
            "testapps",
            "Zlink.Framework.TestHost",
            "bin",
            GetBuildConfiguration(),
            GetTargetFrameworkMoniker(),
            OperatingSystem.IsWindows() ? "Zlink.Framework.TestHost.exe" : "Zlink.Framework.TestHost");
    }
}
