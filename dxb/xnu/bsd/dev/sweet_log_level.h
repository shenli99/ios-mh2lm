/* MH2LM driver log severity. The existing console remains the transport. */
#ifndef _SWEET_LOG_LEVEL_H_
#define _SWEET_LOG_LEVEL_H_

#ifdef __cplusplus
extern "C" {
#endif
int sweet_log_level_enabled(unsigned level);
#ifdef __cplusplus
}
#endif

/* C drivers use printf; C++ callers may define SWEET_LOG_SINK as IOLog. */
#ifndef SWEET_LOG_SINK
#define SWEET_LOG_SINK printf
#endif

/* ERROR is unconditional; severity filtering must never hide a failure. */
#define SWEET_LOG_ERROR(...) do { \
	SWEET_LOG_SINK("[ERROR] " __VA_ARGS__); \
} while (0)
#define SWEET_LOG_WARN(...) do { \
	if (sweet_log_level_enabled(1u)) { \
		SWEET_LOG_SINK("[WARN] " __VA_ARGS__); \
	} \
} while (0)
#define SWEET_LOG_INFO(...) do { \
	if (sweet_log_level_enabled(2u)) { \
		SWEET_LOG_SINK("[INFO] " __VA_ARGS__); \
	} \
} while (0)
#define SWEET_LOG_DEBUG(...) do { \
	if (sweet_log_level_enabled(3u)) { \
		SWEET_LOG_SINK("[DEBUG] " __VA_ARGS__); \
	} \
} while (0)

#endif /* _SWEET_LOG_LEVEL_H_ */
