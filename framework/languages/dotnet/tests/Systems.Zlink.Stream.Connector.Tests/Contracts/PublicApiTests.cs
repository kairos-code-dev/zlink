using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Contracts.Calls;
using Xunit;

public sealed partial class StreamConnectorTests
{
    private static readonly string[] PublicTypeDeclarationTokens =
    [
        "public interface ",
        "public class ",
        "public sealed class ",
        "public abstract class ",
        "public record ",
        "public sealed record ",
        "public enum ",
        "public struct ",
        "public readonly struct ",
        "public delegate ",
        "public static class "
    ];

    private static readonly string[] InternalTypeDeclarationTokens =
    [
        "internal interface ",
        "internal class ",
        "internal sealed class ",
        "internal abstract class ",
        "internal record ",
        "internal sealed record ",
        "internal enum ",
        "internal struct ",
        "internal readonly struct ",
        "internal delegate ",
        "internal static class "
    ];

    [Fact]
    public void ConnectorImplementationIsHiddenBehindPublicInterface()
    {
        var assembly = typeof(IZlinkStreamConnector).Assembly;
        var implementation = assembly.GetType(
            "Systems.Zlink.Stream.Connector.Runtime.ZlinkStreamConnector",
            true)!;

        Assert.False(implementation.IsPublic);
        Assert.True(typeof(IZlinkStreamConnector).IsAssignableFrom(implementation));

        var create = typeof(ZlinkStreamConnectorFactory).GetMethod(nameof(ZlinkStreamConnectorFactory.Create))!;
        Assert.Equal(typeof(IZlinkStreamConnector), create.ReturnType);
    }

    [Fact]
    public void ConnectorSendAndRequestExposeInterfaces()
    {
        var send = typeof(IZlinkStreamConnector).GetMethod(nameof(IZlinkStreamConnector.Send))!;
        var request = typeof(IZlinkStreamConnector).GetMethod(nameof(IZlinkStreamConnector.Request))!;

        Assert.Equal(typeof(IZlinkStreamSendCall), send.ReturnType);
        Assert.Equal(typeof(IZlinkStreamRequestCall), request.ReturnType);
    }

    [Fact]
    public void DisconnectEventCarriesTheFrozenCloseReasonContract()
    {
        var eventInfo = typeof(IZlinkStreamConnector).GetEvent(nameof(IZlinkStreamConnector.Disconnected));
        Assert.NotNull(eventInfo);
        Assert.Equal(
            typeof(Func<ZlinkStreamDisconnected, CancellationToken, ValueTask>),
            eventInfo!.EventHandlerType);
        Assert.Equal(
            new[]
            {
                "ClientClose", "HeartbeatTimeout", "IdleTimeout", "ProtocolError", "ServerDrain", "TransportError"
            },
            Enum.GetNames<ZlinkStreamCloseReason>().Order(StringComparer.Ordinal).ToArray());
    }

    [Fact]
    public void ConnectorPublicTypeSourceFiles_LiveUnderContracts()
    {
        var sourceRoot = GetConnectorSourceRoot();
        var violations = Directory
            .EnumerateFiles(sourceRoot, "*.cs", SearchOption.AllDirectories)
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}",
                                      StringComparison.Ordinal)
                                  && !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}",
                                      StringComparison.Ordinal))
            .Where(path => HasPublicTypeDeclaration(File.ReadAllText(path)))
            .Where(path => !Path.GetRelativePath(sourceRoot, path)
                .StartsWith($"Contracts{Path.DirectorySeparatorChar}", StringComparison.Ordinal))
            .Select(path => Path.GetRelativePath(sourceRoot, path))
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Empty(violations);
    }

    [Fact]
    public void ConnectorContracts_DoNotContain_InternalTypeDeclarations()
    {
        var contractsRoot = Path.Combine(GetConnectorSourceRoot(), "Contracts");
        var violations = Directory
            .EnumerateFiles(contractsRoot, "*.cs", SearchOption.AllDirectories)
            .Where(path => HasInternalTypeDeclaration(File.ReadAllText(path)))
            .Select(path => Path.GetRelativePath(contractsRoot, path))
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Empty(violations);
    }

    [Fact]
    public void ConnectorExportedTypes_UseContractsNamespace()
    {
        var violations = typeof(IZlinkStreamConnector).Assembly
            .GetExportedTypes()
            .Where(static type => type.Namespace is null
                                  || (!type.Namespace.Equals("Systems.Zlink.Stream.Connector.Contracts",
                                          StringComparison.Ordinal)
                                      && !type.Namespace.StartsWith("Systems.Zlink.Stream.Connector.Contracts.",
                                          StringComparison.Ordinal)))
            .Select(static type => type.FullName)
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Empty(violations);
    }

    [Fact]
    public void ConnectorDefaultCodecFactory_IsNotPublicContract()
    {
        var exportedTypeNames = typeof(IZlinkStreamConnector).Assembly
            .GetExportedTypes()
            .Select(static type => type.Name)
            .ToArray();

        Assert.DoesNotContain("ZlinkStreamDefaultCodecs", exportedTypeNames);
        Assert.DoesNotContain("ZlinkStreamDefaultCodecFactory", exportedTypeNames);
        Assert.Contains("IZlinkStreamCompressionCodec", exportedTypeNames);
    }

    private static string GetConnectorSourceRoot()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            var candidate = Path.Combine(
                directory.FullName,
                "src",
                "Systems.Zlink.Stream.Connector");
            if (Directory.Exists(candidate)) return candidate;

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate Systems.Zlink.Stream.Connector source root.");
    }

    private static bool HasPublicTypeDeclaration(string source)
    {
        return source
            .Split('\n')
            .Select(static line => line.TrimEnd('\r'))
            .Any(static line =>
                PublicTypeDeclarationTokens.Any(token => line.StartsWith(token, StringComparison.Ordinal)));
    }

    private static bool HasInternalTypeDeclaration(string source)
    {
        return source
            .Split('\n')
            .Select(static line => line.TrimEnd('\r'))
            .Any(static line =>
                InternalTypeDeclarationTokens.Any(token => line.StartsWith(token, StringComparison.Ordinal)));
    }
}
