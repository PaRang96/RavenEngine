# The text and layout libraries in a directory of their own, so RAVEN_TEXT_OPTIMIZE_DEBUG can give them
# optimized Debug flags without touching the engine's (cmake/RavenText.cmake).
raven_add_freetype()
raven_add_harfbuzz()
raven_add_msdf()
raven_add_yoga()
raven_add_icu()
