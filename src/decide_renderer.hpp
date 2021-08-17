/// Decide the rendering protocol.

#ifdef Q_OS_WIN
#define USE_DIRECT2D 1
#else
#define USE_QPAINTER 1
#endif
