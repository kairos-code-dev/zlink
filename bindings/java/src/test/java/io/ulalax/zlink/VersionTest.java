package io.ulalax.zlink;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Assumptions;

public class VersionTest {
    @Test
    public void versionMatchesCore() {
        int[] v;
        try {
            v = ZlinkVersion.get();
        } catch (Throwable e) {
            Assumptions.assumeTrue(false, "zlink native library not found: " + e.getMessage());
            return;
        }
        org.junit.jupiter.api.Assertions.assertEquals(0, v[0]);
        org.junit.jupiter.api.Assertions.assertEquals(7, v[1]);
        org.junit.jupiter.api.Assertions.assertEquals(0, v[2]);
    }
}
