package dev.kairoscode.zlink.internal;

import java.lang.foreign.SymbolLookup;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

final class LibraryLoader {
    private static final Object LOCK = new Object();
    private static volatile SymbolLookup LOOKUP;

    private LibraryLoader() {}

    public static SymbolLookup lookup() {
        SymbolLookup lookup = LOOKUP;
        if (lookup != null) {
            return lookup;
        }
        synchronized (LOCK) {
            lookup = LOOKUP;
            if (lookup != null) {
                return lookup;
            }
            String path = System.getenv("ZLINK_LIBRARY_PATH");
            if (path != null && !path.isEmpty()) {
                Path p = Path.of(path);
                if (!p.isAbsolute())
                    p = p.toAbsolutePath();
                System.load(p.toString());
                loadOptionalBridgeFromResources();
                LOOKUP = SymbolLookup.loaderLookup();
                return LOOKUP;
            }
            Path nativeDir = findBindingNativeDir();
            if (nativeDir != null) {
                loadFromDirectory(nativeDir);
                LOOKUP = SymbolLookup.loaderLookup();
                return LOOKUP;
            }
            try {
                loadFromResources();
                LOOKUP = SymbolLookup.loaderLookup();
                return LOOKUP;
            } catch (UnsatisfiedLinkError e) {
                System.loadLibrary("zlink");
                loadOptionalBridgeFromResources();
                LOOKUP = SymbolLookup.loaderLookup();
                return LOOKUP;
            }
        }
    }

    private static void loadFromResources() {
        String os = normalizeOs(System.getProperty("os.name"));
        String arch = normalizeArch(System.getProperty("os.arch"));
        String libFile = libraryFileName(os);
        String resourcePath = resourcePath(os, arch, libFile);
        try (InputStream in = LibraryLoader.class.getResourceAsStream(resourcePath)) {
            if (in == null)
                throw new UnsatisfiedLinkError("zlink native resource not found: " + resourcePath);
            Path tmpDir = Files.createTempDirectory("zlink-native-");
            Path tmp = tmpDir.resolve(libFile);
            Files.copy(in, tmp, java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            tmp.toFile().deleteOnExit();
            tmpDir.toFile().deleteOnExit();
            if ("windows".equals(os))
                preloadWindowsDeps(tmpDir);
            loadOptionalBridgeFromResources(os, arch, tmpDir);
            System.load(tmp.toAbsolutePath().toString());
            loadOptionalBridgeFromDirectory(os, tmpDir);
        } catch (IOException e) {
            throw new UnsatisfiedLinkError("failed to load zlink native resource: " + e.getMessage());
        }
    }

    private static void loadOptionalBridgeFromResources() {
        String os = normalizeOs(System.getProperty("os.name"));
        String arch = normalizeArch(System.getProperty("os.arch"));
        loadOptionalBridgeFromResources(os, arch, null);
    }

    private static void loadOptionalBridgeFromResources(String os,
                                                        String arch,
                                                        Path targetDir) {
        String resourcePath = resourcePath(os, arch, bridgeFileName(os));
        try (InputStream in = LibraryLoader.class.getResourceAsStream(resourcePath)) {
            if (in == null)
                return;
            Path dir = targetDir != null
                ? targetDir
                : Files.createTempDirectory("zlink-java-bridge-");
            Path tmp = dir.resolve(bridgeFileName(os));
            Files.copy(in, tmp, java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            tmp.toFile().deleteOnExit();
            if (targetDir == null) {
                dir.toFile().deleteOnExit();
                System.load(tmp.toAbsolutePath().toString());
            }
        } catch (IOException e) {
            throw new UnsatisfiedLinkError(
                "failed to load zlink java bridge resource: " + e.getMessage());
        }
    }

    private static void loadOptionalBridgeFromDirectory(String os, Path dir) {
        Path bridge = dir.resolve(bridgeFileName(os));
        if (!Files.exists(bridge)) {
            Path generated = findGeneratedBridgeDir();
            if (generated != null) {
                bridge = generated.resolve(bridgeFileName(os));
            }
        }
        if (Files.exists(bridge)) {
            System.load(bridge.toAbsolutePath().toString());
        }
    }

    private static void loadFromDirectory(Path dir) {
        String os = normalizeOs(System.getProperty("os.name"));
        Path lib = dir.resolve(libraryFileName(os));
        if (!Files.exists(lib)) {
            throw new UnsatisfiedLinkError("zlink native library not found: " + lib);
        }
        if ("windows".equals(os)) {
            preloadWindowsDeps(dir);
        }
        System.load(lib.toAbsolutePath().toString());
        loadOptionalBridgeFromDirectory(os, dir);
    }

    private static void preloadWindowsDeps(Path localDir) {
        String[] depNames = new String[] {
                "libcrypto-3-x64.dll",
                "libssl-3-x64.dll"
        };
        for (String dep : depNames) {
            Path p = findWindowsDependency(localDir, dep);
            if (p != null) {
                try {
                    System.load(p.toString());
                } catch (UnsatisfiedLinkError ignored) {
                }
            }
        }
    }

    private static Path findWindowsDependency(Path localDir, String fileName) {
        List<Path> dirs = new ArrayList<>();
        if (localDir != null)
            dirs.add(localDir);
        String opensslBin = System.getenv("ZLINK_OPENSSL_BIN");
        if (opensslBin != null && !opensslBin.isEmpty())
            dirs.add(Path.of(opensslBin));
        String runtimeBin = System.getenv("ZLINK_WINDOWS_RUNTIME_BIN");
        if (runtimeBin != null && !runtimeBin.isEmpty())
            dirs.add(Path.of(runtimeBin));
        Path userDir = Path.of(System.getProperty("user.dir", ".")).toAbsolutePath();
        dirs.add(userDir.resolve("../dotnet/runtimes/win-x64/native").normalize());
        dirs.add(userDir.resolve("../node/prebuilds/win32-x64").normalize());
        dirs.add(Path.of("C:\\Program Files\\OpenSSL-Win64\\bin"));
        dirs.add(Path.of("C:\\Program Files\\Git\\mingw64\\bin"));
        dirs.add(Path.of(
                "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\TeamFoundation\\Team Explorer\\Git\\mingw64\\bin"));
        String envPath = System.getenv("PATH");
        if (envPath != null && !envPath.isEmpty()) {
            for (String entry : envPath.split(";")) {
                if (!entry.isEmpty())
                    dirs.add(Path.of(entry));
            }
        }

        for (Path dir : dirs) {
            try {
                Path candidate = dir.resolve(fileName);
                if (Files.exists(candidate))
                    return candidate.toAbsolutePath();
            } catch (Exception ignored) {
            }
        }
        return null;
    }

    private static String normalizeOs(String name) {
        String os = name.toLowerCase();
        if (os.contains("win"))
            return "windows";
        if (os.contains("mac") || os.contains("darwin"))
            return "darwin";
        if (os.contains("linux"))
            return "linux";
        return os.replaceAll("\\s+", "");
    }

    private static String normalizeArch(String arch) {
        String a = arch.toLowerCase();
        if (a.equals("amd64") || a.equals("x86_64"))
            return "x86_64";
        if (a.equals("aarch64") || a.equals("arm64"))
            return "aarch64";
        return a.replaceAll("\\s+", "");
    }

    private static String libraryFileName(String os) {
        if ("windows".equals(os))
            return "zlink.dll";
        if ("darwin".equals(os))
            return "libzlink.dylib";
        return "libzlink.so";
    }

    private static String bridgeFileName(String os) {
        if ("windows".equals(os))
            return "zlink_java_bridge.dll";
        if ("darwin".equals(os))
            return "libzlink_java_bridge.dylib";
        return "libzlink_java_bridge.so";
    }

    private static String resourcePath(String os, String arch, String fileName) {
        return "/native/" + os + "-" + arch + "/" + fileName;
    }

    private static Path findBindingNativeDir() {
        String os = normalizeOs(System.getProperty("os.name"));
        String arch = normalizeArch(System.getProperty("os.arch"));
        String relative = "native/" + os + "-" + arch;
        Path cwd = Path.of(System.getProperty("user.dir", ".")).toAbsolutePath();
        String required = libraryFileName(os);
        Path local = cwd.resolve("src/main/resources").resolve(relative)
            .normalize();
        if (Files.exists(local.resolve(required))) {
            return local;
        }
        return findAncestorRelative(cwd,
            "bindings/java/src/main/resources/" + relative, required);
    }

    private static Path findGeneratedBridgeDir() {
        String os = normalizeOs(System.getProperty("os.name"));
        String arch = normalizeArch(System.getProperty("os.arch"));
        String relative = "build/generated/zlink-native-resources/main/native/"
            + os + "-" + arch;
        Path cwd = Path.of(System.getProperty("user.dir", ".")).toAbsolutePath();
        String required = bridgeFileName(os);
        Path local = cwd.resolve(relative).normalize();
        if (Files.exists(local.resolve(required))) {
            return local;
        }
        return findAncestorRelative(cwd, "bindings/java/" + relative, required);
    }

    private static Path findAncestorRelative(Path start, String relative,
                                             String requiredFile) {
        Path current = start;
        while (current != null) {
            Path candidate = current.resolve(relative).normalize();
            if (Files.exists(candidate.resolve(requiredFile))) {
                return candidate;
            }
            current = current.getParent();
        }
        return null;
    }
}
