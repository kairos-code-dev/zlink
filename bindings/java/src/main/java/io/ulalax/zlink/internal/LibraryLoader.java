package io.ulalax.zlink.internal;

import java.lang.foreign.SymbolLookup;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;

public final class LibraryLoader {
    private LibraryLoader() {}

    public static SymbolLookup lookup() {
        String path = System.getenv("ZLINK_LIBRARY_PATH");
        if (path != null && !path.isEmpty()) {
            System.load(path);
            return SymbolLookup.loaderLookup();
        }
        try {
            System.loadLibrary("zlink");
            return SymbolLookup.loaderLookup();
        } catch (UnsatisfiedLinkError e) {
            loadFromResources();
            return SymbolLookup.loaderLookup();
        }
    }

    private static void loadFromResources() {
        String os = normalizeOs(System.getProperty("os.name"));
        String arch = normalizeArch(System.getProperty("os.arch"));
        String libFile = libraryFileName(os);
        String resourcePath = "/native/" + os + "-" + arch + "/" + libFile;
        try (InputStream in = LibraryLoader.class.getResourceAsStream(resourcePath)) {
            if (in == null)
                throw new UnsatisfiedLinkError("zlink native resource not found: " + resourcePath);
            Path tmp = Files.createTempFile("zlink-", "-" + libFile);
            Files.copy(in, tmp, java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            tmp.toFile().deleteOnExit();
            System.load(tmp.toAbsolutePath().toString());
        } catch (IOException e) {
            throw new UnsatisfiedLinkError("failed to load zlink native resource: " + e.getMessage());
        }
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
}
