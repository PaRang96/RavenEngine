#pragma once

#if defined(_WIN32)
#if defined(RAVEN_CORE_EXPORTS)
#define RAVEN_API __declspec(dllexport)
#else
#define RAVEN_API __declspec(dllimport)
#endif
#else
#define RAVEN_API __attribute__((visibility("default")))
#endif

#if defined(_WIN32)
#if defined(RAVEN_GAME_EXPORTS)
#define RAVEN_GAME_API __declspec(dllexport)
#else
#define RAVEN_GAME_API __declspec(dllimport)
#endif
#else
#define RAVEN_GAME_API __attribute__((visibility("default")))
#endif
