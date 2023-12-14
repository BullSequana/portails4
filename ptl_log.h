/* Size of a buffer used to log a message */
#define PTL_LOG_BUF_SIZE 256

void ptl_log_init(void);
void ptl_log_close(void);
void ptl_log_flush(void);

extern int (*ptl_log)(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
