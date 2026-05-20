namespace Zlink.Framework.Tests;

public sealed class SampleRegressionTests
{
    [Fact]
    public void Bingo_Uses_RegistryBacked_Defaults_Without_Sample_Metadata_Store()
    {
        var sampleRoot = ResolveSampleRoot("Bingo");

        AssertNoSampleRouteStore(sampleRoot);
        AssertNoSampleMetadataStore(sampleRoot);
        AssertSampleUsesRegistryDefaults(sampleRoot, "bingo");
        AssertSessionHandlersDoNotResolveActorPlayRoutes(sampleRoot);
    }

    [Fact]
    public void TicTacToe_Uses_RegistryBacked_Defaults_Without_Sample_Metadata_Store()
    {
        var sampleRoot = ResolveSampleRoot("TicTacToe.SessionGateway");

        AssertNoSampleRouteStore(sampleRoot);
        AssertNoSampleMetadataStore(sampleRoot);
        AssertSampleUsesRegistryDefaults(sampleRoot, "tictactoe");
        AssertSessionHandlersDoNotResolveActorPlayRoutes(sampleRoot);
    }

    private static void AssertNoSampleRouteStore(string sampleRoot)
    {
        var sourceFiles = EnumerateSourceFiles(sampleRoot).ToArray();
        var fileNames = sourceFiles.Select(Path.GetFileName).ToHashSet(StringComparer.Ordinal);

        Assert.DoesNotContain("RegistryPlayRouteStore.cs", fileNames);
        Assert.DoesNotContain("RegistryPlayRoutePublisher.cs", fileNames);
        Assert.DoesNotContain("SpotRouteContracts.cs", fileNames);

        foreach (var file in sourceFiles)
        {
            var text = File.ReadAllText(file);
            Assert.DoesNotContain("RegistryPlayRouteStore", text, StringComparison.Ordinal);
            Assert.DoesNotContain("RegistryPlayRoutePublisher", text, StringComparison.Ordinal);
            Assert.DoesNotContain("AddActorPlayRouteResolver<RegistryPlayRouteStore>", text, StringComparison.Ordinal);
            Assert.DoesNotContain("AddSpotRouteResolver<RegistryPlayRouteStore>", text, StringComparison.Ordinal);
            Assert.DoesNotContain("BindInitialActorPlayRoutesAsync", text, StringComparison.Ordinal);
        }
    }

    private static void AssertNoSampleMetadataStore(string sampleRoot)
    {
        var sourceFiles = EnumerateSourceFiles(sampleRoot).ToArray();
        var fileNames = sourceFiles.Select(Path.GetFileName).ToHashSet(StringComparer.Ordinal);

        Assert.DoesNotContain("ActorSessionLocationStore.cs", fileNames);
        Assert.DoesNotContain("FileRegistryDiscoveryMetadata.cs", fileNames);
        Assert.DoesNotContain("InMemoryRegistryDiscoveryMetadata.cs", fileNames);
        Assert.DoesNotContain("IRegistryDiscoveryMetadata.cs", fileNames);
        Assert.DoesNotContain("RegistryMetadataEntry.cs", fileNames);
        Assert.DoesNotContain("SampleRuntime.cs", fileNames);

        foreach (var file in sourceFiles)
        {
            var text = File.ReadAllText(file);
            Assert.DoesNotContain("RegistryActorSessionLocationStore", text, StringComparison.Ordinal);
            Assert.DoesNotContain("IRegistryDiscoveryMetadata", text, StringComparison.Ordinal);
            Assert.DoesNotContain("FileRegistryDiscoveryMetadata", text, StringComparison.Ordinal);
            Assert.DoesNotContain("OpenRegistryMetadata", text, StringComparison.Ordinal);
            Assert.DoesNotContain("AddActorSessionBindingStore<RegistryActorSessionLocationStore>", text, StringComparison.Ordinal);
        }
    }

    private static void AssertSampleUsesRegistryDefaults(
        string sampleRoot,
        string namespaceName)
    {
        var allText = string.Join(
            Environment.NewLine,
            EnumerateSourceFiles(sampleRoot).Select(File.ReadAllText));

        Assert.Contains($"UseRegistryActorRoutes(\"{namespaceName}\")", allText, StringComparison.Ordinal);
        Assert.Contains($"UseRegistrySpotRoutes(\"{namespaceName}\")", allText, StringComparison.Ordinal);
        Assert.Contains($"UseRegistryActorSessionBindings(\"{namespaceName}\")", allText, StringComparison.Ordinal);
    }

    private static void AssertSessionHandlersDoNotResolveActorPlayRoutes(string sampleRoot)
    {
        var sessionRoot = Path.Combine(sampleRoot, "Server", "Session");
        var sourceFiles = EnumerateSourceFiles(sessionRoot)
            .Where(static file => file.EndsWith("PacketHandler.cs", StringComparison.Ordinal))
            .ToArray();

        Assert.NotEmpty(sourceFiles);

        foreach (var file in sourceFiles)
        {
            var text = File.ReadAllText(file);
            Assert.DoesNotContain("IZLinkActorPlayRouteResolver", text, StringComparison.Ordinal);
            Assert.DoesNotContain("ResolvePlayRouteAsync", text, StringComparison.Ordinal);
        }
    }

    private static IEnumerable<string> EnumerateSourceFiles(string root)
    {
        return Directory
            .EnumerateFiles(root, "*.cs", SearchOption.AllDirectories)
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}", StringComparison.Ordinal)
                && !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}", StringComparison.Ordinal));
    }

    private static string ResolveSampleRoot(string sampleName)
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);

        while (current is not null)
        {
            var candidate = Path.Combine(
                current.FullName,
                "framework",
                "languages",
                "dotnet",
                "samples",
                sampleName);

            if (Directory.Exists(candidate))
            {
                return candidate;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException(
            $"Could not find framework/languages/dotnet/samples/{sampleName} from test runtime.");
    }
}
