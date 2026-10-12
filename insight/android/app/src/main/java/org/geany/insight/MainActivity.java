// SPDX-License-Identifier: GPL-2.0-or-later
package org.geany.insight;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ContentResolver;
import android.content.Intent;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.Gravity;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private static final int OPEN = 1, SAVE_AS = 2, LUA = 3;
    private static final int LIMIT = 2 * 1024 * 1024;
    static final class State {
        String text = "", savedText = "", undo;
        String uri;
        int start, end, undoStart, undoEnd;
        boolean dirty, busy;
        String message = "Ready";
        final Handler main = new Handler(Looper.getMainLooper());
        final ExecutorService worker = Executors.newSingleThreadExecutor();
        MainActivity owner;
    }
    private State state;
    private EditText editor;
    private TextView status;
    private LinearLayout toolbar;
    private boolean rendering, discarded;

    @SuppressWarnings("deprecation")
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        if (android.os.Build.VERSION.SDK_INT >= 30) getWindow().setDecorFitsSystemWindows(false);
        Object retained = getLastNonConfigurationInstance();
        state = retained instanceof State ? (State) retained : new State();
        state.owner = this;
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(8, 8, 8, 8);
        // Respect system bars when targeting Android 15 edge-to-edge behavior.
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            if (android.os.Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets bars = insets.getInsets(android.view.WindowInsets.Type.systemBars()
                    | android.view.WindowInsets.Type.ime());
                view.setPadding(8 + bars.left, 8 + bars.top, 8 + bars.right, 8 + bars.bottom);
            } else {
                view.setPadding(8 + insets.getSystemWindowInsetLeft(), 8 + insets.getSystemWindowInsetTop(),
                    8 + insets.getSystemWindowInsetRight(), 8 + insets.getSystemWindowInsetBottom());
            }
            return insets;
        });
        android.widget.HorizontalScrollView tools = new android.widget.HorizontalScrollView(this);
        toolbar = new LinearLayout(this);
        tools.addView(toolbar);
        addButton("Open", () -> confirmOpen());
        addButton("Save", () -> save(false));
        addButton("Save as", () -> save(true));
        addButton("Lua", () -> picker(LUA));
        addButton("Undo", () -> undo());
        root.addView(tools);
        status = new TextView(this);
        root.addView(status);
        editor = new EditText(this);
        editor.setTypeface(Typeface.MONOSPACE);
        editor.setTextSize(14);
        editor.setGravity(Gravity.TOP | Gravity.START);
        editor.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE
            | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        editor.setHorizontallyScrolling(true);
        editor.setSingleLine(false);
        editor.setFilters(new android.text.InputFilter[] { (source, first, last, dest, begin, finish) -> {
            String inserted = source.subSequence(first, last).toString();
            String prospective = dest.subSequence(0, begin).toString() + inserted + dest.subSequence(finish, dest.length());
            if (inserted.indexOf(0) >= 0 || prospective.getBytes(StandardCharsets.UTF_8).length > LIMIT) {
                state.message = "Input exceeds 2 MiB or contains NUL"; updateStatus();
                return dest.subSequence(begin, finish);
            }
            return null;
        } });
        editor.setSaveEnabled(false); // Large drafts are stored as files, not in Binder bundles.
        root.addView(editor, new LinearLayout.LayoutParams(-1, 0, 1));
        editor.addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
                if (!rendering) snapshotUndo();
            }
            public void onTextChanged(CharSequence s, int start, int before, int count) { }
            public void afterTextChanged(Editable text) {
                if (rendering) return;
                state.text = text.toString();
                state.dirty = !state.text.equals(state.savedText);
                updateStatus();
            }
        });
        setContentView(root);
        if (retained == null) {
            try { DraftStore.load(getFilesDir(), state); }
            catch (Exception ex) { state.message = "Draft could not be restored: " + ex.getMessage(); }
        }
        render();
        if (android.os.Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::handleBack);
        }
    }
    private void addButton(String label, Runnable action) {
        Button button = new Button(this); button.setText(label);
        button.setOnClickListener(view -> { captureSelection(); action.run(); });
        toolbar.addView(button);
    }
    private void captureSelection() {
        state.start = Math.max(0, editor.getSelectionStart());
        state.end = Math.max(state.start, editor.getSelectionEnd());
    }
    private void snapshotUndo() {
        captureSelection(); state.undo = state.text;
        state.undoStart = state.start; state.undoEnd = state.end;
    }
    private void render() {
        rendering = true;
        if (!editor.getText().toString().equals(state.text)) editor.setText(state.text);
        int start = Math.min(state.start, state.text.length());
        int end = Math.max(start, Math.min(state.end, state.text.length()));
        editor.setSelection(start, end);
        editor.setEnabled(!state.busy);
        for (int i = 0; i < toolbar.getChildCount(); i++) toolbar.getChildAt(i).setEnabled(!state.busy);
        rendering = false; updateStatus();
    }
    private void updateStatus() {
        status.setText((state.dirty ? "● " : "") + (state.uri == null ? "Untitled" : "Document")
            + " — " + state.message);
    }
    private void busy() { state.busy = true; state.message = "Working…"; render(); }
    private static void complete(State state, Runnable change) {
        state.main.post(() -> {
            change.run(); state.busy = false;
            if (state.owner != null) state.owner.render();
        });
    }
    private static void fail(State state, Throwable ex) {
        complete(state, () -> state.message = "Failed: " + ex.getMessage());
    }
    private void confirmOpen() {
        if (!state.dirty) { picker(OPEN); return; }
        new AlertDialog.Builder(this).setMessage("Discard unsaved changes and open another file?")
            .setNegativeButton("Cancel", null).setPositiveButton("Discard", (d, w) -> picker(OPEN)).show();
    }
    private void picker(int request) {
        Intent intent = new Intent(request == SAVE_AS ? Intent.ACTION_CREATE_DOCUMENT : Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType(request == SAVE_AS ? "text/plain" : "*/*");
        if (request == SAVE_AS) intent.putExtra(Intent.EXTRA_TITLE, "untitled.txt");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, request);
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        if (request == OPEN || request == SAVE_AS) {
            int flags = data.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            if ((data.getFlags() & Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION) != 0) {
                try { getContentResolver().takePersistableUriPermission(uri, flags); }
                catch (SecurityException ignored) { /* Current session grant can still be used. */ }
            }
        }
        if (request == SAVE_AS) write(uri); else if (request == OPEN || request == LUA) read(uri, request == LUA);
    }
    private static byte[] readBytes(ContentResolver resolver, Uri uri, int limit) throws Exception {
        try (InputStream input = resolver.openInputStream(uri)) {
            if (input == null) throw new java.io.IOException("Cannot open file");
            ByteArrayOutputStream output = new ByteArrayOutputStream(); byte[] buffer = new byte[8192];
            int count;
            while ((count = input.read(buffer)) != -1) {
                if (output.size() > limit - count) throw new java.io.IOException("File exceeds size limit");
                output.write(buffer, 0, count);
            }
            return output.toByteArray();
        }
    }
    static String utf8(byte[] bytes) throws CharacterCodingException {
        String text = StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(bytes)).toString();
        if (text.indexOf(0) >= 0) throw new CharacterCodingException();
        return text;
    }
    private void read(Uri uri, boolean script) {
        State model = state; ContentResolver resolver = getApplicationContext().getContentResolver();
        String before = state.text; int start = state.start, end = state.end;
        busy();
        model.worker.execute(() -> {
            try {
                byte[] bytes = readBytes(resolver, uri, script ? 256 * 1024 : LIMIT);
                String decoded = utf8(bytes);
                if (!script) {
                    complete(model, () -> {
                        model.text = decoded; model.savedText = decoded; model.uri = uri.toString();
                        model.start = model.end = 0; model.undo = null; model.dirty = false; model.message = "Opened";
                    });
                    return;
                }
                byte[] document = before.getBytes(StandardCharsets.UTF_8);
                int first = before.substring(0, start).getBytes(StandardCharsets.UTF_8).length;
                int last = before.substring(0, end).getBytes(StandardCharsets.UTF_8).length;
                NativeBridge.Result result = NativeBridge.run(document, first, last, bytes);
                if (result == null) throw new java.io.IOException("Native runtime returned no result");
                String error = new String(result.error, StandardCharsets.UTF_8);
                if (!error.isEmpty()) throw new java.io.IOException(error);
                String after = utf8(result.text);
                int newStart = utf8(java.util.Arrays.copyOfRange(result.text, 0, result.start)).length();
                int newEnd = utf8(java.util.Arrays.copyOfRange(result.text, 0, result.end)).length();
                complete(model, () -> {
                    model.undo = before; model.undoStart = start; model.undoEnd = end;
                    model.text = after; model.start = newStart; model.end = newEnd;
                    model.dirty = !after.equals(model.savedText); model.message = "Lua completed";
                });
            } catch (Exception | LinkageError ex) { fail(model, ex); }
        });
    }
    private void save(boolean forceNew) {
        if (forceNew || state.uri == null) picker(SAVE_AS); else write(Uri.parse(state.uri));
    }
    private void write(Uri uri) {
        State model = state; String text = state.text;
        byte[] bytes = text.getBytes(StandardCharsets.UTF_8);
        if (bytes.length > LIMIT) { state.message = "Document exceeds 2 MiB"; updateStatus(); return; }
        ContentResolver resolver = getApplicationContext().getContentResolver(); busy();
        model.worker.execute(() -> {
            try (OutputStream output = resolver.openOutputStream(uri, "rwt")) {
                if (output == null) throw new java.io.IOException("Cannot write file; try Save as");
                output.write(bytes); output.flush();
            } catch (Exception ex) { fail(model, ex); return; }
            complete(model, () -> { model.uri = uri.toString(); model.savedText = text; model.dirty = false; model.message = "Saved"; });
        });
    }
    private void undo() {
        if (state.undo == null) { state.message = "Nothing to undo"; updateStatus(); return; }
        state.text = state.undo; state.start = state.undoStart; state.end = state.undoEnd;
        state.dirty = !state.text.equals(state.savedText); state.undo = null; state.message = "Undone"; render();
    }
    @Override public Object onRetainNonConfigurationInstance() { captureSelection(); return state; }
    private void checkpoint() {
        if (discarded) return;
        captureSelection();
        try { DraftStore.save(getFilesDir(), state); }
        catch (Exception ex) { state.message = "Draft checkpoint failed: " + ex.getMessage(); updateStatus(); }
    }
    @Override protected void onSaveInstanceState(Bundle out) { checkpoint(); super.onSaveInstanceState(out); }
    @Override protected void onStop() { checkpoint(); super.onStop(); }
    @Override protected void onDestroy() {
        if (state.owner == this) state.owner = null;
        if (!isChangingConfigurations()) state.worker.shutdown();
        super.onDestroy();
    }
    @SuppressWarnings("deprecation")
    @Override public void onBackPressed() { handleBack(); }
    private void handleBack() {
        if (state.busy) { state.message = "Wait for the operation to finish"; updateStatus(); return; }
        if (!state.dirty) { finish(); return; }
        new AlertDialog.Builder(this).setMessage("Discard unsaved changes and close?")
            .setNegativeButton("Cancel", null).setPositiveButton("Discard", (d, w) -> {
                DraftStore.clear(getFilesDir()); discarded = true; finish();
            }).show();
    }
}
