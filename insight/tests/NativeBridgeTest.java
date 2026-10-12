// SPDX-License-Identifier: GPL-2.0-or-later
import org.geany.insight.NativeBridge;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
public final class NativeBridgeTest {
    static byte[] bytes(String s) { return s.getBytes(StandardCharsets.UTF_8); }
    static void check(boolean value) { if (!value) throw new AssertionError(); }
    public static void main(String[] args) {
        byte[] original = bytes("AПривет😀Z");
        NativeBridge.Result r = NativeBridge.run(original, 1, 13, bytes("editor.replace_selection('мир')"));
        check(r.error.length == 0);
        check(new String(r.text, StandardCharsets.UTF_8).equals("Aмир😀Z"));
        check(r.start == 1 && r.end == 7);
        r = NativeBridge.run(original, 1, 13, bytes("editor.replace_selection('changed'); error('stop')"));
        check(r.error.length > 0 && Arrays.equals(r.text, original));
        r = NativeBridge.run(original, 2, 13, bytes("editor.text()"));
        check(r.error.length > 0 && Arrays.equals(r.text, original));
        r = NativeBridge.run(original, 1, 13, bytes("while true do end"));
        check(new String(r.error, StandardCharsets.UTF_8).contains("budget"));
        r = NativeBridge.run(bytes("a😀z"), 1, 5, bytes("assert(editor.selection() == '😀'); editor.replace_selection('🚀')"));
        check(r.error.length == 0 && new String(r.text, StandardCharsets.UTF_8).equals("a🚀z"));
        r = NativeBridge.run(original, 1, 13, bytes("editor.replace_selection(string.char(255))"));
        check(r.error.length > 0 && Arrays.equals(r.text, original));
        r = NativeBridge.run(null, 0, 0, bytes(""));
        check(r.error.length > 0);
        System.out.println("JNI bridge: all checks passed");
    }
}
