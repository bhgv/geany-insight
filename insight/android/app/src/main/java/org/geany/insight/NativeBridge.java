// SPDX-License-Identifier: GPL-2.0-or-later
package org.geany.insight;

public final class NativeBridge {
    static { System.loadLibrary("insight_android"); }
    private NativeBridge() { }
    public static native Result run(byte[] text, int start, int end, byte[] script);
    public static final class Result {
        public final byte[] text, error;
        public final int start, end;
        public Result(byte[] text, byte[] error, int start, int end) {
            this.text = text; this.error = error; this.start = start; this.end = end;
        }
    }
}
