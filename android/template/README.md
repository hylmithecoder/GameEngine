# Android project template

Copied by `GameBuilder` (Build ▸ Android) into `<project>/build/android/<Game>/`
and filled in:

- `@APPLICATION_ID@`, `@APP_NAME@`, `@VERSION_NAME@`, `@VERSION_CODE@`
  in `app/build.gradle` and `app/src/main/res/values/strings.xml`
- `app/src/main/jniLibs/<abi>/` ← `build-android/jniLibs/` (libmain.so,
  libSDL3.so, libc++_shared.so from `scripts/android/build-player.sh`)
- `app/src/main/java/org/libsdl/` ← SDL3's Java glue (same SDL3 version)
- `app/src/main/assets/game/` ← the game folder + `ilmeee_files.txt`

Then `gradle assembleDebug` produces `app/build/outputs/apk/debug/app-debug.apk`.
