#pragma once
// Shader selection is a renderer adapter detail. Game sources and Studio never
// reference GPU bytecode formats. Generated model shaders use one native symbol.
#if defined(__ANDROID__)
#define LM_SHADER(name) name##_essl
#elif defined(LANGMAKE_IOS) || defined(__APPLE__)
#define LM_SHADER(name) name##_mtl
#else
#define LM_SHADER(name) name##_dxbc
#endif
