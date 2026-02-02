package io.ulalax.zlink.internal;

import java.lang.foreign.Arena;
import java.lang.foreign.SymbolLookup;

public final class LibraryLoader {
    private LibraryLoader() {}

    public static SymbolLookup lookup() {
        String path = System.getenv("ZLINK_LIBRARY_PATH");
        if (path != null && !path.isEmpty()) {
            System.load(path);
        } else {
            System.loadLibrary("zlink");
        }
        return SymbolLookup.loaderLookup();
    }
}
