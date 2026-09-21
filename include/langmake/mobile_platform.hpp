#pragma once
// CMake defines LANGMAKE_IOS only for the iOS application target and its core.
#if defined(__ANDROID__) || defined(LANGMAKE_IOS)
#define LANGMAKE_MOBILE 1
#endif
