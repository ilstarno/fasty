/*
 * @Author: Indrit Zeqiri 
 * @Date: 2018-11-26 15:44:34 
 * @Last Modified by:   Indrit Zeqiri 
 * @Last Modified time: 2018-11-26 15:44:34 
 */

#ifndef FASTY_LIB_H
#define FASTY_LIB_H


#define ALWAYS(str, ...)        log_write(LOG_ALWAYS, str, ##__VA_ARGS__)
#define ERR(str, ...)           log_write(LOG_ERR, str, ##__VA_ARGS__)
#define WAR(str, ...)           log_write(LOG_WAR, str, ##__VA_ARGS__)
#define INF(str, ...)           log_write(LOG_INF, str, ##__VA_ARGS__)
#define DEBUG(str, ...)           log_write(LOG_DEBUG, str, ##__VA_ARGS__)

#define ALWAYS_T(str, ...)      log_write_time(LOG_ALWAYS, str, ##__VA_ARGS__)
#define ERR_T(str, ...)         log_write_time(LOG_ERR, str, ##__VA_ARGS__)
#define WAR_T(str, ...)         log_write_time(LOG_WAR, str, ##__VA_ARGS__)
#define INF_T(str, ...)         log_write_time(LOG_INF, str, ##__VA_ARGS__)
#define DEBUG_T(str, ...)         log_write_time(LOG_DEBUG, str, ##__VA_ARGS__)

#define LOG_LINE                "--------------------------------------------------"
#define LOG_LINE_N              "--------------------------------------------------\n"
#define LOG_LINE_NN             "--------------------------------------------------\n\n"
#define ALWAYS_LINE             ALWAYS(LOG_LINE)
#define INF_LINE                INF(LOG_LINE)
#define DEBUG_LINE                DEBUG(LOG_LINE)

#define LOG_LINE_LONG           "--------------------------------------------------------------------------------------------------"
#define LOG_LINE_LONG_N         "--------------------------------------------------------------------------------------------------\n"
#define LOG_LINE_LONG_NN        "--------------------------------------------------------------------------------------------------\n\n"
#define ALWAYS_LINE_LONG        ALWAYS(LOG_LINE_LONG)
#define INF_LINE_LONG           INF(LOG_LINE_LONG)
#define DEBUG_LINE_LONG           DEBUG(LOG_LINE_LONG)

#define LOREM_IPSUM             "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum."

#define PARAM(param)            (0==strcmp(label, param))


/* Query String Value */

typedef char                    QSVAL[QSBUF];
//typedef struct QSVAL          { char x[QSBUF]; } QSVAL;


#ifdef APP_EMAIL_FROM_USER
#define EMAIL_FROM_USER         APP_EMAIL_FROM_USER
#else
#define EMAIL_FROM_USER         "noreply"
#endif


#define CONNECT 0
#define READ    1
#define WRITE   2



/* REST calls */

typedef struct {
    char    key[64];
    char    value[256];
} rest_header_t;


#define REST_MAX_HEADERS                            100
#define REST_HEADERS_RESET                          library_rest_headers_reset()
#define REST_HEADER_SET(key, val)                   library_rest_header_set(key, val)
#define REST_HEADER_UNSET(key)                      library_rest_header_unset(key)


#ifdef JSON_NO_AUTO_AMPERSANDS
#define CALL_REST_HTTP(req, res, method, url, keep) library_rest_req(req, res, method, url, FALSE, keep)
#define CALL_REST_JSON(req, res, method, url, keep) library_rest_req(req, res, method, url, TRUE, keep)
#else
#define CALL_REST_HTTP(req, res, method, url, keep) library_rest_req((char*)req, (char*)res, method, url, FALSE, keep)
#define CALL_REST_JSON(req, res, method, url, keep) library_rest_req(&req, &res, method, url, TRUE, keep)
#endif

/* aliases -- highest level -- 'keep' always TRUE */
#define CALL_REST_RAW(req, res, method, url)        CALL_REST_HTTP(req, res, method, url, TRUE)
#define CALL_HTTP(req, res, method, url)            CALL_REST_HTTP(req, res, method, url, TRUE)
#define CALL_REST(req, res, method, url)            CALL_REST_JSON(req, res, method, url, TRUE)


#define CALL_REST_DEFAULT_TIMEBRING                   1000     /* in ms -- to avoid blocking forever */

#define REST_RES_HEADER_LEN                         4095
#define REST_ADDRESSES_CACHE_SIZE                   100


#define CALL_HTTP_STATUS                            G_rest_status
#define CALL_REST_STATUS                            CALL_HTTP_STATUS
#define CALL_HTTP_CONTENT_TYPE                      G_rest_content_type
#define CALL_REST_CONTENT_TYPE                      CALL_HTTP_CONTENT_TYPE


/* JSON */

#define JSON_STRING             0
#define JSON_INTEGER            1
#define JSON_FLOAT              2
#define JSON_BOOL               3
#define JSON_RECORD             4
#define JSON_ARRAY              5

#define JSON_KEY_LEN            31
#define JSON_VAL_LEN            255

#ifdef APP_JSON_MAX_ELEMS       /* in one JSON struct */
#define JSON_MAX_ELEMS          APP_JSON_MAX_ELEMS
#else
#define JSON_MAX_ELEMS          10
#endif

#ifdef APP_JSON_MAX_LEVELS
#define JSON_MAX_LEVELS         APP_JSON_MAX_LEVELS
#else
#define JSON_MAX_LEVELS         4
#endif

#define JSON_MAX_JSONS          1000    /* size of the array used for auto-initializing JSON variables */
#define JSON_POOL_SIZE          1000    /* for storing sub-JSONs */


/* single JSON element */

typedef struct {
    char    name[JSON_KEY_LEN+1];
    char    value[JSON_VAL_LEN+1];
    char    type;
} json_elem_t;

/* JSON object */

typedef struct {
    int         cnt;
    char        array;
    json_elem_t rec[JSON_MAX_ELEMS];
} json_t;

typedef json_t JSON;


#ifdef APP_JSON_BUFSIZE
#define JSON_BUFSIZE                        APP_JSON_BUFSIZE
#else
#define JSON_BUFSIZE                        65568
#endif


#ifdef JSON_NO_AUTO_AMPERSANDS

#define JSON_TO_STRING(json)                library_json_to_string(json)
#define JSON_TO_STRING_PRETTY(json)         library_json_to_string_pretty(json)
#define JSON_FROM_STRING(json, str)         library_json_from_string(json, str, 0, 0)


#define JSON_ADD_STR(json, name, val)       library_json_add(json, name, val, 0, 0, JSON_STRING, -1)
#define JSON_ADD_STR_A(json, i, val)        library_json_add(json, NULL, val, 0, 0, JSON_STRING, i)
#define JSON_ADD_INT(json, name, val)       library_json_add(json, name, NULL, val, 0, JSON_INTEGER, -1)
#define JSON_ADD_INT_A(json, i, val)        library_json_add(json, NULL, NULL, val, 0, JSON_INTEGER, i)
#define JSON_ADD_FLOAT(json, name, val)     library_json_add(json, name, NULL, 0, val, JSON_FLOAT, -1)
#define JSON_ADD_FLOAT_A(json, i, val)      library_json_add(json, NULL, NULL, 0, val, JSON_FLOAT, i)
#define JSON_ADD_BOOL(json, name, val)      library_json_add(json, name, NULL, val, 0, JSON_BOOL, -1)
#define JSON_ADD_BOOL_A(json, i, val)       library_json_add(json, NULL, NULL, val, 0, JSON_BOOL, i)

#define JSON_ADD_RECORD(json, name, val)    library_json_add_record(json, name, val, FALSE, -1)
#define JSON_ADD_RECORD_A(json, i ,val)     library_json_add_record(json, NULL, val, FALSE, i)

#define JSON_ADD_ARRAY(json, name, val)     library_json_add_record(json, name, val, TRUE, -1)
#define JSON_ADD_ARRAY_A(json, i, val)      library_json_add_record(json, NULL, val, TRUE, i)


#define JSON_GET_STR(json, name)            library_json_get_str(json, name, -1)
#define JSON_GET_STR_A(json, i)             library_json_get_str(json, NULL, i)
#define JSON_GET_INT(json, name)            library_json_get_int(json, name, -1)
#define JSON_GET_INT_A(json, i)             library_json_get_int(json, NULL, i)
#define JSON_GET_FLOAT(json, name)          library_json_get_float(json, name, -1)
#define JSON_GET_FLOAT_A(json, i)           library_json_get_float(json, NULL, i)
#define JSON_GET_BOOL(json, name)           library_json_get_bool(json, name, -1)
#define JSON_GET_BOOL_A(json, i)            library_json_get_bool(json, NULL, i)

#define JSON_GET_RECORD(json, name, val)    library_json_get_record(json, name, val, -1)
#define JSON_GET_RECORD_A(json, i, val)     library_json_get_record(json, NULL, val, i)

#define JSON_GET_ARRAY(json, name, val)     library_json_get_record(json, name, val, -1)
#define JSON_GET_ARRAY_A(json, i, val)      library_json_get_record(json, NULL, val, i)


#define JSON_RESET(json)                    library_json_reset(json)
#define JSON_COUNT(json)                    json.cnt

#define JSON_LOG_DEBUG(json, name)            library_json_log_dbg(json, name)
#define JSON_LOG_INF(json, name)            library_json_log_inf(json, name)

#else  /* JSON_NO_AUTO_AMPERSANDS not defined */

#define JSON_TO_STRING(json)                library_json_to_string(&json)
#define JSON_TO_STRING_PRETTY(json)         library_json_to_string_pretty(&json)
#define JSON_FROM_STRING(json, str)         library_json_from_string(&json, str, 0, 0)


#define JSON_ADD_STR(json, name, val)       library_json_add(&json, name, val, 0, 0, JSON_STRING, -1)
#define JSON_ADD_STR_A(json, i, val)        library_json_add(&json, NULL, val, 0, 0, JSON_STRING, i)
#define JSON_ADD_INT(json, name, val)       library_json_add(&json, name, NULL, val, 0, JSON_INTEGER, -1)
#define JSON_ADD_INT_A(json, i, val)        library_json_add(&json, NULL, NULL, val, 0, JSON_INTEGER, i)
#define JSON_ADD_FLOAT(json, name, val)     library_json_add(&json, name, NULL, 0, val, JSON_FLOAT, -1)
#define JSON_ADD_FLOAT_A(json, i, val)      library_json_add(&json, NULL, NULL, 0, val, JSON_FLOAT, i)
#define JSON_ADD_BOOL(json, name, val)      library_json_add(&json, name, NULL, val, 0, JSON_BOOL, -1)
#define JSON_ADD_BOOL_A(json, i, val)       library_json_add(&json, NULL, NULL, val, 0, JSON_BOOL, i)

#define JSON_ADD_RECORD(json, name, val)    library_json_add_record(&json, name, &val, FALSE, -1)
#define JSON_ADD_RECORD_A(json, i, val)     library_json_add_record(&json, NULL, &val, FALSE, i)

#define JSON_ADD_ARRAY(json, name, val)     library_json_add_record(&json, name, &val, TRUE, -1)
#define JSON_ADD_ARRAY_A(json, i, val)      library_json_add_record(&json, NULL, &val, TRUE, i)


#define JSON_GET_STR(json, name)            library_json_get_str(&json, name, -1)
#define JSON_GET_STR_A(json, i)             library_json_get_str(&json, NULL, i)
#define JSON_GET_INT(json, name)            library_json_get_int(&json, name, -1)
#define JSON_GET_INT_A(json, i)             library_json_get_int(&json, NULL, i)
#define JSON_GET_FLOAT(json, name)          library_json_get_float(&json, name, -1)
#define JSON_GET_FLOAT_A(json, i)           library_json_get_float(&json, NULL, i)
#define JSON_GET_BOOL(json, name)           library_json_get_bool(&json, name, -1)
#define JSON_GET_BOOL_A(json, i)            library_json_get_bool(&json, NULL, i)

#define JSON_GET_RECORD(json, name, val)    library_json_get_record(&json, name, &val, -1)
#define JSON_GET_RECORD_A(json, i, val)     library_json_get_record(&json, NULL, &val, i)

#define JSON_GET_ARRAY(json, name, val)     library_json_get_record(&json, name, &val, -1)
#define JSON_GET_ARRAY_A(json, i, val)      library_json_get_record(&json, NULL, &val, i)


#define JSON_RESET(json)                    library_json_reset(&json)
#define JSON_COUNT(json)                    json.cnt

#define JSON_LOG_DEBUG(json, name)            library_json_log_dbg(&json, name)
#define JSON_LOG_INF(json, name)            library_json_log_inf(&json, name)

#endif  /* JSON_NO_AUTO_AMPERSANDS */

/* for backward compatibility */
#define fasty_read_param(param, val)        fasty_read_param_str(param, val)


#ifdef __cplusplus
extern "C" {
#endif
    void fasty_library_initialization(void);
    bool library_exists(const char *fname);
    void library_get_execution_name(char *dst, const char *path);
    void library_update_time_globals(void);
    void library_setnonblocking(int sock);
    void library_rest_headers_reset(void);
    void library_rest_header_set(const char *key, const char *value);
    void library_rest_header_unset(const char *key);
    bool library_rest_req(const void *req, void *res, const char *method, const char *url, bool json, bool keep);
    int library_finish_with_timebring(int sock, char readwrite, char *buffer, int len, int *msec, void *ssl, int level);
    void library_get_app_dir(void);
    double library_elapsed(struct timespec *start);
    long library_get_memory(void);
    void library_log_memory(void);
    char *fasty_filter_strict(const char *src);
    char *library_add_spaces(const char *src, int len);
    char *library_add_lspaces(const char *src, int len);
    char get_res_type(const char *fname);
    void date_str2rec(const char *str, date_t *rec);
    void date_rec2str(char *str, date_t *rec);
    time_t time_http2epoch(const char *str);
    time_t time_db2epoch(const char *str);
    char *time_epoch2http(time_t epoch);
    void library_set_datetime_formats(const char *lang);
    void amt(char *stramt, long in_amt);
    void amtd(char *stramt, double in_amt);
    void library_amt(char *stramt, long in_amt);
    void library_amtd(char *stramt, double in_amt);
    void samts(char *stramt, const char *in_amt);
    void library_normalize_float(char *str);
    void ftm(char *strtm, long in_tm);
    char const *san(const char *str);
    char *san_long(const char *str);
    char *fasty_sql_esc(const char *str);
    char *fasty_html_esc(const char *str);
    char *fasty_html_unesc(const char *str);
    char *uri_encode(const char *str);
    char *upper(const char *str);
    char *stp_right(char *str);
    bool strdigits(const char *src);
    char *nospaces(char *dst, const char *src);
    void fasty_random(char *dest, int len);
    void msleep(int msec);
    void library_json_reset(JSON *json);
    char *library_json_to_string(JSON *json);
    char *library_json_to_string_pretty(JSON *json);
    bool library_json_from_string(JSON *json, const char *src, int len, int level);
    bool library_json_add(JSON *json, const char *name, const char *str_value, long int_value, double flo_value, char type, int i);
    bool library_json_add_record(JSON *json, const char *name, JSON *json_sub, bool is_array, int i);
    bool library_json_get(JSON *json, const char *name, char *str_value, long *num_value, char type);
    char *library_json_get_str(JSON *json, const char *name, int i);
    long library_json_get_int(JSON *json, const char *name, int i);
    double library_json_get_float(JSON *json, const char *name, int i);
    bool library_json_get_bool(JSON *json, const char *name, int i);
    bool library_json_get_record(JSON *json, const char *name, JSON *json_sub, int i);
    void library_json_log_dbg(JSON *json, const char *name);
    void library_json_log_inf(JSON *json, const char *name);
    void get_byteorder(void);
    time_t db2epoch(const char *str);
    bool fasty_email(const char *to, const char *subject, const char *message);
    int fasty_minify(char *dest, const char *src);
    void date_inc(char *str, int days, int *dow);
    int date_cmp(const char *str1, const char *str2);
    bool library_read_conf(const char *file);
    bool fasty_read_param_str(const char *param, char *dest);
    bool fasty_read_param_int(const char *param, int *dest);
    char *library_create_pid_file(const char *name);
	bool library_shm_create(long bytes);
	void library_shm_delete(long bytes);
    bool log_start(const char *prefix, bool test);
    void log_write_time(int level, const char *message, ...);
    void log_write(int level, const char *message, ...);
    void log_long(const char *str, long len, const char *desc);
    void log_flush(void);
    void log_finish(void);
#ifdef ICONV
    char *fasty_convert(char *src, const char *cp_from, const char *cp_to);
#endif

    int Base64encode_len(int len);
    int Base64encode(char * coded_dst, const char *plain_src,int len_plain_src);
    int Base64decode_len(const char * coded_src);
    int Base64decode(char * plain_dst, const char *coded_src);

#ifdef _WIN32   /* Windows */
    int getpid(void);
    int clock_gettime(int dummy, struct timespec *spec);
    char *stpcpy(char *dest, const char *src);
    char *stpncpy(char *dest, const char *src, unsigned long len);
#endif  /* _WIN32 */

#ifndef strnstr
    char *strnstr(const char *haystack, const char *needle, size_t len);
#endif

#ifdef __cplusplus
}   // extern "C"
#endif


#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t  buffer[64];
} SHA1_CTX;

#define SHA1_DIGEST_SIZE 20

void libSHA1(unsigned char *ptr, unsigned int size, unsigned char *bringbuf);

void digest_to_hex(const uint8_t digest[SHA1_DIGEST_SIZE], char *bringput);


#ifdef __cplusplus
}
#endif

#endif  /* FASTY_LIB_H */
