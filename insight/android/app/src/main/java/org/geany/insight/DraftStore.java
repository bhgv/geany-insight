// SPDX-License-Identifier: GPL-2.0-or-later
package org.geany.insight;
import android.util.AtomicFile;
import java.io.*;
import java.nio.charset.StandardCharsets;

final class DraftStore {
    private static AtomicFile file(File directory) { return new AtomicFile(new File(directory, "draft.bin")); }
    private static void write(DataOutputStream output, String text) throws IOException {
        byte[] bytes = text.getBytes(StandardCharsets.UTF_8);
        if (bytes.length > 2 * 1024 * 1024) throw new IOException("Draft exceeds 2 MiB");
        output.writeInt(bytes.length); output.write(bytes);
    }
    private static String read(DataInputStream input, int limit) throws IOException {
        int length = input.readInt();
        if (length < 0 || length > limit) throw new IOException("Invalid draft length");
        byte[] bytes = new byte[length]; input.readFully(bytes);
        return MainActivity.utf8(bytes);
    }
    static void save(File directory, MainActivity.State state) throws IOException {
        AtomicFile file = file(directory); FileOutputStream raw = null;
        try {
            raw = file.startWrite(); DataOutputStream output = new DataOutputStream(raw);
            output.writeInt(1); write(output, state.text); write(output, state.uri == null ? "" : state.uri);
            output.writeBoolean(state.dirty); output.writeInt(state.start); output.writeInt(state.end);
            output.flush(); file.finishWrite(raw);
        } catch (IOException ex) { if (raw != null) file.failWrite(raw); throw ex; }
    }
    static void load(File directory, MainActivity.State state) throws IOException {
        AtomicFile file = file(directory);
        if (!file.getBaseFile().exists()) return;
        try (DataInputStream input = new DataInputStream(file.openRead())) {
            if (input.readInt() != 1) throw new IOException("Unknown draft version");
            String text = read(input, 2 * 1024 * 1024), uri = read(input, 16 * 1024);
            boolean dirty = input.readBoolean(); int start = input.readInt(), end = input.readInt();
            if (start < 0 || end < start || end > text.length()) throw new IOException("Invalid selection");
            state.text = text; state.uri = uri.isEmpty() ? null : uri; state.dirty = dirty;
            state.savedText = dirty ? null : text; state.start = start; state.end = end;
            state.message = "Draft restored";
        }
    }
    static void clear(File directory) { file(directory).delete(); }
}
