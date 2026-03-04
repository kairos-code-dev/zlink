package dev.kairoscode.zlink;

import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class CoreVersionPortedTest {
    private static final Pattern VERSION_LINE =
        Pattern.compile("^#define\\s+ZLINK_VERSION_(MAJOR|MINOR|PATCH)\\s+(\\d+)\\s*$");

    @Test
    public void testVersionMatchesCoreHeader() throws IOException {
        TestSupport.assumeNative();

        int[] actual = ZlinkVersion.get();
        int[] expected = readHeaderVersion();

        assertEquals(expected[0], actual[0]);
        assertEquals(expected[1], actual[1]);
        assertEquals(expected[2], actual[2]);
    }

    private static int[] readHeaderVersion() throws IOException {
        Path header = locateHeader();
        if (header == null)
            throw new IOException("core/include/zlink.h not found");

        int major = -1;
        int minor = -1;
        int patch = -1;

        List<String> lines = Files.readAllLines(header);
        for (String line : lines) {
            Matcher m = VERSION_LINE.matcher(line);
            if (!m.matches())
                continue;
            String key = m.group(1);
            int value = Integer.parseInt(m.group(2));
            if ("MAJOR".equals(key))
                major = value;
            else if ("MINOR".equals(key))
                minor = value;
            else if ("PATCH".equals(key))
                patch = value;
        }

        if (major < 0 || minor < 0 || patch < 0)
            throw new IOException("version parse failed from " + header);
        return new int[]{major, minor, patch};
    }

    private static Path locateHeader() {
        Path current = Path.of(".").toAbsolutePath().normalize();
        for (int i = 0; i < 8 && current != null; i++) {
            Path candidate = current.resolve("core/include/zlink.h");
            if (Files.exists(candidate))
                return candidate;
            current = current.getParent();
        }
        return null;
    }
}
