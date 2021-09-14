/// Decide the rendering protocol.

#if !defined(USE_DIRECT2D) && !defined(USE_QPAINTER)
#ifdef Q_OS_WIN
#define USE_DIRECT2D 1
#else
#define USE_QPAINTER 1
#endif // Q_OS_WIN

#endif // !defined(USE_DIRECT2D) && !defined(USE_QPAINTER)
