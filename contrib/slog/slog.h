#ifndef SKYBOX_LOG_H
#define SKYBOX_LOG_H

extern "C" {

    enum {
        L_FATAL,     // only errors (difference to MSGL_ERR isn't too clear)
        L_ERR,       // only errors
        L_WARN,      // only warnings
        L_INFO,      // what you normally see on the terminal
        L_STATUS,    // exclusively for the playback status line (-quiet disables)
        L_V,         // -v | slightly more information than default
        L_DEBUG,     // -v -v | full debug information; this and numerically below
        // should not produce "per frame" output
        L_TRACE,     // -v -v -v | anything that might flood the terminal
        L_STATS,     // dumping fine grained stats (--dump-stats)

        L_MAX = L_STATS,
    };

    void* slog_register(void* parent, const char* name);
    int slog_set_option(char* filter, char* path);
    int slog_terminate();
    void slog(void* log, int lev, const char* format, ...);
    void slog_tag(const char* tag, int lev, const char* format, ...);
    void slog_unity(void* log, int lev, const char* format);

}
#endif /* SKYBOX_LOG_H */
