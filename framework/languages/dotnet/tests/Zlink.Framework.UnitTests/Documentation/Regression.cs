using System.Text.RegularExpressions;
using System.Xml.Linq;

namespace Zlink.Framework.UnitTests.Documentation;

public sealed class RegressionTests
{
    private static readonly string[] DotNetContractDocuments =
    [
        "README.ko.md",
        // 언어별 공개 계약은 3문서로 압축한다: 시스템 구조 · 인터페이스 · connector.
        // 기능별 의미는 공통 스펙(framework/doc/framework/spec)이 소유한다.
        "01-system-structure.ko.md",
        "02-handler-interfaces.ko.md",
        "03-stream-connector.ko.md",
        "regression-test-matrix.ko.md",
        "runtime-lifecycle.ko.md",
        "runtime-execution.ko.md",
        "backend-dependency-policy.ko.md",
    ];

    private static readonly string[] GuideNarrativeDocuments =
    [
        "01-overview.ko.md",
        "02-getting-started.ko.md",
        "03-concepts.ko.md",
        "04-feature-map.ko.md",
        "05-channel-messaging.ko.md",
        "06-spot.ko.md",
        "07-actor-spot.ko.md",
        "08-actor-session.ko.md",
        "09-stream.ko.md",
        "10-location.ko.md",
        "11-monitoring.ko.md",
        "12-operations.ko.md",
        "13-interface-catalog.ko.md",
        "14-grpc-alternative.ko.md"
    ];

    [Fact]
    public void DotNetContractDocuments_AllExposeRegressionTestSection()
    {
        var directory = GetDotNetDocRoot();
        var contractDirectory = GetDotNetContractDocRoot();
        // Narrative guide docs are onboarding prose, not contract docs, so they
        // are exempt from the regression-section requirement.
        // API examples remain tied to regression evidence and stay in the strict set.
        var guideRoot = Path.Combine(directory, "guide");
        var actualDocuments = Directory
            .EnumerateFiles(directory, "*.ko.md", SearchOption.AllDirectories)
            .Where(path => !IsUnderDirectory(path, guideRoot, true))
            .Concat(GetDotNetContractDocs()
                .Where(path => !string.Equals(
                    Path.GetFileName(path),
                    "README.ko.md",
                    StringComparison.Ordinal)))
            .Select(Path.GetFileName)
            .OfType<string>()
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(DotNetContractDocuments.Order(StringComparer.Ordinal), actualDocuments);

        var matrix = File.ReadAllText(ResolveDoc("regression-test-matrix.ko.md"));
        var references = ExtractRegressionTestReferences(matrix).ToArray();
        Assert.NotEmpty(references);
        Assert.DoesNotContain(
            references,
            static reference => reference.StartsWith("planned:", StringComparison.Ordinal));
    }

    [Fact]
    public void DotNetContractReadme_Exposes_Resolvable_Regression_Evidence()
    {
        var readme = File.ReadAllText(Path.Combine(GetDotNetContractDocRoot(), "README.ko.md"));

        Assert.Contains("## 회귀 테스트", readme, StringComparison.Ordinal);
        Assert.Contains(
            "ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature",
            readme,
            StringComparison.Ordinal);
        Assert.Contains(
            "RegressionTests.DotNetContractRegressionTestReferences_Resolve_ToActiveTestMethods",
            readme,
            StringComparison.Ordinal);
    }

    [Fact]
    public void DotNetGuideNarrative_DocumentsExist_AndAreWellFormed()
    {
        var guideRoot = Path.Combine(GetDotNetDocRoot(), "guide");

        var actual = Directory
            .EnumerateFiles(guideRoot, "*.ko.md", SearchOption.TopDirectoryOnly)
            .Select(Path.GetFileName)
            .OfType<string>()
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(GuideNarrativeDocuments.Order(StringComparer.Ordinal), actual);

        foreach (var document in GuideNarrativeDocuments)
        {
            var text = File.ReadAllText(Path.Combine(guideRoot, document));
            Assert.Contains("<!-- framework-adapter-nav:start -->", text, StringComparison.Ordinal);
            Assert.Matches(@"(?m)^# .+", text);
        }

    }

    [Fact]
    public void DotNetDocs_SpotRouteChannelAcceptance_RulesStayDocumented()
    {
        var docRoot = GetDotNetDocRoot();
        var guideRoot = Path.Combine(docRoot, "guide");
        var guideAndSampleDocs = Directory
            .EnumerateFiles(guideRoot, "*.ko.md", SearchOption.AllDirectories)
            .ToArray();

        foreach (var path in guideAndSampleDocs)
        {
            var text = File.ReadAllText(path);
            Assert.DoesNotContain("AddChannel(", text, StringComparison.Ordinal);
            Assert.DoesNotContain("AddRouteChannel(", text, StringComparison.Ordinal);
        }

        // SPOT과 channel 등록 표면은 한 문서(시스템 구조)가 함께 소유한다.
        var combined = File.ReadAllText(ResolveDoc("01-system-structure.ko.md"));

        Assert.DoesNotContain("AcceptSpotRoutesFromChannel", combined,
            StringComparison.Ordinal);
        Assert.Contains("AddClientServerChannel", combined,
            StringComparison.Ordinal);
        Assert.Contains("AddRouteMesh", combined,
            StringComparison.Ordinal);
        Assert.DoesNotContain("NonPublic", combined, StringComparison.Ordinal);
        Assert.DoesNotContain("internal/private", combined,
            StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void DotNetDocs_BoundSession_Does_Not_Document_Request_Surface()
    {
        var docRoot = GetDotNetDocRoot();
        var docs = Directory
            .EnumerateFiles(docRoot, "*.ko.md", SearchOption.AllDirectories)
            .Where(static path => !path.Contains(
                $"{Path.DirectorySeparatorChar}draft{Path.DirectorySeparatorChar}",
                StringComparison.Ordinal))
            .ToArray();

        foreach (var path in docs)
        {
            var text = File.ReadAllText(path);
            Assert.DoesNotContain("IZLinkBoundSessionRequestCall", text, StringComparison.Ordinal);
            Assert.DoesNotContain("BoundSession.Request", text, StringComparison.Ordinal);
            Assert.DoesNotContain("Request<TRequest>(TRequest request)", text, StringComparison.Ordinal);
        }
    }

    [Fact]
    public void DotNetDocs_DoNotDocument_Replaced_Spot_Address_Contracts()
    {
        var roots = new[] { GetDotNetDocRoot(), GetDotNetContractDocRoot() };
        var forbidden = new[]
        {
            "SpotRef",
            "IZLinkSpotRefResolver",
            "ResolveSpotRefAsync",
            "IZLinkActorAddressResolver",
            "ResolveActorSpotRefAsync",
            "IZLinkSpotLocationResolver"
        };

        foreach (var path in roots.SelectMany(root =>
                     Directory.EnumerateFiles(root, "*.ko.md", SearchOption.AllDirectories)))
        {
            var text = File.ReadAllText(path);
            foreach (var symbol in forbidden)
                Assert.DoesNotContain(symbol, text, StringComparison.Ordinal);
        }
    }

    [Fact]
    public void DotNetDocs_DoNotDocumentNestedFrameworkConfigurationCallbacks()
    {
        var docRoot = GetDotNetDocRoot();
        var docs = Directory
            .EnumerateFiles(docRoot, "*.ko.md", SearchOption.AllDirectories)
            .Where(static path => !path.Contains(
                $"{Path.DirectorySeparatorChar}draft{Path.DirectorySeparatorChar}",
                StringComparison.Ordinal))
            .ToArray();
        var forbidden = new (Regex Pattern, string Reason)[]
        {
            (new Regex(@"\bEnable(?:Server|Client|Publisher|Subscriber)\s*\([\s\S]{0,160}?Action<", RegexOptions.Compiled),
                "nested capability callback"),
            (new Regex(@"\bUseManualConnections\s*\([\s\S]{0,160}?Action<", RegexOptions.Compiled),
                "manual connection callback"),
            (new Regex(@"\bAddNode\s*\([\s\S]{0,160}?Action<", RegexOptions.Compiled), "spot mesh node callback"),
            (new Regex(@"\bConfigureEntrySpot\s*\([\s\S]{0,160}?Action<", RegexOptions.Compiled),
                "entry spot options callback")
        };
        var offenders = new List<string>();

        foreach (var path in docs)
        {
            var text = File.ReadAllText(path);
            var relative = Path.GetRelativePath(docRoot, path);
            foreach (var (pattern, reason) in forbidden)
                if (pattern.IsMatch(text))
                    offenders.Add($"{relative}: {reason}");
        }

        Assert.Empty(offenders.Order(StringComparer.Ordinal));
    }

    [Fact]
    public void DotNetRegressionMatrix_References_AllContractDocuments()
    {
        var matrix = File.ReadAllText(ResolveDoc("regression-test-matrix.ko.md"));

        foreach (var document in DotNetContractDocuments) Assert.Contains(document, matrix, StringComparison.Ordinal);
    }

    [Fact]
    public void DotNetContractRegressionTestReferences_Resolve_ToActiveTestMethods()
    {
        var activeTests = GetActiveTestMethods();
        var activeE2EScenarios = GetActiveE2EScenarios();
        var unresolved = new List<string>();

        var document = "regression-test-matrix.ko.md";
        var references = ExtractRegressionTestReferences(File.ReadAllText(ResolveDoc(document))).ToArray();
        Assert.NotEmpty(references);

        foreach (var reference in references)
                if (reference.StartsWith("E2E:", StringComparison.Ordinal))
                {
                    if (!activeE2EScenarios.Contains(reference[4..]))
                        unresolved.Add($"{document}: {reference}");
                }
                else if (reference.StartsWith("SCRIPT:", StringComparison.Ordinal))
                {
                    var languageRoot = Path.GetFullPath(Path.Combine(
                        GetDotNetDocRoot(),
                        "..",
                        "..",
                        "..",
                        "languages",
                        "dotnet"));
                    if (!File.Exists(Path.Combine(languageRoot, reference[7..])))
                        unresolved.Add($"{document}: {reference}");
                }
                else if (!activeTests.Contains(reference))
                    unresolved.Add($"{document}: {reference}");

        Assert.True(
            unresolved.Count == 0,
            $"Unresolved regression references:{Environment.NewLine}{string.Join(Environment.NewLine, unresolved.Order(StringComparer.Ordinal))}");
    }

    [Fact]
    public void DotNetRegressionMatrix_Includes_ExecutionSerialization_Guards()
    {
        var matrix = File.ReadAllText(ResolveDoc("regression-test-matrix.ko.md"));

        Assert.Contains("Entry Spot actor mailbox dispatch", matrix, StringComparison.Ordinal);
        Assert.Contains("local actor mailbox dispatch", matrix, StringComparison.Ordinal);
        Assert.Contains("user Spot actor dispatch serialization", matrix, StringComparison.Ordinal);
        Assert.Contains("session actor dispatch ordering", matrix, StringComparison.Ordinal);
        Assert.Contains("actor dispatch location after mailbox wait", matrix, StringComparison.Ordinal);
        Assert.Contains("session callback task dispatch", matrix, StringComparison.Ordinal);
        Assert.Contains("session callback 직렬성", matrix, StringComparison.Ordinal);
        Assert.Contains("runtime task exception observation", matrix, StringComparison.Ordinal);
        Assert.Contains("execution queue cancellation semantics", matrix, StringComparison.Ordinal);
    }

    [Fact]
    public void DotNetRegressionMatrix_Uses_AutomaticTurnDispatch_Contract()
    {
        var matrix = File.ReadAllText(ResolveDoc("regression-test-matrix.ko.md"));

        Assert.Contains("Automatic turn dispatch regression", matrix, StringComparison.Ordinal);
        Assert.Contains(
            "SerialExecutionQueue_AutomaticTurn_Allows_Later_Work_Then_Resumes_On_Line",
            matrix,
            StringComparison.Ordinal);
        Assert.Contains("E2E:ATD-B3", matrix, StringComparison.Ordinal);
        Assert.DoesNotContain("Spot yield dispatch regression", matrix, StringComparison.Ordinal);
        Assert.DoesNotContain("`Yield(...)`", matrix, StringComparison.Ordinal);
    }

    [Fact]
    public void EveryCommonE2EScenarioHasAnActiveDotNetFixtureAndAllRunnerEntry()
    {
        var commonE2ERoot = Path.GetFullPath(Path.Combine(
            GetDotNetDocRoot(),
            "..",
            "common",
            "e2e"));
        var dotNetE2ERoot = Path.GetFullPath(Path.Combine(
            GetDotNetDocRoot(),
            "..",
            "..",
            "..",
            "languages",
            "dotnet",
            "e2e"));
        var fixtureByConfig = new Dictionary<int, string>
        {
            [1] = "LocationMessaging",
            [2] = "SpotService",
            [3] = "PubSub",
            [4] = "RegistrationCodec",
            [5] = "ResilienceLifecycle",
            [6] = "StoreFailure",
            [7] = "RuntimeMonitoring",
            [8] = "AutomaticTurnDispatch",
            [9] = "ToActorMessaging",
            [10] = "SpotActorTransfer",
            [11] = "ObservabilityOps"
        };
        var activeScenarios = GetActiveE2EScenarios();
        var allRunner = File.ReadAllText(Path.Combine(dotNetE2ERoot, "run_e2e_all.sh"));

        // Config 8은 세 terminator(submit/async/yield) 계약으로 다시 쓴 목표 문서다. 현재 `.NET`
        // 구현은 자동 turn dispatch에 머물러 있어 TD-* fixture가 아직 없다. 이 차이는 공통 spec의
        // 구현 차이 문서가 소유한다 — 여기서 조용히 통과시키지 않고, 갭이 기록돼 있는지를 검증한다.
        const int ExecutionTurnConfig = 8;

        foreach (var pair in fixtureByConfig)
        {
            var document = Directory.GetFiles(
                    commonE2ERoot,
                    $"config-{pair.Key}-*.ko.md",
                    SearchOption.TopDirectoryOnly)
                .Single();
            var scenarioIds = Regex.Matches(
                    File.ReadAllText(document),
                    @"(?m)^#{2,5}\s+(?<id>[A-Z][A-Z0-9]*(?:-[A-Z0-9]+)+)\b")
                .Select(static match => match.Groups["id"].Value)
                .ToArray();

            Assert.NotEmpty(scenarioIds);

            if (pair.Key == ExecutionTurnConfig)
            {
                Assert.All(scenarioIds, id => Assert.StartsWith("TD-", id, StringComparison.Ordinal));
                Assert.All(scenarioIds, id => Assert.DoesNotContain(id, activeScenarios));
                continue;
            }

            Assert.All(scenarioIds, id => Assert.Contains(id, activeScenarios));
            Assert.Single(Regex.Matches(
                    allRunner,
                    $@"(?m)^\s{{2}}{Regex.Escape(pair.Value)}$")
                .Cast<Match>());
        }

        // 갭이 실제로 기록돼 있어야 위 예외가 정당하다.
        var gap = File.ReadAllText(Path.Combine(GetCommonSpecRoot(), "90-implementation-gap.ko.md"));
        Assert.Contains("config-8-execution-turn.ko.md", gap, StringComparison.Ordinal);
        Assert.Contains("yield terminator 부재", gap, StringComparison.Ordinal);
    }

    [Fact]
    public void SpotNodeContractsUseTheLocationStoreAsTheAddressSourceOfTruth()
    {
        // SpotNode 등록 계약은 언어별 시스템 구조 문서가 소유한다.
        var dotnet = File.ReadAllText(Path.Combine(GetDotNetContractDocRoot(), "01-system-structure.ko.md"));
        var node = File.ReadAllText(Path.GetFullPath(Path.Combine(
            GetDotNetContractDocRoot(),
            "..",
            "node",
            "01-system-structure.ko.md")));

        Assert.Contains("location store", dotnet, StringComparison.Ordinal);
        Assert.Contains("location store", node, StringComparison.Ordinal);
        Assert.DoesNotContain("core `ResolveSpot", dotnet, StringComparison.Ordinal);
        Assert.DoesNotContain("core `resolveSpot", node, StringComparison.Ordinal);
    }

    [Fact]
    public void DotNetLanguageContractsPreserveTheReviewedPublicRuntimeDecisions()
    {
        var handlers = File.ReadAllText(Path.Combine(GetDotNetContractDocRoot(), "02-handler-interfaces.ko.md"));
        var channel = File.ReadAllText(Path.Combine(
            GetDotNetContractDocRoot(),
            "01-system-structure.ko.md"));
        var spot = File.ReadAllText(Path.Combine(GetDotNetContractDocRoot(), "01-system-structure.ko.md"));
        var actor = File.ReadAllText(Path.Combine(GetDotNetContractDocRoot(), "02-handler-interfaces.ko.md"));

        Assert.Contains("`IZLinkEndpointConnections`", handlers, StringComparison.Ordinal);
        Assert.Contains("`IZLinkSpotMeshBuilder`", handlers, StringComparison.Ordinal);
        Assert.Contains("`IZLinkDrainControl`", handlers, StringComparison.Ordinal);
        Assert.Equal(10, Regex.Matches(handlers, @"string\? packetName = null").Count);
        Assert.Contains(
            "Entry Spot timer callback은 Entry Spot 전체 실행 줄",
            handlers,
            StringComparison.Ordinal);
        Assert.Contains("`ZLinkConfigurationException`", channel, StringComparison.Ordinal);
        Assert.Contains("ASP.NET Core", spot, StringComparison.Ordinal);
        Assert.Contains("public interface IZLinkActorClient", actor, StringComparison.Ordinal);
        Assert.Contains("public interface IZLinkLocationStore", handlers, StringComparison.Ordinal);
    }

    [Fact]
    public void DotNetG0ContractLedger_Covers_LiveSpecs_And_ResolvableProofs()
    {
        var ledgerPath = Path.GetFullPath(Path.Combine(
            GetDotNetDocRoot(),
            "..",
            "..",
            "plan",
            "log",
            "framework-public-contract-gap-implementation",
            "dotnet-g0-contract-ledger.ko.md"));
        var ledger = File.ReadAllText(ledgerPath);
        // spec 트리는 패키지 폴더로 나뉜다. framework가 소유하는 공통 계약은
        // 기반(spec 루트) + server + stream-connector의 framework-facing 계약이다.
        var specRoot = GetCommonSpecRoot();
        var commonSpecFiles = Directory
            .EnumerateFiles(specRoot, "*.ko.md", SearchOption.TopDirectoryOnly)
            .Concat(Directory.EnumerateFiles(
                Path.Combine(specRoot, "server"),
                "*.ko.md",
                SearchOption.TopDirectoryOnly))
            .Append(Path.Combine(specRoot, "stream-connector", "32-stream-connector.ko.md"))
            .ToArray();
        var commonSpecPathByName = commonSpecFiles.ToDictionary(
            static path => Path.GetFileName(path),
            static path => path,
            StringComparer.Ordinal);
        var commonSpecs = commonSpecPathByName.Keys
            .Order(StringComparer.Ordinal)
            .ToArray();
        var contractSpecs = GetDotNetContractDocs()
            .Select(Path.GetFileName)
            .OfType<string>()
            .Where(static file => file != "25-stage-wrapper-on-spot.ko.md")
            .Order(StringComparer.Ordinal)
            .ToArray();

        foreach (var spec in commonSpecs)
            Assert.Matches(
                $@"(?m)^\| DN-COMMON-[0-9]+ \| `{Regex.Escape(spec)}` \|",
                ledger);
        foreach (var spec in contractSpecs)
            Assert.Matches(
                $@"(?m)^\| DN-DOC-[0-9]+ \| `{Regex.Escape(spec)}` \|",
                ledger);

        Assert.Equal(commonSpecs.Length, Regex.Matches(ledger, @"(?m)^\| DN-COMMON-[0-9]+ \|").Count);
        Assert.Equal(contractSpecs.Length, Regex.Matches(ledger, @"(?m)^\| DN-DOC-[0-9]+ \|").Count);
        Assert.Contains("25-stage-wrapper-on-spot.ko.md", ledger, StringComparison.Ordinal);

        var formalMatrixIds = commonSpecs
            .Select(spec => File.ReadAllText(commonSpecPathByName[spec]))
            .SelectMany(static text => Regex
                .Matches(text, @"(?m)^\| (?<id>(?:MFLOW(?:-EXT)?|RMETRIC|DRAIN)-[0-9]{3}) \|")
                .Select(static match => match.Groups["id"].Value))
            .Order(StringComparer.Ordinal)
            .ToArray();
        Assert.NotEmpty(formalMatrixIds);
        Assert.Equal(formalMatrixIds.Length, formalMatrixIds.Distinct(StringComparer.Ordinal).Count());
        foreach (var id in formalMatrixIds)
            Assert.Single(Regex.Matches(ledger, $@"(?m)^\| {Regex.Escape(id)} \|").Cast<Match>());

        // snapshot 경로는 spec 루트 기준 상대 경로다(패키지 폴더 포함).
        var snapshotEntries = Regex
            .Matches(
                ledger,
                @"(?m)^(?<hash>[0-9a-f]{64})\s+(?<path>\S+\.ko\.md)$")
            .Cast<Match>()
            .ToArray();
        var snapshotFiles = commonSpecFiles
            .Concat(GetDotNetContractDocs())
            .Select(path => (
                Key: Path.GetRelativePath(specRoot, path).Replace('\\', '/'),
                Path: path))
            .ToArray();
        Assert.Equal(snapshotFiles.Length, snapshotEntries.Length);
        foreach (var (key, path) in snapshotFiles)
        {
            var entry = Assert.Single(snapshotEntries.Where(match =>
                match.Groups["path"].Value == key));
            var actualHash = Convert
                .ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(path)))
                .ToLowerInvariant();
            Assert.Equal(entry.Groups["hash"].Value, actualHash);
        }

        var activeTests = GetActiveTestMethods();
        var activeE2EScenarios = GetActiveE2EScenarios();
        var unresolved = ExtractLedgerProofReferences(ledger)
            .Where(reference => reference.StartsWith("E2E:", StringComparison.Ordinal)
                ? !activeE2EScenarios.Contains(reference[4..])
                : !activeTests.Contains(reference))
            .Order(StringComparer.Ordinal)
            .ToArray();
        Assert.True(
            unresolved.Length == 0,
            $"Unresolved G0 ledger proofs:{Environment.NewLine}{string.Join(Environment.NewLine, unresolved)}");
    }

    private static string GetDotNetDocRoot()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);

        while (current is not null)
        {
            var candidate = Path.Combine(
                current.FullName,
                "framework",
                "doc",
                "framework",
                "dotnet");

            if (Directory.Exists(candidate)) return candidate;

            current = current.Parent;
        }

        throw new DirectoryNotFoundException(
            "Could not find framework/doc/framework/dotnet from test runtime.");
    }

    /// <summary>
    /// `.NET`의 공개 계약 문서는 두 패키지에 걸쳐 있다 — framework(server)와 stream connector.
    /// </summary>
    private static string[] GetDotNetContractDocs()
    {
        var connectorRoot = Path.Combine(
            GetCommonSpecRoot(),
            "stream-connector",
            "languages",
            "dotnet");

        return Directory
            .EnumerateFiles(GetDotNetContractDocRoot(), "*.ko.md", SearchOption.TopDirectoryOnly)
            .Concat(Directory.EnumerateFiles(connectorRoot, "*.ko.md", SearchOption.TopDirectoryOnly))
            .Order(StringComparer.Ordinal)
            .ToArray();
    }

    private static string GetCommonSpecRoot()
    {
        return Path.GetFullPath(Path.Combine(GetDotNetDocRoot(), "..", "spec"));
    }

    private static string GetDotNetContractDocRoot()
    {
        var dotnetDocRoot = GetDotNetDocRoot();
        var contractRoot = Path.GetFullPath(Path.Combine(
            dotnetDocRoot,
            "..",
            "spec",
            "server",
            "languages",
            "dotnet"));

        return Directory.Exists(contractRoot)
            ? contractRoot
            : throw new DirectoryNotFoundException(
                "Could not find framework/doc/framework/spec/server/languages/dotnet from test runtime.");
    }

    private static string ResolveDoc(string fileName)
    {
        var matches = Directory
            .EnumerateFiles(GetDotNetDocRoot(), fileName, SearchOption.AllDirectories)
            .Concat(Directory.EnumerateFiles(
                GetDotNetContractDocRoot(),
                fileName,
                SearchOption.TopDirectoryOnly))
            .Where(path => !string.Equals(
                Path.GetFileName(path),
                "README.ko.md",
                StringComparison.Ordinal)
                || string.Equals(
                    Path.GetDirectoryName(path),
                    GetDotNetDocRoot(),
                    StringComparison.Ordinal))
            .ToArray();

        return matches.Length switch
        {
            1 => matches[0],
            0 => throw new FileNotFoundException(
                $"Could not find .NET document '{fileName}'."),
            _ => throw new InvalidOperationException(
                $"Ambiguous document '{fileName}': {string.Join(", ", matches)}")
        };
    }

    private static IReadOnlyCollection<string> ExtractRegressionTestReferences(string text)
    {
        var sectionMatch = Regex.Match(
            text,
            @"^## [^\r\n]*회귀 테스트[^\r\n]*\r?\n(?<body>.*?)(?=^## |\z)",
            RegexOptions.Multiline | RegexOptions.Singleline);

        Assert.True(sectionMatch.Success, "Missing regression test section.");

        return Regex
            .Matches(
                sectionMatch.Groups["body"].Value,
                @"^\| (?<cell>.*?) \|",
                RegexOptions.Multiline)
            .SelectMany(static row => Regex
                .Matches(row.Groups["cell"].Value, @"`(?<test>[^`]+)`")
                .Select(static match => match.Groups["test"].Value))
            .ToArray();
    }

    private static IReadOnlySet<string> GetActiveTestMethods()
    {
        var testsRoot = GetTestsRoot();
        var sourceFiles = new HashSet<string>(StringComparer.Ordinal);

        foreach (var projectPath in Directory.EnumerateFiles(testsRoot, "*.csproj", SearchOption.AllDirectories))
            AddProjectSources(projectPath, sourceFiles);

        var activeTests = new HashSet<string>(StringComparer.Ordinal);
        foreach (var sourceFile in sourceFiles)
        {
            var text = File.ReadAllText(sourceFile);
            var classMatches = Regex.Matches(
                text,
                @"(?m)^\s*(?:(?:public|internal|file|private|protected)\s+)*(?:(?:sealed|static|abstract|partial|new)\s+)*class\s+(?<class>[A-Za-z_][A-Za-z0-9_]*)\b");

            if (classMatches.Count == 0) continue;

            foreach (Match methodMatch in Regex.Matches(
                         text,
                         @"\bpublic\s+(?:async\s+)?(?:Task|void)\s+(?<method>[A-Za-z_][A-Za-z0-9_]*)\s*\("))
            {
                var className = classMatches
                    .Last(match => match.Index < methodMatch.Index)
                    .Groups["class"].Value;
                var methodName = methodMatch.Groups["method"].Value;
                if (HasFactOrTheoryAttribute(text, methodMatch.Index)) activeTests.Add($"{className}.{methodName}");
            }
        }

        return activeTests;
    }

    private static IReadOnlySet<string> GetActiveE2EScenarios()
    {
        var e2eRoot = Path.GetFullPath(Path.Combine(
            GetDotNetDocRoot(),
            "..",
            "..",
            "..",
            "languages",
            "dotnet",
            "e2e"));
        var scenarios = new HashSet<string>(StringComparer.Ordinal);
        foreach (var featureMap in Directory.EnumerateFiles(
                     e2eRoot,
                     "feature-map.ko.md",
                     SearchOption.AllDirectories))
        {
            var text = File.ReadAllText(featureMap);
            var runnerPath = Path.Combine(
                Path.GetDirectoryName(featureMap)
                ?? throw new InvalidOperationException($"Missing feature-map directory: {featureMap}"),
                "run_e2e.sh");
            if (!File.Exists(runnerPath)) continue;
            var fixtureRoot = Path.GetDirectoryName(featureMap)!;
            var implementationText = string.Join(
                Environment.NewLine,
                Directory
                    .EnumerateFiles(fixtureRoot, "*", SearchOption.AllDirectories)
                    .Where(static path => path.EndsWith(".cs", StringComparison.Ordinal)
                                          || path.EndsWith(".sh", StringComparison.Ordinal))
                    .Where(static path => !path.Contains(
                                              $"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}",
                                              StringComparison.Ordinal)
                                          && !path.Contains(
                                              $"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}",
                                              StringComparison.Ordinal))
                    .Select(File.ReadAllText));
            foreach (Match match in Regex.Matches(
                         text,
                         @"(?m)^\| (?<id>[A-Z][A-Z0-9]*(?:-[A-Z0-9]+)+) \| (?<status>[^|]+) \|"))
            {
                var scenarioId = match.Groups["id"].Value;
                if (match.Groups["status"].Value.Trim() == "구현"
                    && implementationText.Contains(scenarioId, StringComparison.OrdinalIgnoreCase))
                    scenarios.Add(scenarioId);
            }
        }

        return scenarios;
    }

    private static IEnumerable<string> ExtractLedgerProofReferences(string ledger)
    {
        foreach (var line in ledger.Split('\n'))
        {
            if (!line.StartsWith("| DN-", StringComparison.Ordinal)) continue;
            var cells = line.Split('|');
            if (cells.Length < 6) continue;
            var proofCell = cells[^2];
            foreach (Match match in Regex.Matches(proofCell, @"`(?<reference>[^`]+)`"))
            {
                var reference = match.Groups["reference"].Value;
                if (reference.StartsWith("E2E:", StringComparison.Ordinal)
                    || Regex.IsMatch(
                        reference,
                        @"^[A-Za-z_][A-Za-z0-9_]*\.[A-Za-z_][A-Za-z0-9_]*$"))
                    yield return reference;
            }
        }
    }

    private static bool HasFactOrTheoryAttribute(string text, int methodIndex)
    {
        var prefix = text[..methodIndex];
        var factIndex = new[] { "[Fact", "[Theory", "[SkippableFact", "[SkippableTheory" }
            .Max(attribute => prefix.LastIndexOf(attribute, StringComparison.Ordinal));
        if (factIndex < 0) return false;

        var priorMethod = Regex.Matches(
                prefix,
                @"\bpublic\s+(?:async\s+)?(?:Task|void)\s+[A-Za-z_][A-Za-z0-9_]*\s*\(")
            .Cast<Match>()
            .LastOrDefault();
        if (priorMethod is not null && factIndex < priorMethod.Index) return false;

        var attributes = prefix[factIndex..];
        return !Regex.IsMatch(attributes, @"\bSkip\s*=");
    }

    private static bool IsUnderDirectory(
        string path,
        string directory,
        bool includeDirectChildrenOnly)
    {
        var actualDirectory = Path.GetDirectoryName(path);
        if (actualDirectory is null) return false;

        if (includeDirectChildrenOnly) return string.Equals(actualDirectory, directory, StringComparison.Ordinal);

        var relative = Path.GetRelativePath(directory, actualDirectory);
        return relative == "."
               || (!relative.StartsWith("..", StringComparison.Ordinal)
                   && !Path.IsPathRooted(relative));
    }

    private static void AddProjectSources(string projectPath, ISet<string> sourceFiles)
    {
        var projectDirectory = Path.GetDirectoryName(projectPath)
                               ?? throw new InvalidOperationException(
                                   $"Could not get project directory for '{projectPath}'.");
        var document = XDocument.Load(projectPath);
        var defaultCompileItems = document
            .Descendants("EnableDefaultCompileItems")
            .LastOrDefault()?.Value;
        var projectSources = string.Equals(defaultCompileItems, "false", StringComparison.OrdinalIgnoreCase)
            ? new HashSet<string>(StringComparer.Ordinal)
            : Directory
                .EnumerateFiles(projectDirectory, "*.cs", SearchOption.AllDirectories)
                .Where(static path => !path.Contains(
                                          $"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}",
                                          StringComparison.Ordinal)
                                      && !path.Contains(
                                          $"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}",
                                          StringComparison.Ordinal))
                .Select(Path.GetFullPath)
                .ToHashSet(StringComparer.Ordinal);

        foreach (var remove in document.Descendants("Compile")
                     .SelectMany(static element => element.Attributes("Remove")))
        foreach (var removedPath in ResolveProjectPattern(projectDirectory, remove.Value))
            projectSources.Remove(removedPath);

        foreach (var include in document.Descendants("Compile")
                     .SelectMany(static element => element.Attributes("Include")))
        foreach (var includedPath in ResolveProjectPattern(projectDirectory, include.Value))
            projectSources.Add(includedPath);

        foreach (var sourceFile in projectSources) sourceFiles.Add(sourceFile);
    }

    private static IEnumerable<string> ResolveProjectPattern(string projectDirectory, string pattern)
    {
        if (pattern.Contains('*', StringComparison.Ordinal)) yield break;

        var path = Path.GetFullPath(Path.Combine(projectDirectory, pattern));
        if (File.Exists(path)) yield return path;
    }

    private static string GetTestsRoot()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);

        while (current is not null)
        {
            var candidate = Path.Combine(current.FullName, "framework", "languages", "dotnet", "tests");
            if (Directory.Exists(candidate)) return candidate;

            current = current.Parent;
        }

        throw new DirectoryNotFoundException(
            "Could not find framework/languages/dotnet/tests from test runtime.");
    }
}
