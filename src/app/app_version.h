#pragma once

#if !defined(TERMINAL_VERSION_MAJOR) || !defined(TERMINAL_VERSION_MINOR) || \
    !defined(TERMINAL_VERSION_PATCH) || !defined(TERMINAL_VERSION_BUILD) || \
    !defined(TERMINAL_VERSION_COMMA) || !defined(TERMINAL_VERSION_STRING) || \
    !defined(TERMINAL_VERSION_STRING_W) || !defined(TERMINAL_FILE_VERSION_STRING)
#error Terminal version definitions must come from version.props through MSBuild.
#endif
