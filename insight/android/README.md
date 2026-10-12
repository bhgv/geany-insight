# Android native core
This is NOT an Android editor or APK yet. No Java/Kotlin UI or JNI bridge exists.
The C host callbacks deliberately have no GTK dependency and can be implemented
by a future Android document backend. NDK builds the same Lua source/runtime.

```sh
cmake -S insight -B build-android \
 -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
 -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26
cmake --build build-android
```
NDK compilation has not been verified in the authoring environment.
