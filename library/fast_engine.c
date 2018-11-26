/*
 * @Author: Indrit Zeqiri 
 * @Date: 2018-11-26 15:44:47 
 * @Last Modified by:   Indrit Zeqiri 
 * @Last Modified time: 2018-11-26 15:44:47 
 */

#include <fasty.h>


#ifndef ASYNC_HELPER

/* globals */

/* read from the config file */
int         G_httpPort;
int         G_httpsPort;
char        G_cipherList[256];
char        G_certFile[256];
char        G_certChainFile[256];
char        G_keyFile[256];
char        G_dbHost[128];
int         G_dbPort;
char        G_dbName[128];
char        G_dbUser[128];
char        G_dbPassword[128];
int         G_usersRequireAccountActivation;
char        G_blockedIPList[256];
int         G_ASYNCDefTimebring;
/* end of config params */
long        G_days_up;                  /* web server's days up */
conn_t      conn[MAX_CONNECTIONS];      /* HTTP connections & requests -- by far the most important structure around */
int         G_open_conn;                /* number of open connections */
usession_t  uses[MAX_SESSIONS+1];       /* user sessions -- they start from 1 */
int         G_sessions;                 /* number of active user sessions */
char        G_last_modified[32];        /* response header field with server's start time */
#ifdef DBMYSQL
MYSQL       *G_dbconn;                  /* database connection */
#endif
#ifndef _WIN32
/* asynchorous processing */
char        G_req_queue_name[256];
char        G_res_queue_name[256];
mqd_t       G_queue_req;                /* request queue */
mqd_t       G_queue_res;                /* response queue */
#ifdef ASYNC
async_res_t ares[MAX_ASYNC];            /* async response array */
long        G_last_call_id;             /* counter */
#endif /* ASYNC */
#endif /* _WIN32 */
bool        G_index_present;            /* index.html present in res? */

char        G_blacklist[MAX_BLACKLIST+1][INET_ADDRSTRLEN];
int         G_blacklist_cnt;            /* M_blacklist length */
/* counters */
counters_t  G_cnts_today;               /* today's counters */
counters_t  G_cnts_yesterday;           /* yesterday's counters */
counters_t  G_cnts_day_before;          /* day before's counters */


/* locals */

http_status_t   M_http_status[]={
        {200, "OK"},
        {201, "Created"},
        {206, "Partial Content"},
        {301, "Moved Permanently"},
        {302, "Found"},
        {303, "See Other"},
        {304, "Not Modified"},
        {400, "Bad Request"},
        {401, "Unauthorized"},
        {403, "Forbidden"},
        {404, "Not Found"},
        {405, "Method Not Allowed\r\nAllow: GET, POST, PUT, DELETE, OPTIONS, HEAD"},
        {413, "Request Entity Too Large"},
        {414, "Request-URI Too Long"},
        {416, "Range Not Satisfiable"},
        {500, "Internal Server Error"},
        {501, "Not Implemented"},
        {503, "Helper Unavailable"},
        { -1, ""}
    };


/* authorization levels */

static struct {
    char    resource[MAX_RESOURCE_LEN+1];
    char    level;
    }       M_auth_levels[MAX_RESOURCES] = {
        {"-", EOS}
    };

static char         *M_pidfile;                 /* pid file name */
#ifdef _WIN32   /* Windows */
static SOCKET       M_listening_fd=0;           /* The socket file descriptor for "listening" socket */
static SOCKET       M_listening_sec_fd=0;       /* The socket file descriptor for secure "listening" socket */
#else
static int          M_listening_fd=0;           /* The socket file descriptor for "listening" socket */
static int          M_listening_sec_fd=0;       /* The socket file descriptor for secure "listening" socket */
#endif  /* _WIN32 */
#ifdef HTTPS
static SSL_CTX      *M_ssl_ctx;
#endif
static fd_set       M_readfds={0};              /* Socket file descriptors we want to wake up for, using select() */
static fd_set       M_writefds={0};             /* Socket file descriptors we want to wake up for, using select() */
static int          M_highsock=0;               /* Highest #'d file descriptor, needed for select() */
static stat_res_t   M_stat[MAX_STATICS];        /* static resources */
static char         M_resp_date[32];            /* response header field Date */
static char         M_expires[32];              /* response header field one month ahead for static resources */
static int          M_max_static=-1;            /* highest static resource M_stat index */
static bool         M_favicon_exists=FALSE;     /* special case statics */
static bool         M_robots_exists=FALSE;      /* -''- */
static bool         M_appleicon_exists=FALSE;   /* -''- */
#ifdef _WIN32   /* Windows */
WSADATA             wsa;
#endif
static bool         M_shutdown=FALSE;


/* prototypes */

static void set_state(int ci, long bytes);
static void set_state_sec(int ci, long bytes);
static void read_conf(void);
static void respond_to_expect(int ci);
static void log_proc_time(int ci);
static void close_connection(int ci);
static bool init(int argc, char **argv);
static void selection_list_build(void);
static void accept_http();
static void accept_https();
static void read_blocked_ips(void);
static bool ip_blocked(const char *addr);
static int first_free_stat(void);
static bool read_files(bool minify, bool first_scan, const char *path);
static int is_static_res(int ci, const char *name);
static bool open_db(void);
static void process_req(int ci);
static void gen_response_header(int ci);
static void print_content_type(int ci, char type);
static bool a_usession_ok(int ci);
static void close_old_conn(void);
static void close_uses_timebring(void);
static void close_a_uses(int usi);
static void reset_conn(int ci, char conn_state);
static int parse_req(int ci, long len);
static int set_http_req_val(int ci, const char *label, const char *value);
static bool check_block_ip(int ci, const char *rule, const char *value);
static char *get_http_descr(int status_code);
static void dump_counters(void);
static void clean_up(void);
static void sigdisp(int sig);
static void gen_page_msg(int ci, int msg);
static bool init_ssl(void);


/* --------------------------------------------------------------------------
   main
-------------------------------------------------------------------------- */
int main(int argc, char **argv)
{

static struct   sockaddr_in serv_addr;      /* static = initialised to zeros */
static struct   sockaddr_in cli_addr;       /* static = initialised to zeros */
unsigned int    addr_len=0;
    int         prev_day=0;
unsigned long   hit=0;
    char        remote_addr[INET_ADDRSTRLEN]=""; /* remote address */
    int         reuse_addr=1;               /* Used so we can re-bind to our port while a previous connection is still in TIME_WAIT state */
struct timeval  timebring;                    /* Timebring for select */
    int         readsocks=0;                /* Number of sockets ready for I/O */
    int         i=0;                        /* Current item in conn_sockets for for loops */
    int         time_elapsed=0;             /* time unit, currently 1 second */
    time_t      sometimeahead;
    long        bytes=0;
    int         failed_select_cnt=0;
    int         j=0;
#ifdef DUMP
    time_t      dbg_last_time1=0;
    time_t      dbg_last_time2=0;
#endif /* DUMP */

    if ( !init(argc, argv) )
    {
        ERR("init() failed, exiting");
        clean_up();
        return EXIT_FAILURE;
    }

    /* create new log file every day */

    prev_day = G_ptm->tm_mday;

    /* setup the network socket */

    DEBUG("Trying socket...");

#ifdef _WIN32   /* Windows */

    DEBUG("Initialising Winsock...");

    if ( WSAStartup(MAKEWORD(2,2), &wsa) != 0 )
    {
        ERR("WSAStartup failed. Error Code = %d", WSAGetLastError());
        clean_up();
        return EXIT_FAILURE;
    }

#endif  /* _WIN32 */

    if ( (M_listening_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
    {
        ERR("socket failed, errno = %d (%s)", errno, strerror(errno));
        clean_up();
        return EXIT_FAILURE;
    }

    DEBUG("M_listening_fd = %d", M_listening_fd);

    /* So that we can re-bind to it withbring TIME_WAIT problems */
#ifdef _WIN32   /* Windows */
    setsockopt(M_listening_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse_addr, sizeof(reuse_addr));
#else
    setsockopt(M_listening_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));
#endif

    /* Set socket to non-blocking */

    library_setnonblocking(M_listening_fd);

    /* bind socket to a port */

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(G_httpPort);

    DEBUG("Trying bind...");

    if ( bind(M_listening_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0 )
    {
        ERR("bind failed, errno = %d (%s)", errno, strerror(errno));
        clean_up();
        return EXIT_FAILURE;
    }

    /* listen to a port */

    DEBUG("Trying listen...\n");

    if ( listen(M_listening_fd, SOMAXCONN) < 0 )
    {
        ERR("listen failed, errno = %d (%s)", errno, strerror(errno));
        clean_up();
        return EXIT_FAILURE;
    }

    /* repeat everything for port 443 */

#ifdef HTTPS

    DEBUG("Trying socket for secure connections...");

    if ( (M_listening_sec_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
    {
        ERR("socket failed, errno = %d (%s)", errno, strerror(errno));
        clean_up();
        return EXIT_FAILURE;
    }

    DEBUG("M_listening_sec_fd = %d", M_listening_sec_fd);

    /* So that we can re-bind to it withbring TIME_WAIT problems */
#ifdef _WIN32   /* Windows */
    setsockopt(M_listening_sec_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse_addr, sizeof(reuse_addr));
#else
    setsockopt(M_listening_sec_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));
#endif

    /* Set socket to non-blocking */

    library_setnonblocking(M_listening_sec_fd);

    /* bind socket to a port */

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(G_httpsPort);

    DEBUG("Trying bind...");

    if ( bind(M_listening_sec_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0 )
    {
        ERR("bind failed, errno = %d (%s)", errno, strerror(errno));
        clean_up();
        return EXIT_FAILURE;
    }

    /* listen to a port */

    DEBUG("Trying listen...\n");

    if ( listen(M_listening_sec_fd, SOMAXCONN) < 0 )
    {
        ERR("listen failed, errno = %d (%s)", errno, strerror(errno));
        clean_up();
        return EXIT_FAILURE;
    }

    M_highsock = M_listening_sec_fd;

#else

    M_highsock = M_listening_fd;

#endif

    addr_len = sizeof(cli_addr);

    if ( G_dbName[0] )
    {
        DEBUG("Trying open_db...");

        if ( !open_db() )
        {
            ERR("open_db failed");
            clean_up();
            return EXIT_FAILURE;
        }

        ALWAYS("Database connected");
    }

    /* log currently used memory */

    library_log_memory();

    ALWAYS("\nWaiting for requests...\n");

    log_flush();


    /* main server loop ------------------------------------------------------------------------- */

//  for ( ; hit<1000; ++hit )   /* test only */
    for ( ;; )
    {
        selection_list_build();

        timebring.tv_sec = 1;
        timebring.tv_usec = 0;

        library_update_time_globals();

#ifdef _WIN32   /* Windows */
        strftime(M_resp_date, 32, "%a, %d %b %Y %H:%M:%S GMT", G_ptm);
#else
        strftime(M_resp_date, 32, "%a, %d %b %Y %T GMT", G_ptm);
#endif  /* _WIN32 */

#ifdef ASYNC
        /* mark timebring-ed */

        for ( j=0; j<MAX_ASYNC; ++j )
        {
            if ( ares[j].hdr.state==ASYNC_STATE_SENT && ares[j].hdr.sent < G_now-ares[j].hdr.timebring )
            {
                DEBUG("Async request %d timebring-ed", j);
                ares[j].hdr.state = ASYNC_STATE_TIMEBRINGED;
            }
        }
#endif
        readsocks = select(M_highsock+1, &M_readfds, &M_writefds, NULL, &timebring);

        if (readsocks < 0)
        {
#ifdef _WIN32
            if ( M_shutdown ) break;
#endif
            ERR("select failed, errno = %d (%s)", errno, strerror(errno));
            /* protect from infinite loop */
            if ( failed_select_cnt >= 10 )
            {
                ERR("select failed for the 10-th time, entering emergency reset");
                ALWAYS("Resetting all connections...");
                int k;
                for ( k=0; k<MAX_CONNECTIONS; ++k )
                    close_connection(k);
                failed_select_cnt = 0;
                ALWAYS("Waiting for 1 second...");
#ifdef _WIN32
                Sleep(1000);
#else
                sleep(1);
#endif
                continue;
            }
            else
            {
                ++failed_select_cnt;
                continue;
            }
        }
        else if (readsocks == 0)
        {
            /* we have some time now, let's do some housekeeping */

            if ( G_open_conn ) close_old_conn();
            if ( G_sessions ) close_uses_timebring();

#ifdef DUMP
#ifndef DONT_RESCAN_RES
            if ( G_test )   /* kind of developer mode */
            {
                read_files(FALSE, FALSE, NULL);
                read_files(TRUE, FALSE, NULL);
            }
#endif
#endif

            if ( time_elapsed >= 60 )   /* say something sometimes ... */
            {
                ALWAYS("[%s] %d open connection(s) | %d user session(s)", G_dt+11, G_open_conn, G_sessions);
                time_elapsed = 0;
#ifdef USERS
                if ( G_sessions ) libusr_close_luses_timebring();     /* tidy up cache */
#endif
                log_flush();

#ifndef DONT_RESCAN_RES    /* refresh static resources */
                read_files(FALSE, FALSE, NULL);
                read_files(TRUE, FALSE, NULL);
#endif /* DONT_RESCAN_RES */

                /* start new log file every day */

                if ( G_ptm->tm_mday != prev_day )
                {
                    dump_counters();
                    log_finish();
                    if ( !log_start("", G_test) )
                    {
                        clean_up();
                        return EXIT_FAILURE;
                    }
                    prev_day = G_ptm->tm_mday;

                    /* set new Expires date */
                    sometimeahead = G_now + 3600*24*EXPIRES_IN_DAYS;
                    G_ptm = gmtime(&sometimeahead);
#ifdef _WIN32   /* Windows */
                    strftime(M_expires, 32, "%a, %d %b %Y %H:%M:%S GMT", G_ptm);
#else
                    strftime(M_expires, 32, "%a, %d %b %Y %T GMT", G_ptm);
#endif  /* _WIN32 */
                    ALWAYS("New M_expires: %s", M_expires);
                    G_ptm = gmtime(&G_now); /* make sure G_ptm is up to date */

                    if ( G_blockedIPList[0] )
                    {
                        /* update blacklist */
                        read_blocked_ips();
                    }

                    /* copy & reset counters */
                    memcpy(&G_cnts_day_before, &G_cnts_yesterday, sizeof(counters_t));
                    memcpy(&G_cnts_yesterday, &G_cnts_today, sizeof(counters_t));
                    memset(&G_cnts_today, 0, sizeof(counters_t));

                    /* log currently used memory */
                    library_log_memory();
                    ++G_days_up;
                }
            }
        }
        else    /* readsocks > 0 */
        {
            if ( FD_ISSET(M_listening_fd, &M_readfds) )
            {
                accept_http();
            }
#ifdef HTTPS
            else if ( FD_ISSET(M_listening_sec_fd, &M_readfds) )
            {
                accept_https();
            }
#endif
            else    /* existing connections have something going on on them ---------------------------------- */
            {
                for ( i=0; i<MAX_CONNECTIONS; ++i )
                {
                    /* --------------------------------------------------------------------------------------- */
                    if ( FD_ISSET(conn[i].fd, &M_readfds) )     /* incoming data ready */
                    {
//                      DEBUG("\nfd=%d has incoming data ready to read", conn[i].fd);
#ifdef HTTPS
                        if ( conn[i].secure )   /* HTTPS */
                        {
//                          DEBUG("secure, state=%c, pending=%d", conn[i].conn_state, SSL_pending(conn[i].ssl));

                            if ( conn[i].conn_state != CONN_STATE_READING_DATA )
                            {
#ifdef DUMP
                                DEBUG("Trying SSL_read from fd=%d", conn[i].fd);
#endif
                                bytes = SSL_read(conn[i].ssl, conn[i].in, IN_BUFSIZE-1);
                                if ( bytes > 1 )
                                    conn[i].in[bytes] = EOS;
                                else if ( bytes == 1 )  /* when browser splits the request to prevent BEAST attack */
                                {
                                    bytes = SSL_read(conn[i].ssl, conn[i].in+1, IN_BUFSIZE-2) + 1;
                                    if ( bytes > 1 )
                                        conn[i].in[bytes] = EOS;
                                }
                                set_state_sec(i, bytes);
                            }
                            else    /* POST */
                            {
#ifdef DUMP
                                DEBUG("state == CONN_STATE_READING_DATA");
                                DEBUG("Trying SSL_read %ld bytes of POST data from fd=%d", conn[i].clen-conn[i].was_read, conn[i].fd);
#endif /* DUMP */
                                bytes = SSL_read(conn[i].ssl, conn[i].data+conn[i].was_read, conn[i].clen-conn[i].was_read);
                                if ( bytes > 0 )
                                    conn[i].was_read += bytes;
                                set_state_sec(i, bytes);
                            }
                        }
                        else        /* HTTP */
#endif /* HTTPS */
                        {
//                          DEBUG("not secure, state=%c", conn[i].conn_state);

                            if ( conn[i].conn_state == CONN_STATE_CONNECTED )
                            {
#ifdef DUMP
                                DEBUG("state == CONN_STATE_CONNECTED");
                                DEBUG("Trying read from fd=%d", conn[i].fd);
#endif /* DUMP */
#ifdef _WIN32   /* Windows */
                                bytes = recv(conn[i].fd, conn[i].in, IN_BUFSIZE-1, 0);
#else
                                bytes = read(conn[i].fd, conn[i].in, IN_BUFSIZE-1);
#endif  /* _WIN32 */
                                if ( bytes > 0 )
                                    conn[i].in[bytes] = EOS;
                                set_state(i, bytes);    /* possibly:    CONN_STATE_DISCONNECTED (if error or closed by peer) */
                                                        /*              CONN_STATE_READY_FOR_PARSE */
                            }
                            else if ( conn[i].conn_state == CONN_STATE_READING_DATA )   /* POST */
                            {
#ifdef DUMP
                                DEBUG("state == CONN_STATE_READING_DATA");
                                DEBUG("Trying to read %ld bytes of POST data from fd=%d", conn[i].clen-conn[i].was_read, conn[i].fd);
#endif /* DUMP */
#ifdef _WIN32   /* Windows */
                                bytes = recv(conn[i].fd, conn[i].data+conn[i].was_read, conn[i].clen-conn[i].was_read, 0);
#else
                                bytes = read(conn[i].fd, conn[i].data+conn[i].was_read, conn[i].clen-conn[i].was_read);
#endif  /* _WIN32 */
                                if ( bytes > 0 )
                                    conn[i].was_read += bytes;
                                set_state(i, bytes);    /* possibly:    CONN_STATE_DISCONNECTED (if error or closed by peer) */
                                                        /*              CONN_STATE_READY_FOR_PROCESS */
                            }
                        }
                    }

                    /* --------------------------------------------------------------------------------------- */
                    if ( FD_ISSET(conn[i].fd, &M_writefds) )        /* ready for bringgoing data */
                    {
#ifdef DUMP
                        if ( G_now != dbg_last_time1 )   /* only once in a second */
                        {
                            DEBUG("ci=%d, fd=%d is ready for bringgoing data, conn_state = %c", i, conn[i].fd, conn[i].conn_state);
                            dbg_last_time1 = G_now;
                        }
#endif /* DUMP */
                        /* async processing */
#ifdef ASYNC
                        if ( conn[i].conn_state == CONN_STATE_WAITING_FOR_ASYNC )
                        {
#ifdef DUMP
                            if ( G_now != dbg_last_time2 )   /* only once in a second */
                            {
                                DEBUG("ci=%d, state == CONN_STATE_WAITING_FOR_ASYNC", i);
                                dbg_last_time2 = G_now;
                            }
#endif /* DUMP */
                            for ( j=0; j<MAX_ASYNC; ++j )
                            {
                                if ( (ares[j].hdr.state==ASYNC_STATE_RECEIVED || ares[j].hdr.state==ASYNC_STATE_TIMEBRINGED) && ares[j].hdr.ci == i )
                                {
                                    if ( ares[j].hdr.state == ASYNC_STATE_RECEIVED )
                                    {
                                        DEBUG("Async response in an array for ci=%d, processing", i);
                                        app_async_done(i, ares[j].hdr.helper, ares[j].data, ares[j].hdr.err_code);
                                    }
                                    else if ( ares[j].hdr.state == ASYNC_STATE_TIMEBRINGED )
                                    {
                                        DEBUG("Async response done as timebring-ed for ci=%d", i);
                                        app_async_done(i, ares[j].hdr.helper, "", ERR_ASYNC_TIMEBRING);
                                    }
                                    gen_response_header(i);
                                    ares[j].hdr.state = ASYNC_STATE_FREE;
                                    break;
                                }
                            }
                        }
#endif /* ASYNC */
#ifdef HTTPS
                        if ( conn[i].secure )   /* HTTPS */
                        {
//                          DEBUG("secure, state=%c", conn[i].conn_state);

                            if ( conn[i].conn_state == CONN_STATE_READY_TO_SEND_HEADER )
                            {
#ifdef DUMP
                                DEBUG("state == CONN_STATE_READY_TO_SEND_HEADER");
                                DEBUG("Trying SSL_write %ld bytes to fd=%d", strlen(conn[i].header), conn[i].fd);
#endif /* DUMP */
                                bytes = SSL_write(conn[i].ssl, conn[i].header, strlen(conn[i].header));
                                set_state_sec(i, bytes);
                            }
                            else if ( conn[i].conn_state == CONN_STATE_READY_TO_SEND_BODY || conn[i].conn_state == CONN_STATE_SENDING_BODY)
                            {
#ifdef DUMP
                                DEBUG("state == %s", conn[i].conn_state==CONN_STATE_READY_TO_SEND_BODY?"CONN_STATE_READY_TO_SEND_BODY":"CONN_STATE_SENDING_BODY");
                                DEBUG("Trying SSL_write %ld bytes to fd=%d", conn[i].clen, conn[i].fd);
#endif /* DUMP */
                                if ( conn[i].static_res == NOT_STATIC )
                                    bytes = SSL_write(conn[i].ssl, conn[i].bring_data, conn[i].clen);
                                else
                                    bytes = SSL_write(conn[i].ssl, M_stat[conn[i].static_res].data, conn[i].clen);
                                set_state_sec(i, bytes);
                            }
                        }
                        else    /* HTTP */
#endif /* HTTPS */
                        {
//                          DEBUG("not secure, state=%c", conn[i].conn_state);

                            if ( conn[i].conn_state == CONN_STATE_READY_TO_SEND_HEADER )
                            {
#ifdef DUMP
                                DEBUG("state == CONN_STATE_READY_TO_SEND_HEADER");
                                DEBUG("Trying to write %ld bytes to fd=%d", strlen(conn[i].header), conn[i].fd);
#endif /* DUMP */
#ifdef _WIN32   /* Windows */
                                bytes = send(conn[i].fd, conn[i].header, strlen(conn[i].header), 0);
#else
                                bytes = write(conn[i].fd, conn[i].header, strlen(conn[i].header));
#endif  /* _WIN32 */
                                set_state(i, bytes);    /* possibly:    CONN_STATE_DISCONNECTED (if error or closed by peer) */
                                                        /*              CONN_STATE_READY_TO_SEND_BODY */
                            }
                            else if ( conn[i].conn_state == CONN_STATE_READY_TO_SEND_BODY || conn[i].conn_state == CONN_STATE_SENDING_BODY)
                            {
#ifdef DUMP
                                DEBUG("state == %s", conn[i].conn_state==CONN_STATE_READY_TO_SEND_BODY?"CONN_STATE_READY_TO_SEND_BODY":"CONN_STATE_SENDING_BODY");
                                DEBUG("Trying to write %ld bytes to fd=%d", conn[i].clen-conn[i].data_sent, conn[i].fd);
//                                log_long(conn[i].bring_data+conn[i].data_sent, conn[i].clen-conn[i].data_sent, "Body to send");
#endif /* DUMP */
                                if ( conn[i].static_res == NOT_STATIC )
                                {
#ifdef _WIN32   /* Windows */
                                    bytes = send(conn[i].fd, conn[i].bring_data+conn[i].data_sent, conn[i].clen-conn[i].data_sent, 0);
#else
                                    bytes = write(conn[i].fd, conn[i].bring_data+conn[i].data_sent, conn[i].clen-conn[i].data_sent);
#endif  /* _WIN32 */
                                }
                                else
                                {
#ifdef _WIN32   /* Windows */
                                    bytes = send(conn[i].fd, M_stat[conn[i].static_res].data+conn[i].data_sent, conn[i].clen-conn[i].data_sent, 0);
#else
                                    bytes = write(conn[i].fd, M_stat[conn[i].static_res].data+conn[i].data_sent, conn[i].clen-conn[i].data_sent);
#endif  /* _WIN32 */
                                }
                                conn[i].data_sent += bytes;
                                set_state(i, bytes);    /* possibly:    CONN_STATE_DISCONNECTED (if error or closed by peer or !keep_alive) */
                                                        /*              CONN_STATE_SENDING_BODY (if data_sent < clen) */
                                                        /*              CONN_STATE_CONNECTED */
                            }
                        }
                    }

                    /* --------------------------------------------------------------------------------------- */
                    /* after reading / writing it may be ready for parsing and processing ... */

                    if ( conn[i].conn_state == CONN_STATE_READY_FOR_PARSE )
                    {
                        clock_gettime(MONOTONIC_CLOCK_NAME, &conn[i].proc_start);

                        conn[i].status = parse_req(i, bytes);
#ifdef HTTPS
#ifdef DOMAINONLY       /* redirect to final domain first */
                        if ( !conn[i].secure && conn[i].upgrade2https && 0!=strcmp(conn[i].host, APP_DOMAIN) )
                            conn[i].upgrade2https = FALSE;
#endif
#endif /* HTTPS */
                        if ( conn[i].conn_state != CONN_STATE_READING_DATA )
                        {
#ifdef DUMP
                            DEBUG("Changing state to CONN_STATE_READY_FOR_PROCESS");
#endif
                            conn[i].conn_state = CONN_STATE_READY_FOR_PROCESS;
                        }
                    }

                    /* received Expect: 100-continue before content */

                    if ( conn[i].expect100 )
                        respond_to_expect(i);

                    /* ready for processing */

                    if ( conn[i].conn_state == CONN_STATE_READY_FOR_PROCESS )
                    {
#ifdef HTTPS
                        if ( conn[i].upgrade2https && conn[i].status==200 )
                            conn[i].status = 301;
#endif
                        /* update visits counter */
                        if ( !conn[i].resource[0] && conn[i].status==200 && !conn[i].bot && !conn[i].head_only && 0==strcmp(conn[i].host, APP_DOMAIN) )
                        {
                            ++G_cnts_today.visits;
                            if ( conn[i].mobile )
                                ++G_cnts_today.visits_mob;
                            else
                                ++G_cnts_today.visits_dsk;
                        }

                        /* process request */
                        process_req(i);
#ifdef ASYNC
                        if ( conn[i].conn_state != CONN_STATE_WAITING_FOR_ASYNC )
#endif
                        gen_response_header(i);
                    }
                }
            }
        }

        /* async processing -- check on response queue */
#ifdef ASYNC
        async_res_t res;
#ifdef DUMP
        int mq_ret;
        if ( (mq_ret=mq_receive(G_queue_res, (char*)&res, ASYNC_RES_MSG_SIZE, NULL)) != -1 )    /* there's a response in the queue */
#else
        if ( mq_receive(G_queue_res, (char*)&res, ASYNC_RES_MSG_SIZE, NULL) != -1 )    /* there's a response in the queue */
#endif /* DUMP */
        {
            DEBUG("Message received!");
            DEBUG("res.hdr.call_id = %ld", res.hdr.call_id);
            DEBUG("res.hdr.ci = %d", res.hdr.ci);
            DEBUG("res.hdr.helper [%s]", res.hdr.helper);

            for ( j=0; j<MAX_ASYNC; ++j )
            {
                if ( ares[j].hdr.call_id == res.hdr.call_id )
                {
                    DEBUG("ares record found");
                    memcpy(&ares[j], (char*)&res, sizeof(async_res_t));
                    ares[j].hdr.state = ASYNC_STATE_RECEIVED;
                    break;
                }
            }
        }
#ifdef DUMP
        else
        {
            static time_t last_time=0;  /* not too often */

            if ( last_time != G_now )
            {
                int wtf = errno;
                if ( wtf != EAGAIN )
                    ERR("mq_receive failed, errno = %d (%s)", wtf, strerror(wtf));
                last_time = G_now;
            }
        }
#endif /* DUMP */

        /* free timebring-ed */

        for ( j=0; j<MAX_ASYNC; ++j )
        {
            if ( ares[j].hdr.state==ASYNC_STATE_TIMEBRINGED )     /* apparently closed browser connection */
            {
                ares[j].hdr.state = ASYNC_STATE_FREE;
            }
        }

#endif /* ASYNC */

        ++time_elapsed;
    }

    return EXIT_SUCCESS;
}


/* --------------------------------------------------------------------------
   Set new connection state after read or write
-------------------------------------------------------------------------- */
static void set_state(int ci, long bytes)
{
    if ( bytes <= 0 )
    {
        DEBUG("bytes = %ld, errno = %d (%s), disconnecting slot %d\n", bytes, errno, strerror(errno), ci);
        close_connection(ci);
        return;
    }

    /* bytes > 0 */

    DEBUG("bytes = %ld", bytes);

    if ( conn[ci].conn_state == CONN_STATE_CONNECTED )  /* assume the whole header has been read */
    {
#ifdef DUMP
        DEBUG("Changing state to CONN_STATE_READY_FOR_PARSE");
#endif
        conn[ci].conn_state = CONN_STATE_READY_FOR_PARSE;
    }
    else if ( conn[ci].conn_state == CONN_STATE_READING_DATA )  /* it could have been received only partially */
    {
        if ( conn[ci].was_read < conn[ci].clen )
        {
            DEBUG("Continue receiving");
        }
        else    /* data received */
        {
            conn[ci].data[conn[ci].was_read] = EOS;
            DEBUG("POST data received");
#ifdef DUMP
            DEBUG("Changing state to CONN_STATE_READY_FOR_PROCESS");
#endif
            conn[ci].conn_state = CONN_STATE_READY_FOR_PROCESS;
        }
    }
    else if ( conn[ci].conn_state == CONN_STATE_READY_TO_SEND_HEADER )  /* assume the whole header has been sent successfuly */
    {
        if ( conn[ci].clen > 0 )
        {
#ifdef DUMP
            DEBUG("Changing state to CONN_STATE_READY_TO_SEND_BODY");
#endif
            conn[ci].conn_state = CONN_STATE_READY_TO_SEND_BODY;
        }
        else /* no body to send */
        {
            DEBUG("clen = 0");
            log_proc_time(ci);
            if ( conn[ci].keep_alive )
            {
                DEBUG("End of processing, reset_conn\n");
                reset_conn(ci, CONN_STATE_CONNECTED);
            }
            else
            {
                DEBUG("End of processing, close_connection\n");
                close_connection(ci);
            }
        }
    }
    else if ( conn[ci].conn_state == CONN_STATE_READY_TO_SEND_BODY )    /* it could have been sent only partially */
    {
        if ( bytes < conn[ci].clen )
        {
#ifdef DUMP
            DEBUG("Changing state to CONN_STATE_SENDING_BODY");
#endif
            conn[ci].conn_state = CONN_STATE_SENDING_BODY;
        }
        else /* assuming the whole body has been sent at once */
        {
            log_proc_time(ci);
            if ( conn[ci].keep_alive )
            {
                DEBUG("End of processing, reset_conn\n");
                reset_conn(ci, CONN_STATE_CONNECTED);
            }
            else
            {
                DEBUG("End of processing, close_connection\n");
                close_connection(ci);
            }
        }
    }
    else if ( conn[ci].conn_state == CONN_STATE_SENDING_BODY )
    {
        if ( conn[ci].data_sent < conn[ci].clen )
        {
            DEBUG("Continue sending");
        }
        else    /* body sent */
        {
            log_proc_time(ci);
            if ( conn[ci].keep_alive )
            {
                DEBUG("End of processing, reset_conn\n");
                reset_conn(ci, CONN_STATE_CONNECTED);
            }
            else
            {
                DEBUG("End of processing, close_connection\n");
                close_connection(ci);
            }
        }
    }
}


/* --------------------------------------------------------------------------
   Set new connection state after read or write for secure connections
-------------------------------------------------------------------------- */
static void set_state_sec(int ci, long bytes)
{
    int     e;
    char    ec[128]="";
#ifdef HTTPS
    e = errno;

    conn[ci].ssl_err = SSL_get_error(conn[ci].ssl, bytes);

    if ( bytes <= 0 )
    {
        if ( conn[ci].ssl_err == SSL_ERROR_SYSCALL )
            sprintf(ec, ", errno = %d (%s)", e, strerror(e));

        DEBUG("bytes = %ld, ssl_err = %d%s", bytes, conn[ci].ssl_err, ec);

        if ( conn[ci].ssl_err != SSL_ERROR_WANT_READ && conn[ci].ssl_err != SSL_ERROR_WANT_WRITE )
        {
            DEBUG("Closing connection\n");
            close_connection(ci);
        }

        return;
    }

    /* bytes > 0 */

    DEBUG("bytes = %ld", bytes);

    // we have no way of knowing if accept finished before reading actual request
    if ( conn[ci].conn_state == CONN_STATE_ACCEPTING || conn[ci].conn_state == CONN_STATE_CONNECTED )   /* assume the whole header has been read */
    {
#ifdef DUMP
        DEBUG("Changing state to CONN_STATE_READY_FOR_PARSE");
#endif
        conn[ci].conn_state = CONN_STATE_READY_FOR_PARSE;
    }
    else if ( conn[ci].conn_state == CONN_STATE_READING_DATA )
    {
        if ( conn[ci].was_read < conn[ci].clen )
        {
            DEBUG("Continue receiving");
        }
        else    /* data received */
        {
            conn[ci].data[conn[ci].was_read] = EOS;
            DEBUG("POST data received");
#ifdef DUMP
            DEBUG("Changing state to CONN_STATE_READY_FOR_PROCESS");
#endif
            conn[ci].conn_state = CONN_STATE_READY_FOR_PROCESS;
        }
    }
    else if ( conn[ci].conn_state == CONN_STATE_READY_TO_SEND_HEADER )
    {
        if ( conn[ci].clen > 0 )
        {
#ifdef DUMP
            DEBUG("Changing state to CONN_STATE_READY_TO_SEND_BODY");
#endif
            conn[ci].conn_state = CONN_STATE_READY_TO_SEND_BODY;
        }
        else /* no body to send */
        {
            DEBUG("clen = 0");
            log_proc_time(ci);
            if ( conn[ci].keep_alive )
            {
                DEBUG("End of processing, reset_conn\n");
                reset_conn(ci, CONN_STATE_CONNECTED);
            }
            else
            {
                DEBUG("End of processing, close_connection\n");
                close_connection(ci);
            }
        }
    }
    else if ( conn[ci].conn_state == CONN_STATE_READY_TO_SEND_BODY || conn[ci].conn_state == CONN_STATE_SENDING_BODY )
    {
        log_proc_time(ci);
        if ( conn[ci].keep_alive )
        {
            DEBUG("End of processing, reset_conn\n");
            reset_conn(ci, CONN_STATE_CONNECTED);
        }
        else
        {
            DEBUG("End of processing, close_connection\n");
            close_connection(ci);
        }
    }
#endif
}


/* --------------------------------------------------------------------------
   Read & parse conf file and set global parameters
-------------------------------------------------------------------------- */
static void read_conf()
{
    bool    conf_read=FALSE;

    /* set defaults */

    G_logLevel = 3;
    G_logToStdbring = 0;
    G_logCombined = 0;
    G_httpPort = 80;
    G_httpsPort = 443;
    G_certFile[0] = EOS;
    G_certChainFile[0] = EOS;
    G_keyFile[0] = EOS;
    G_dbHost[0] = EOS;
    G_dbPort = 0;
    G_dbName[0] = EOS;
    G_dbUser[0] = EOS;
    G_dbPassword[0] = EOS;
    G_usersRequireAccountActivation = 0;
    G_blockedIPList[0] = EOS;
    G_ASYNCDefTimebring = ASYNC_DEF_TIMEBRING;
    G_RESTTimebring = CALL_REST_DEFAULT_TIMEBRING;
    G_test = 0;

    /* get the conf file path & name */

    if ( G_appdir[0] )
    {
        char conf_path[512];
        sprintf(conf_path, "%s/bin/fasty.conf", G_appdir);
        if ( !(conf_read=library_read_conf(conf_path)) )   /* no config file there */
            conf_read = library_read_conf("fasty.conf");
    }
    else    /* no FASTYDIR -- try current dir */
    {
        conf_read = library_read_conf("fasty.conf");
    }

    if ( conf_read )
    {
        fasty_read_param_int("logLevel", &G_logLevel);
        fasty_read_param_int("logToStdbring", &G_logToStdbring);
        fasty_read_param_int("logCombined", &G_logCombined);
        fasty_read_param_int("httpPort", &G_httpPort);
        fasty_read_param_int("httpsPort", &G_httpsPort);
        fasty_read_param_str("certFile", G_certFile);
        fasty_read_param_str("certChainFile", G_certChainFile);
        fasty_read_param_str("keyFile", G_keyFile);
        fasty_read_param_str("dbHost", G_dbHost);
        fasty_read_param_int("dbPort", &G_dbPort);
        fasty_read_param_str("dbName", G_dbName);
        fasty_read_param_str("dbUser", G_dbUser);
        fasty_read_param_str("dbPassword", G_dbPassword);
        fasty_read_param_int("usersRequireAccountActivation", &G_usersRequireAccountActivation);
        fasty_read_param_str("blockedIPList", G_blockedIPList);
        fasty_read_param_int("ASYNCDefTimebring", &G_ASYNCDefTimebring);
        fasty_read_param_int("RESTTimebring", &G_RESTTimebring);
        fasty_read_param_int("test", &G_test);
    }
}


/* --------------------------------------------------------------------------
   Respond to Expect: header
-------------------------------------------------------------------------- */
static void respond_to_expect(int ci)
{
    static char reply_accept[]="HTTP/1.1 100 Continue\r\n\r\n";
    static char reply_refuse[]="HTTP/1.1 413 Request Entity Too Large\r\n\r\n";
    int bytes;

    if ( conn[ci].clen >= MAX_POST_DATA_BUFSIZE )   /* refuse */
    {
        INF("Sending 413");
#ifdef HTTPS
        if ( conn[ci].secure )
            bytes = SSL_write(conn[ci].ssl, reply_refuse, 41);
        else
#endif
#ifdef _WIN32   /* Windows */
            bytes = send(conn[ci].fd, reply_refuse, 41, 0);
#else
            bytes = write(conn[ci].fd, reply_refuse, 41);
#endif  /* _WIN32 */

        if ( bytes < 41 ) ERR("write error, bytes = %d", bytes);
    }
    else    /* accept */
    {
        INF("Sending 100");

#ifdef HTTPS
        if ( conn[ci].secure )
            bytes = SSL_write(conn[ci].ssl, reply_accept, 25);
        else
#endif
#ifdef _WIN32   /* Windows */
            bytes = send(conn[ci].fd, reply_accept, 25, 0);
#else
            bytes = write(conn[ci].fd, reply_accept, 25);
#endif  /* _WIN32 */

        if ( bytes < 25 ) ERR("write error, bytes = %d", bytes);
    }

    conn[ci].expect100 = FALSE;
}


/* --------------------------------------------------------------------------
   Log processing time
-------------------------------------------------------------------------- */
static void log_proc_time(int ci)
{
    DEBUG("Processing time: %.3lf ms [%s]\n", library_elapsed(&conn[ci].proc_start), conn[ci].resource);

    /* Use (almost) Combined Log Format */

    char logtime[64];
    strftime(logtime, 64, "%d/%b/%Y:%H:%M:%S +0000", G_ptm);

    if ( G_logCombined )
        ALWAYS("%s - - [%s] \"%s /%s %s\" %d %d \"%s\" \"%s\"  #%ld  %.3lf ms%s", conn[ci].ip, logtime, conn[ci].method, conn[ci].uri, conn[ci].proto, conn[ci].status, conn[ci].data_sent, conn[ci].referer, conn[ci].uagent, conn[ci].req, library_elapsed(&conn[ci].proc_start), REQ_BOT?"  [bot]":"");
    else
        ALWAYS("%s - - [%s] \"%s /%s %s\" %d %d  #%ld  %.3lf ms%s", conn[ci].ip, logtime, conn[ci].method, conn[ci].uri, conn[ci].proto, conn[ci].status, conn[ci].data_sent, conn[ci].req, library_elapsed(&conn[ci].proc_start), REQ_BOT?"  [bot]":"");
}


/* --------------------------------------------------------------------------
   Close connection
-------------------------------------------------------------------------- */
static void close_connection(int ci)
{
#ifdef DUMP
    DEBUG("Closing connection ci=%d, fd=%d", ci, conn[ci].fd);
#endif
#ifdef HTTPS
    if ( conn[ci].ssl )
    {
        SSL_free(conn[ci].ssl);
        conn[ci].ssl = NULL;
    }
#endif
#ifdef _WIN32   /* Windows */
    closesocket(conn[ci].fd);
#else
    close(conn[ci].fd);
#endif  /* _WIN32 */
    reset_conn(ci, CONN_STATE_DISCONNECTED);
}


/* --------------------------------------------------------------------------
   Engine init
   Return TRUE if success
-------------------------------------------------------------------------- */
static bool init(int argc, char **argv)
{
    time_t      sometimeahead;
    int         i=0;

    fasty_library_initialization();

    /* init globals */

    G_days_up = 0;
    G_open_conn = 0;
    G_sessions = 0;
    G_index_present = FALSE;
#ifdef DBMYSQL
    G_dbconn = NULL;
#endif

    /* counters */

    memset(&G_cnts_today, 0, sizeof(counters_t));
    memset(&G_cnts_yesterday, 0, sizeof(counters_t));
    memset(&G_cnts_day_before, 0, sizeof(counters_t));

    /* read the config file or set defaults */

    read_conf();

    /* command line arguments */

    if ( argc > 1 )
    {
        G_httpPort = atoi(argv[1]);
        printf("Will be listening on the port %d...\n", G_httpPort);
    }

    /* start log --------------------------------------------------------- */

    char exec_name[256];
    library_get_execution_name(exec_name, argv[0]);

    if ( !log_start("", G_test) )
        return FALSE;

    ALWAYS("Starting program");
    ALWAYS("");

#ifdef __linux__
    INF("This is Linux");
    INF("");
#endif

#ifdef _WIN32
    INF("This is Windows");
    INF("");
#endif

    ALWAYS("G_appdir [%s]", G_appdir);
    ALWAYS("logLevel = %d", G_logLevel);
    ALWAYS("logToStdbring = %d", G_logToStdbring);
    ALWAYS("logCombined = %d", G_logCombined);
    if ( argc > 1 )
    {
        ALWAYS_LINE;
        WAR("httpPort = %d -- overwritten by a command line argument", G_httpPort);
        ALWAYS_LINE;
    }
    else
        ALWAYS("httpPort = %d", G_httpPort);
    ALWAYS("httpsPort = %d", G_httpsPort);
    ALWAYS("dbHost [%s]", G_dbHost);
    ALWAYS("dbPort = %d", G_dbPort);
    ALWAYS("dbName [%s]", G_dbName);
    ALWAYS("ASYNCDefTimebring = %d", G_ASYNCDefTimebring);
    ALWAYS("RESTTimebring = %d", G_RESTTimebring);
    ALWAYS("test = %d", G_test);

    /* pid file ---------------------------------------------------------- */

    if ( !(M_pidfile=library_create_pid_file("fasty_app")) )
        return FALSE;

    /* empty static resources list */

    for ( i=0; i<MAX_STATICS; ++i )
        strcpy(M_stat[i].name, "-");

    /* check endianness and some parameters */

    get_byteorder();

    ALWAYS("");
    ALWAYS_LINE_LONG;
    ALWAYS("");
    ALWAYS("System:");
    ALWAYS("-------");
#ifdef DUMP
    // SIZE_MAX is not defined in older GCC!
//    ALWAYS("              SIZE_MAX = %lu (%lu kB / %lu MB)", SIZE_MAX, SIZE_MAX/1024, SIZE_MAX/1024/1024);
#endif
    ALWAYS("            FD_SETSIZE = %d", FD_SETSIZE);
    ALWAYS("             SOMAXCONN = %d", SOMAXCONN);
    ALWAYS("");
    ALWAYS("Server:");
    ALWAYS("-------");
    ALWAYS("              FASTYDIR = %s", G_appdir);
    ALWAYS("    WEB_SERVER_VERSION = %s", WEB_SERVER_VERSION);
#ifdef MEM_SMALL
    ALWAYS("          Memory model = MEM_SMALL");
#endif
#ifdef MEM_MEDIUM
    ALWAYS("          Memory model = MEM_MEDIUM");
#endif
#ifdef MEM_BIG
    ALWAYS("          Memory model = MEM_BIG");
#endif
#ifdef MEM_HUGE
    ALWAYS("          Memory model = MEM_HUGE");
#endif
    ALWAYS("       MAX_CONNECTIONS = %d", MAX_CONNECTIONS);
    ALWAYS("          MAX_SESSIONS = %d", MAX_SESSIONS);
    ALWAYS("          CONN_TIMEBRING = %d seconds", CONN_TIMEBRING);
    ALWAYS("          USES_TIMEBRING = %d seconds", USES_TIMEBRING);
#ifdef USERS
    ALWAYS("         LUSES_TIMEBRING = %d seconds", LUSES_TIMEBRING);
#endif
    ALWAYS("");
    ALWAYS("           conn's size = %lu B (%lu kB / %0.2lf MB)", sizeof(conn), sizeof(conn)/1024, (double)sizeof(conn)/1024/1024);
    ALWAYS("            uses' size = %lu B (%lu kB / %0.2lf MB)", sizeof(uses), sizeof(uses)/1024, (double)sizeof(uses)/1024/1024);
    ALWAYS("");
    ALWAYS("           BRING_BUFSIZE = %lu B (%lu kB / %0.2lf MB)", BRING_BUFSIZE, BRING_BUFSIZE/1024, (double)BRING_BUFSIZE/1024/1024);
#ifdef BRINGFAST
    ALWAYS("           Output type = BRINGFAST");
#endif
#ifdef BRINGCHECK
    ALWAYS("           Output type = BRINGCHECK");
#endif
#ifdef BRINGCHECKREALLOC
    ALWAYS("           Output type = BRINGCHECKREALLOC");
#endif

#ifdef QS_DEF_SQL_ESCAPE
    ALWAYS(" Query string security = QS_DEF_SQL_ESCAPE");
#endif
#ifdef QS_DEF_DONT_ESCAPE
    ALWAYS(" Query string security = QS_DEF_DONT_ESCAPE");
#endif
#ifdef QS_DEF_HTML_ESCAPE
    ALWAYS(" Query string security = QS_DEF_HTML_ESCAPE");
#endif
    ALWAYS("");
    ALWAYS("Program:");
    ALWAYS("--------");
    ALWAYS("           APP_WEBSITE = %s", APP_WEBSITE);
    ALWAYS("            APP_DOMAIN = %s", APP_DOMAIN);
    ALWAYS("           APP_VERSION = %s", APP_VERSION);
    ALWAYS("         APP_LOGIN_URI = %s", APP_LOGIN_URI);
    if ( APP_DEF_AUTH_LEVEL == AUTH_LEVEL_NONE )
        ALWAYS("    APP_DEF_AUTH_LEVEL = AUTH_LEVEL_NONE");
    else if ( APP_DEF_AUTH_LEVEL == AUTH_LEVEL_ANONYMOUS )
        ALWAYS("    APP_DEF_AUTH_LEVEL = AUTH_LEVEL_ANONYMOUS");
    else if ( APP_DEF_AUTH_LEVEL == AUTH_LEVEL_LOGGEDIN )
        ALWAYS("    APP_DEF_AUTH_LEVEL = AUTH_LEVEL_LOGGEDIN");
    else if ( APP_DEF_AUTH_LEVEL == AUTH_LEVEL_ADMIN )
        ALWAYS("    APP_DEF_AUTH_LEVEL = AUTH_LEVEL_ADMIN");
#ifdef APP_ADMIN_EMAIL
    ALWAYS("       APP_ADMIN_EMAIL = %s", APP_ADMIN_EMAIL);
#endif
#ifdef APP_CONTACT_EMAIL
    ALWAYS("     APP_CONTACT_EMAIL = %s", APP_CONTACT_EMAIL);
#endif
#ifdef USERS
    ALWAYS("");
#ifdef USERSBYEMAIL
    ALWAYS(" Users' authentication = USERSBYEMAIL");
#else
    ALWAYS(" Users' authentication = USERSBYLOGIN");
#endif
#endif /* USERS */
//    ALWAYS("");
//    ALWAYS("           auses' size = %lu B (%lu kB / %0.2lf MB)", sizeof(auses), sizeof(auses)/1024, (double)sizeof(auses)/1024/1024);
//    ALWAYS("");
    ALWAYS_LINE_LONG;
    ALWAYS("");

#ifdef DUMP
    WAR("DUMP is enabled, this file may grow big quickly!");
    ALWAYS("");
#endif /* DUMP */

    /* custom init
       Among others, that may contain generating statics, like css and js */

    if ( !app_initialization(argc, argv) )
    {
        ERR("app_initialization() failed");
        return FALSE;
    }

    DEBUG("app_initialization() OK");

    /* read static resources */

    if ( !read_files(FALSE, TRUE, NULL) )   /* normal */
    {
        ERR("read_files() failed");
        return FALSE;
    }

    DEBUG("read_files(FALSE) OK");

    if ( !read_files(TRUE, TRUE, NULL) )    /* minified */
    {
        ERR("read_files() for minified failed");
        return FALSE;
    }

    DEBUG("read_files(TRUE) OK");

    /* special case statics -- check if present */

    for ( i=0; M_stat[i].name[0] != '-'; ++i )
    {
        if ( 0==strcmp(M_stat[i].name, "favicon.ico") )
        {
            M_favicon_exists = TRUE;
            break;
        }
    }

    for ( i=0; M_stat[i].name[0] != '-'; ++i )
    {
        if ( 0==strcmp(M_stat[i].name, "robots.txt") )
        {
            M_robots_exists = TRUE;
            break;
        }
    }

    for ( i=0; M_stat[i].name[0] != '-'; ++i )
    {
        if ( 0==strcmp(M_stat[i].name, "apple-touch-icon.png") )
        {
            M_appleicon_exists = TRUE;
            break;
        }
    }

    DEBUG("Standard icons OK");

    /* libSHA1 test */

    uint8_t sha1_res1[SHA1_DIGEST_SIZE];
    char    sha1_res2[64];
    char    sha1_res3[64];

    DEBUG("");
    DEBUG("Trying libSHA1...\n");
    DEBUG("Expected: [A9993E36 4706816A BA3E2571 7850C26C 9CD0D89D]");

    libSHA1((unsigned char*)"abc", 3, sha1_res1);

    digest_to_hex(sha1_res1, sha1_res2);
    DEBUG("     Got: [%s]\n", sha1_res2);

    /* calculate Expires and Last-Modified header fields for static resources */

#ifdef _WIN32   /* Windows */
    strftime(G_last_modified, 32, "%a, %d %b %Y %H:%M:%S GMT", G_ptm);
#else
    strftime(G_last_modified, 32, "%a, %d %b %Y %T GMT", G_ptm);
#endif  /* _WIN32 */
    DEBUG("Now is: %s\n", G_last_modified);

    sometimeahead = G_now + 3600*24*EXPIRES_IN_DAYS;
    G_ptm = gmtime(&sometimeahead);
#ifdef _WIN32   /* Windows */
    strftime(M_expires, 32, "%a, %d %b %Y %H:%M:%S GMT", G_ptm);
#else
    strftime(M_expires, 32, "%a, %d %b %Y %T GMT", G_ptm);
#endif  /* _WIN32 */
    DEBUG("M_expires: %s\n", M_expires);

    G_ptm = gmtime(&G_now); /* reset to today */

	/* handle signals ---------------------------------------------------- */

    signal(SIGINT,  sigdisp);   /* Ctrl-C */
    signal(SIGTERM, sigdisp);
#ifndef _WIN32
    signal(SIGQUIT, sigdisp);   /* Ctrl-\ */
    signal(SIGTSTP, sigdisp);   /* Ctrl-Z */

    signal(SIGPIPE, SIG_IGN);   /* ignore SIGPIPE */
#endif

    /* initialize SSL connection ----------------------------------------- */

#ifdef HTTPS
    if ( !init_ssl() )
    {
        ERR("init_ssl failed");
        return FALSE;
    }
#endif

    /* init conn array --------------------------------------------------- */

    for ( i=0; i<MAX_CONNECTIONS; ++i )
    {
#ifdef BRINGCHECKREALLOC
        if ( !(conn[i].bring_data = (char*)malloc(BRING_BUFSIZE)) )
        {
            ERR("malloc for conn[%d].bring_data failed", i);
            return FALSE;
        }
#endif /* BRINGCHECKREALLOC */

        conn[i].bring_data_allocated = BRING_BUFSIZE;
        conn[i].data = NULL;
        reset_conn(i, CONN_STATE_DISCONNECTED);

#ifdef HTTPS
        conn[i].ssl = NULL;
#endif
        conn[i].req = 0;
    }

    /* init user sessions ------------------------------------------------ */

    for ( i=0; i<MAX_SESSIONS+1; ++i )
    {
        engine_uses_reset(i);
        app_uses_reset(i);
    }

    /* read blocked IPs list --------------------------------------------- */

    read_blocked_ips();

#ifdef ASYNC
    ALWAYS("\nOpening message queues...\n");

#ifdef APP_ASYNC_ID
    sprintf(G_req_queue_name, "%s_%d", ASYNC_REQ_QUEUE, APP_ASYNC_ID);
    sprintf(G_res_queue_name, "%s_%d", ASYNC_RES_QUEUE, APP_ASYNC_ID);
#else
    strcpy(G_req_queue_name, ASYNC_REQ_QUEUE);
    strcpy(G_res_queue_name, ASYNC_RES_QUEUE);
#endif

    struct mq_attr attr={0};

    attr.mq_maxmsg = ASYNC_MQ_MAXMSG;

    /* ------------------------------------------------------------------- */

    if ( mq_unlink(G_req_queue_name) == 0 )
        INF("Message queue %s removed from system", G_req_queue_name);

    attr.mq_msgsize = ASYNC_REQ_MSG_SIZE;

    G_queue_req = mq_open(G_req_queue_name, O_WRONLY | O_CREAT | O_NONBLOCK, 0664, &attr);
    if (G_queue_req < 0)
        ERR("mq_open for req failed, errno = %d (%s)", errno, strerror(errno));

    /* ------------------------------------------------------------------- */

    if ( mq_unlink(G_res_queue_name) == 0 )
        INF("Message queue %s removed from system", G_res_queue_name);

    attr.mq_msgsize = ASYNC_RES_MSG_SIZE;   /* larger buffer */

    G_queue_res = mq_open(G_res_queue_name, O_RDONLY | O_CREAT | O_NONBLOCK, 0664, &attr);
    if (G_queue_res < 0)
        ERR("mq_open for res failed, errno = %d (%s)", errno, strerror(errno));

    /* ------------------------------------------------------------------- */

    for (i=0; i<MAX_ASYNC; ++i)
        ares[i].hdr.state = ASYNC_STATE_FREE;

    G_last_call_id = 0;

#endif /* ASYNC */

    return TRUE;
}


/* --------------------------------------------------------------------------
   Build select list
-------------------------------------------------------------------------- */
static void selection_list_build()
{
    int i;

    FD_ZERO(&M_readfds);
    FD_ZERO(&M_writefds);

    FD_SET(M_listening_fd, &M_readfds);
#ifdef HTTPS
    FD_SET(M_listening_sec_fd, &M_readfds);
#endif

    G_open_conn = 0;

    for ( i=0; i<MAX_CONNECTIONS; ++i )
    {
        if ( conn[i].conn_state == CONN_STATE_DISCONNECTED ) continue;

        /* reading */

        FD_SET(conn[i].fd, &M_readfds);

        /* writing -- only for certain states */

#ifdef HTTPS
        if ( conn[i].secure )
        {
            if ( conn[i].conn_state == CONN_STATE_READY_TO_SEND_HEADER
                    || conn[i].conn_state == CONN_STATE_READY_TO_SEND_BODY
                    || conn[i].conn_state == CONN_STATE_SENDING_BODY
                    || conn[i].conn_state == CONN_STATE_WAITING_FOR_ASYNC
                    || conn[i].ssl_err == SSL_ERROR_WANT_WRITE )
            {
                FD_SET(conn[i].fd, &M_writefds);
            }
        }
        else
        {
#endif /* HTTPS */
            if ( conn[i].conn_state != CONN_STATE_CONNECTED
                    && conn[i].conn_state != CONN_STATE_READING_DATA )
                FD_SET(conn[i].fd, &M_writefds);
#ifdef HTTPS
        }
#endif
        if (conn[i].fd > M_highsock)
            M_highsock = conn[i].fd;

        ++G_open_conn;
    }
}


/* --------------------------------------------------------------------------
   Handle a brand new connection
   we've got fd and IP here for conn array
-------------------------------------------------------------------------- */
static void accept_http()
{
    int     i;          /* current item in conn_sockets for for loops */
    int     connection; /* socket file descriptor for incoming connections */
static struct   sockaddr_in cli_addr;   /* static = initialised to zeros */
    socklen_t   addr_len;
    char    remote_addr[INET_ADDRSTRLEN]="";    /* remote address */
    long    bytes;

    /* We have a new connection coming in! We'll
       try to find a spot for it in conn array  */

    addr_len = sizeof(cli_addr);

    /* connection is a fd that accept gives us that we'll be communicating through now with the remote client */
    /* this fd will become our conn id for the whole connection's life (that is, until one of the sides close()-s) */

    connection = accept(M_listening_fd, (struct sockaddr*)&cli_addr, &addr_len);

    if ( connection < 0 )
    {
#ifdef _WIN32
        if ( M_shutdown )
            return;
#endif
        ERR("accept failed, errno = %d (%s)", errno, strerror(errno));
        return;
    }

    /* get the remote address */
#ifdef _WIN32   /* Windows */
    strcpy(remote_addr, inet_ntoa(cli_addr.sin_addr));
#else
    inet_ntop(AF_INET, &(cli_addr.sin_addr), remote_addr, INET_ADDRSTRLEN);
#endif

    if ( G_blockedIPList[0] && ip_blocked(remote_addr) )
    {
        ++G_cnts_today.blocked;
#ifdef _WIN32   /* Windows */
        closesocket(connection);
#else
        close(connection);
#endif  /* _WIN32 */
        return;
    }

    library_setnonblocking(connection);

    /* find a free slot in conn */

    for ( i=0; i<MAX_CONNECTIONS; ++i )
    {
        if ( conn[i].conn_state == CONN_STATE_DISCONNECTED )    /* free connection slot -- we'll use it */
        {
            DEBUG("\nConnection accepted: %s, slot=%d, fd=%d", remote_addr, i, connection);
            conn[i].fd = connection;
            conn[i].secure = FALSE;
            strcpy(conn[i].ip, remote_addr);        /* possibly client IP */
            strcpy(conn[i].pip, remote_addr);       /* possibly proxy IP */
#ifdef DUMP
            DEBUG("Changing state to CONN_STATE_CONNECTED");
#endif
            conn[i].conn_state = CONN_STATE_CONNECTED;
            conn[i].last_activity = G_now;
            connection = -1;                        /* mark as OK */
            break;
        }
    }

    if (connection != -1)   /* none was free */
    {
        /* No room left in the queue! */
        WAR("No room left for new client, sending 503");
#ifdef _WIN32   /* Windows */
        bytes = send(connection, "HTTP/1.1 503 Helper Unavailable\r\n\r\n", 36, 0);
#else
        bytes = write(connection, "HTTP/1.1 503 Helper Unavailable\r\n\r\n", 36);
#endif  /* _WIN32 */
        if ( bytes < 36 )
            ERR("write error, bytes = %d of 36", bytes);
#ifdef _WIN32   /* Windows */
        closesocket(connection);
#else
        close(connection);
#endif  /* _WIN32 */
    }
}


/* --------------------------------------------------------------------------
   Handle a brand new connection
   we've got fd and IP here for conn array
-------------------------------------------------------------------------- */
static void accept_https()
{
#ifdef HTTPS
    int     i;          /* current item in conn_sockets for for loops */
    int     connection; /* socket file descriptor for incoming connections */
static struct   sockaddr_in cli_addr;   /* static = initialised to zeros */
    socklen_t   addr_len;
    char    remote_addr[INET_ADDRSTRLEN]="";    /* remote address */
    long    bytes;
    int     ret, ssl_err;

    /* We have a new connection coming in! We'll
       try to find a spot for it in conn array  */

    addr_len = sizeof(cli_addr);

    /* connection is a fd that accept gives us that we'll be communicating through now with the remote client */
    /* this fd will become our conn id for the whole connection's life (that is, until one of the sides close()-s) */

    connection = accept(M_listening_sec_fd, (struct sockaddr*)&cli_addr, &addr_len);

    if ( connection < 0 )
    {
        ERR("accept failed, errno = %d (%s)", errno, strerror(errno));
        return;
    }

    /* get the remote address */
#ifdef _WIN32   /* Windows */
    strcpy(remote_addr, inet_ntoa(cli_addr.sin_addr));
#else
    inet_ntop(AF_INET, &(cli_addr.sin_addr), remote_addr, INET_ADDRSTRLEN);
#endif

    if ( G_blockedIPList[0] && ip_blocked(remote_addr) )
    {
        ++G_cnts_today.blocked;
#ifdef _WIN32   /* Windows */
        closesocket(connection);
#else
        close(connection);
#endif  /* _WIN32 */
        return;
    }

    library_setnonblocking(connection);

    /* find a free slot in conn */

    for ( i=0; i<MAX_CONNECTIONS; ++i )
    {
        if ( conn[i].conn_state == CONN_STATE_DISCONNECTED )    /* free connection slot -- we'll use it */
        {
            DEBUG("\nSecure connection accepted: %s, slot=%d, fd=%d", remote_addr, i, connection);
            conn[i].fd = connection;
            conn[i].secure = TRUE;

            conn[i].ssl = SSL_new(M_ssl_ctx);

            if ( !conn[i].ssl )
            {
                ERR("SSL_new failed");
                close_connection(i);
                return;
            }

            /* SSL_set_fd() sets the file descriptor fd as the input/bringput facility
               for the TLS/SSL (encrypted) side of ssl. fd will typically be the socket
               file descriptor of a network connection.
               When performing the operation, a socket BIO is automatically created to
               interface between the ssl and fd. The BIO and hence the SSL engine inherit
               the behaviour of fd. If fd is non-blocking, the ssl will also have non-blocking behaviour.
               If there was already a BIO connected to ssl, BIO_free() will be called
               (for both the reading and writing side, if different). */

            ret = SSL_set_fd(conn[i].ssl, connection);

            if ( ret <= 0 )
            {
                ERR("SSL_set_fd failed, ret = %d", ret);
                close_connection(i);
                return;
            }

            ret = SSL_accept(conn[i].ssl);  /* handshake here */

            if ( ret <= 0 )
            {
                conn[i].ssl_err = SSL_get_error(conn[i].ssl, ret);

                if ( conn[i].ssl_err != SSL_ERROR_WANT_READ && conn[i].ssl_err != SSL_ERROR_WANT_WRITE )
                {
                    ERR("SSL_accept failed, ssl_err = %d", conn[i].ssl_err);
                    close_connection(i);
                    return;
                }
            }

            strcpy(conn[i].ip, remote_addr);        /* possibly client IP */
            strcpy(conn[i].pip, remote_addr);       /* possibly proxy IP */
#ifdef DUMP
            DEBUG("Changing state to CONN_STATE_ACCEPTING");
#endif
            conn[i].conn_state = CONN_STATE_ACCEPTING;
            conn[i].last_activity = G_now;
            connection = -1;                        /* mark as OK */
            break;
        }
    }

    if (connection != -1)   /* none was free */
    {
        /* No room left in the queue! */
        WAR("No room left for new client, closing");
#ifdef _WIN32   /* Windows */
        closesocket(connection);
#else
        close(connection);
#endif  /* _WIN32 */
    }
#endif
}


/* --------------------------------------------------------------------------
   Read list of blocked IPs from the file
-------------------------------------------------------------------------- */
static void read_blocked_ips()
{
    char    fname[512];
    FILE    *h_file=NULL;
    int     c=0;
    int     i=0;
    char    now_value=1;
    char    now_comment=0;
    char    value[64]="";

    if ( G_blockedIPList[0] == EOS ) return;

    INF("Updating blocked IPs list");

    /* open the file */

    if ( G_blockedIPList[0] == '/' )    /* full path */
        strcpy(fname, G_blockedIPList);
    else if ( G_appdir[0] )
        sprintf(fname, "%s/bin/%s", G_appdir, G_blockedIPList);
    else
        strcpy(fname, G_blockedIPList);

    if ( NULL == (h_file=fopen(fname, "r")) )
    {
        WAR("Couldn't open %s\n", fname);
        return;
    }

    G_blacklist_cnt = 0;

    /* parse the file */

    while ( EOF != (c=fgetc(h_file)) )
    {
        if ( c == ' ' || c == ' ' || c == '\r' ) continue;  /* omit whitespaces */

        if ( c == '\n' )    /* end of value or end of comment or empty line */
        {
            if ( now_value && i )   /* end of value */
            {
                value[i] = EOS;
                if ( !ip_blocked(value) )   /* avoid duplicates */
                {
                    strcpy(G_blacklist[G_blacklist_cnt++], value);
                    if ( G_blacklist_cnt == MAX_BLACKLIST )
                    {
                        WAR("Blacklist full! (%d IPs)", G_blacklist_cnt);
                        now_value = 0;
                        break;
                    }
                }
                i = 0;
            }
            now_value = 1;
            now_comment = 0;
        }
        else if ( now_comment )
        {
            continue;
        }
        else if ( c == '#' )    /* possible end of value */
        {
            if ( now_value && i )   /* end of value */
            {
                value[i] = EOS;
                strcpy(G_blacklist[G_blacklist_cnt++], value);
                if ( G_blacklist_cnt == MAX_BLACKLIST )
                {
                    WAR("Blacklist full! (%d IPs)", G_blacklist_cnt);
                    now_value = 0;
                    break;
                }
                i = 0;
            }
            now_value = 0;
            now_comment = 1;
        }
        else if ( now_value )   /* value */
        {
            if ( i < INET_ADDRSTRLEN-1 )
                value[i++] = c;
        }
    }

    if ( now_value && i )   /* end of value */
    {
        value[i] = EOS;
        strcpy(G_blacklist[G_blacklist_cnt++], value);
    }

    if ( NULL != h_file )
        fclose(h_file);

    ALWAYS("%d IPs blacklisted", G_blacklist_cnt);

    /* show the list */

/*  DEBUG("");
    for ( i=0; i<G_blacklist_cnt; ++i )
        DEBUG("%s", G_blacklist[i]);
    DEBUG("");*/
}


/* --------------------------------------------------------------------------
   Return TRUE if addr is on our blacklist
-------------------------------------------------------------------------- */
static bool ip_blocked(const char *addr)
{
    int i;

    for ( i=0; i<G_blacklist_cnt; ++i )
    {
        if ( 0==strcmp(G_blacklist[i], addr) )
            return TRUE;
    }

    return FALSE;
}


/* --------------------------------------------------------------------------
   Read static resources from disk
   Read all the files from G_appdir/res or resmin directory
   path is a relative path uder `res` or `resmin`
-------------------------------------------------------------------------- */
static bool read_files(bool minify, bool first_scan, const char *path)
{
    int     i;
    char    resdir[STATIC_PATH_LEN];        /* full path to res */
    char    ressubdir[STATIC_PATH_LEN];     /* full path to res/subdir */
    char    namewpath[STATIC_PATH_LEN];     /* full path including file name */
    char    resname[STATIC_PATH_LEN];       /* relative path including file name */
    DIR     *dir;
    struct dirent *dirent;
    FILE    *fd;
    char    *data_tmp=NULL;
    char    *data_tmp_min=NULL;
    struct stat fstat;
    char    mod_time[32];

#ifndef _WIN32
    if ( G_appdir[0] == EOS ) return TRUE;
#endif

    if ( first_scan && !path ) DEBUG("");

#ifdef DUMP
//    DEBUG_LINE_LONG;
//    DEBUG("read_files, minify = %s", minify?"TRUE":"FALSE");
#endif

#ifdef _WIN32   /* be more forgiving */

    if ( G_appdir[0] )
    {
        if ( minify )
            sprintf(resdir, "%s/resmin", G_appdir);
        else
            sprintf(resdir, "%s/res", G_appdir);
    }
    else    /* no FASTYDIR */
    {
        if ( minify )
            strcpy(resdir, "../resmin");
        else
            strcpy(resdir, "../res");
    }

#else /* Linux -- don't fool around */

    if ( minify )
        sprintf(resdir, "%s/resmin", G_appdir);
    else
        sprintf(resdir, "%s/res", G_appdir);

#endif /* _WIN32 */

#ifdef DUMP
//    DEBUG("resdir [%s]", resdir);
#endif

    if ( !path )     /* highest level */
    {
        strcpy(ressubdir, resdir);
    }
    else    /* recursive call */
    {
        sprintf(ressubdir, "%s/%s", resdir, path);
    }

#ifdef DUMP
//    DEBUG("ressubdir [%s]", ressubdir);
#endif

    if ( (dir=opendir(ressubdir)) == NULL )
    {
//#ifdef DUMP
        if ( first_scan )
            DEBUG("Couldn't open directory [%s]", ressubdir);
//#endif
        return TRUE;    /* don't panic, just no external resources will be used */
    }

    /* ------------------------------------------------------------------- */
    /* check removed files */

    if ( !first_scan && !path )   /* on the highest level only */
    {
#ifdef DUMP
//        DEBUG("Checking removed files...");
#endif
        for ( i=0; i<=M_max_static; ++i )
        {
            if ( M_stat[i].name[0]==EOS ) continue;  /* already removed */

            if ( minify && M_stat[i].source != STATIC_SOURCE_RESMIN ) continue;

            if ( !minify && M_stat[i].source != STATIC_SOURCE_RES ) continue;
#ifdef DUMP
//            DEBUG("Checking %s...", M_stat[i].name);
#endif
            char fullpath[STATIC_PATH_LEN];
            sprintf(fullpath, "%s/%s", resdir, M_stat[i].name);

            if ( !library_exists(fullpath) )
            {
                INF("Removing %s from static resources", M_stat[i].name);

                if ( 0==strcmp(M_stat[i].name, "index.html") )
                    G_index_present = FALSE;

                M_stat[i].name[0] = EOS;
                free(M_stat[i].data);
            }
        }
    }

    /* ------------------------------------------------------------------- */
#ifdef DUMP
//    DEBUG("Reading %sfiles", first_scan?"":"new ");
#endif
    /* read the files into memory */

    while ( (dirent=readdir(dir)) )
    {
        if ( dirent->d_name[0] == '.' )  /* skip ".", ".." and hidden files */
            continue;

        /* ------------------------------------------------------------------- */
        /* resource name */

        if ( !path )
            strcpy(resname, dirent->d_name);
        else
            sprintf(resname, "%s/%s", path, dirent->d_name);

#ifdef DUMP
//        if ( first_scan )
//            DEBUG("resname [%s]", resname);
#endif

        /* ------------------------------------------------------------------- */
        /* additional file info */

        sprintf(namewpath, "%s/%s", resdir, resname);

#ifdef DUMP
//        if ( first_scan )
//            DEBUG("namewpath [%s]", namewpath);
#endif

        if ( stat(namewpath, &fstat) != 0 )
        {
            ERR("stat for [%s] failed, errno = %d (%s)", namewpath, errno, strerror(errno));
            closedir(dir);
            return FALSE;
        }

        /* ------------------------------------------------------------------- */

        if ( S_ISDIR(fstat.st_mode) )   /* directory */
        {
#ifdef DUMP
//            if ( first_scan )
//                DEBUG("Reading subdirectory [%s]...", dirent->d_name);
#endif
            read_files(minify, first_scan, resname);
            continue;
        }
        else if ( !S_ISREG(fstat.st_mode) )    /* skip if not a regular file nor directory */
        {
#ifdef DUMP
            if ( first_scan )
                DEBUG("[%s] is not a regular file", resname);
#endif
            continue;
        }

        /* ------------------------------------------------------------------- */
        /* already read? */

        bool reread = FALSE;

        if ( !first_scan )
        {
            bool exists = FALSE;

            for ( i=0; i<=M_max_static; ++i )
            {
                if ( M_stat[i].name[0]==EOS ) continue;  /* removed */

                if ( minify && M_stat[i].source != STATIC_SOURCE_RESMIN ) continue;

                if ( !minify && M_stat[i].source != STATIC_SOURCE_RES ) continue;

                /* ------------------------------------------------------------------- */

                if ( 0==strcmp(M_stat[i].name, resname) )
                {
#ifdef DUMP
//                    DEBUG("%s already read", resname);
#endif
                    if ( M_stat[i].modified == fstat.st_mtime )
                    {
#ifdef DUMP
//                        DEBUG("Not modified");
#endif
                        exists = TRUE;
                    }
                    else
                    {
                        INF("%s has been modified", resname);
                        reread = TRUE;
                    }

                    break;
                }
            }

            if ( exists ) continue;  /* not modified */
        }

        /* find the first unused slot in M_stat array */

        if ( !reread )
        {
            i = first_free_stat();
            /* file name */
            strcpy(M_stat[i].name, resname);
        }

        /* last modified */

        M_stat[i].modified = fstat.st_mtime;

        /* size and content */

#ifdef _WIN32   /* Windows */
        if ( NULL == (fd=fopen(namewpath, "rb")) )
#else
        if ( NULL == (fd=fopen(namewpath, "r")) )
#endif  /* _WIN32 */
            ERR("Couldn't open %s", namewpath);
        else
        {
            fseek(fd, 0, SEEK_END);     /* determine the file size */
            M_stat[i].len = ftell(fd);
            rewind(fd);

            if ( minify )
            {
                /* we don't know the minified size yet -- read file into temp buffer */

                if ( NULL == (data_tmp=(char*)malloc(M_stat[i].len+1)) )
                {
                    ERR("Couldn't allocate %ld bytes for %s!", M_stat[i].len, M_stat[i].name);
                    fclose(fd);
                    closedir(dir);
                    return FALSE;
                }

                if ( NULL == (data_tmp_min=(char*)malloc(M_stat[i].len+1)) )
                {
                    ERR("Couldn't allocate %ld bytes for %s!", M_stat[i].len, M_stat[i].name);
                    fclose(fd);
                    closedir(dir);
                    return FALSE;
                }

                fread(data_tmp, M_stat[i].len, 1, fd);
                *(data_tmp+M_stat[i].len) = EOS;

                M_stat[i].len = fasty_minify(data_tmp_min, data_tmp);  /* new length */
            }

            /* allocate the final destination */

            if ( reread )
                free(M_stat[i].data);

            if ( NULL == (M_stat[i].data=(char*)malloc(M_stat[i].len+1)) )
            {
                ERR("Couldn't allocate %ld bytes for %s!", M_stat[i].len+1, M_stat[i].name);
                fclose(fd);
                closedir(dir);
                return FALSE;
            }

            if ( minify )
            {
                memcpy(M_stat[i].data, data_tmp_min, M_stat[i].len+1);
                free(data_tmp);
                free(data_tmp_min);
                data_tmp = NULL;
                data_tmp_min = NULL;

                M_stat[i].source = STATIC_SOURCE_RESMIN;
            }
            else
            {
                fread(M_stat[i].data, M_stat[i].len, 1, fd);

                M_stat[i].source = STATIC_SOURCE_RES;
            }

            fclose(fd);

            if ( !reread )
            {
                M_stat[i].type = get_res_type(M_stat[i].name);

                if ( 0==strcmp(M_stat[i].name, "index.html") )
                    G_index_present = TRUE;
            }

            /* log file info */

            if ( G_logLevel > LOG_INF )
            {
                G_ptm = gmtime(&M_stat[i].modified);
                sprintf(mod_time, "%d-%02d-%02d %02d:%02d:%02d", G_ptm->tm_year+1900, G_ptm->tm_mon+1, G_ptm->tm_mday, G_ptm->tm_hour, G_ptm->tm_min, G_ptm->tm_sec);
                G_ptm = gmtime(&G_now);     /* set it back */
                DEBUG("%s %s\t\t%ld bytes", library_add_spaces(M_stat[i].name, 28), mod_time, M_stat[i].len);
            }
        }

#ifdef DUMP
//      if ( minify )   /* temporarily */
//          DEBUG("minified %s: [%s]", M_stat[i].name, M_stat[i].data);
#endif /* DUMP */
    }

    closedir(dir);

    if ( first_scan && !path ) DEBUG("");

    return TRUE;
}


/* --------------------------------------------------------------------------
   Find first free slot in M_stat
-------------------------------------------------------------------------- */
static int first_free_stat()
{
    int i=0;

    for ( i=0; i<MAX_STATICS; ++i )
    {
        if ( M_stat[i].name[0]=='-' || M_stat[i].name[0]==EOS )
        {
            if ( i > M_max_static ) M_max_static = i;
            return i;
        }
    }

    ERR("MAX_STATICS reached (%d)! You can set/increase APP_MAX_STATICS in fasty_app.h.", MAX_STATICS);

    return -1;  /* nothing's free, we ran bring of statics! */
}


/* --------------------------------------------------------------------------
   Return M_stat array index if name is on statics' list
-------------------------------------------------------------------------- */
static int is_static_res(int ci, const char *name)
{
    int i;

    for ( i=0; M_stat[i].name[0] != '-'; ++i )
    {
        if ( 0==strcmp(M_stat[i].name, name) )
        {
//          DEBUG("It is static");
            if ( conn[ci].if_mod_since >= M_stat[i].modified )
            {
//              DEBUG("Not Modified");
                conn[ci].status = 304;  /* Not Modified */
            }
            return i;
        }
    }

    return -1;
}


/* --------------------------------------------------------------------------
   Open database connection
-------------------------------------------------------------------------- */
static bool open_db()
{
#ifdef DBMYSQL
    if ( NULL == (G_dbconn=mysql_init(NULL)) )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return FALSE;
    }

#ifdef DBMYSQLRECONNECT
    my_bool reconnect=1;
    mysql_options(G_dbconn, MYSQL_OPT_RECONNECT, &reconnect);
#endif

//    unsigned long max_packet=33554432;  /* 32 MB */
//    mysql_options(G_dbconn, MYSQL_OPT_MAX_ALLOWED_PACKET, &max_packet);

    if ( NULL == mysql_real_connect(G_dbconn, G_dbHost[0]?G_dbHost:NULL, G_dbUser, G_dbPassword, G_dbName, G_dbPort, NULL, 0) )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return FALSE;
    }
#endif
    return TRUE;
}


/* --------------------------------------------------------------------------
   Main new request processing
   Request received over current conn is already parsed
-------------------------------------------------------------------------- */
static void process_req(int ci)
{
    int     ret=OK;

    DEBUG("process_req, ci=%d", ci);

#ifdef DUMP
    if ( conn[ci].post && conn[ci].data )
        log_long(conn[ci].data, conn[ci].was_read, "POST data");
#endif

    conn[ci].p_curr_c = conn[ci].bring_data;

    conn[ci].location[COLON_POSITION] = '-';    /* no protocol here yet */

    /* ------------------------------------------------------------------------ */
    /* Generate HTML content before header -- to know its size & type --------- */

    /* ------------------------------------------------------------------------ */
    /* authorization check / log in from cookies ------------------------------ */

    if ( conn[ci].static_res == NOT_STATIC && conn[ci].status == 200 )
    {
#ifdef USERS
        if ( conn[ci].cookie_in_l[0] )  /* logged in sesid cookie present */
        {
            ret = libusr_l_usession_ok(ci);     /* is it valid? */

            if ( ret == OK )    /* valid sesid -- user logged in */
                DEBUG("User logged in from cookie");
            else if ( ret != ERR_INT_SERVER_ERROR && ret != ERR_SERVER_TOOBUSY )    /* dodged sesid... or session expired */
                WAR("Invalid ls cookie");
        }

        if ( conn[ci].auth_level==AUTH_LEVEL_ADMIN && !ADMIN )  /* return not found */
        {
            INF("AUTH_LEVEL_ADMIN required, returning 404");
            ret = ERR_NOT_FOUND;
            RES_DONT_CACHE;
        }
        else if ( conn[ci].auth_level==AUTH_LEVEL_LOGGEDIN && !LOGGED )    /* redirect to login page */
        {
            INF("AUTH_LEVEL_LOGGEDIN required, redirecting to login");
            ret = ERR_REDIRECTION;
            if ( !strlen(APP_LOGIN_URI) )   /* login page = landing page */
                sprintf(conn[ci].location, "%s://%s", PROTOCOL, conn[ci].host);
            else
                strcpy(conn[ci].location, APP_LOGIN_URI);
        }
        else    /* login not required for this URI */
        {
            ret = OK;
        }

        if ( conn[ci].auth_level==AUTH_LEVEL_ANONYMOUS && !REQ_BOT && !conn[ci].head_only && !LOGGED )    /* anonymous user session required */
#else
        if ( conn[ci].auth_level==AUTH_LEVEL_ANONYMOUS && !REQ_BOT && !conn[ci].head_only )
#endif
        {
            if ( !conn[ci].cookie_in_a[0] || !a_usession_ok(ci) )       /* valid anonymous sesid cookie not present */
            {
                if ( !engine_uses_start(ci) )  /* start new anonymous user session */
                    ret = ERR_SERVER_TOOBUSY;   /* user sessions exhausted */
            }
        }

        /* ------------------------------------------------------------------------ */
        /* process request -------------------------------------------------------- */

        if ( ret == OK )
        {
            if ( !conn[ci].location[0] )
                ret = app_process_req(ci);  /* main application called here */
        }

        conn[ci].last_activity = G_now;
        if ( conn[ci].usi ) US.last_activity = G_now;

#ifdef ASYNC
        if ( conn[ci].conn_state == CONN_STATE_WAITING_FOR_ASYNC )
        {
            return;
        }
#endif
        /* ------------------------------------------------------------------------ */

        if ( conn[ci].location[0] || ret == ERR_REDIRECTION )   /* redirection has a priority */
            conn[ci].status = 303;
        else if ( ret == ERR_INVALID_REQUEST )
            conn[ci].status = 400;
        else if ( ret == ERR_UNAUTHORIZED )
            conn[ci].status = 401;
        else if ( ret == ERR_FORBIDDEN )
            conn[ci].status = 403;
        else if ( ret == ERR_NOT_FOUND )
            conn[ci].status = 404;
        else if ( ret == ERR_INT_SERVER_ERROR )
            conn[ci].status = 500;
        else if ( ret == ERR_SERVER_TOOBUSY )
            conn[ci].status = 503;

        if ( ret==ERR_REDIRECTION || ret==ERR_INVALID_REQUEST || ret==ERR_UNAUTHORIZED || ret==ERR_FORBIDDEN || ret==ERR_NOT_FOUND || ret==ERR_INT_SERVER_ERROR || ret==ERR_SERVER_TOOBUSY )
        {
#ifdef USERS
            if ( conn[ci].usi && !LOGGED ) close_a_uses(conn[ci].usi);
#else
            if ( conn[ci].usi ) close_a_uses(conn[ci].usi);
#endif
            if ( !conn[ci].keep_content )
            {
                conn[ci].p_curr_c = conn[ci].bring_data;      /* reset bring buffer pointer as it could have contained something already */
                gen_page_msg(ci, ret);
            }
        }
    }
}


/* --------------------------------------------------------------------------
   Generate HTTP response header
-------------------------------------------------------------------------- */
static void gen_response_header(int ci)
{
    DEBUG("gen_response_header, ci=%d", ci);

    conn[ci].p_curr_h = conn[ci].header;

    PRINT_HTTP_STATUS(conn[ci].status);

    if ( conn[ci].status == 301 || conn[ci].status == 303 )     /* redirection */
    {
        DEBUG("Redirecting");

        /*
           1 - upgrade 2 https, keep URI (301)
           2 - app new page version, ignore URI, use location (303)
           3 - redirect to final domain, keep URI (301)
        */

        if ( conn[ci].upgrade2https )   /* (1) */
        {
            PRINT_HTTP_VARY_UIR;    /* Upgrade-Insecure-Requests */
            sprintf(G_tmp, "Location: https://%s/%s\r\n", conn[ci].host, conn[ci].uri);
        }
        else if ( conn[ci].location[0] == 'h'        /* (2) full address already present */
                    && conn[ci].location[1] == 't'
                    && conn[ci].location[2] == 't'
                    && conn[ci].location[3] == 'p' )
        {
            sprintf(G_tmp, "Location: %s\r\n", conn[ci].location);
        }
        else if ( conn[ci].location[0] )        /* (2) */
        {
            sprintf(G_tmp, "Location: %s://%s/%s\r\n", PROTOCOL, conn[ci].host, conn[ci].location);
        }
        else if ( conn[ci].uri[0] ) /* (3) URI */
        {
#ifdef DOMAINONLY
            sprintf(G_tmp, "Location: %s://%s/%s\r\n", PROTOCOL, G_test?conn[ci].host:APP_DOMAIN, conn[ci].uri);
#else
            sprintf(G_tmp, "Location: %s://%s/%s\r\n", PROTOCOL, conn[ci].host, conn[ci].uri);
#endif
        }
        else    /* (3) No URI */
        {
#ifdef DOMAINONLY
            sprintf(G_tmp, "Location: %s://%s\r\n", PROTOCOL, G_test?conn[ci].host:APP_DOMAIN);
#else
            sprintf(G_tmp, "Location: %s://%s\r\n", PROTOCOL, conn[ci].host);
#endif
        }
        HBRING(G_tmp);

        conn[ci].clen = 0;
    }
    else if ( conn[ci].status == 304 )      /* not modified since */
    {
//        DEBUG("Not Modified");

        if ( conn[ci].static_res == NOT_STATIC )
        {
            PRINT_HTTP_LAST_MODIFIED(G_last_modified);
        }
        else    /* static res */
        {
            PRINT_HTTP_LAST_MODIFIED(time_epoch2http(M_stat[conn[ci].static_res].modified));
        }

        conn[ci].clen = 0;
    }
    else    /* normal response with content: 2xx, 4xx, 5xx */
    {
//        DEBUG("Normal response");

        if ( conn[ci].dont_cache )  /* dynamic content */
        {
            PRINT_HTTP_VARY_DYN;
            PRINT_HTTP_NO_CACHE;
        }
        else    /* static content */
        {
            PRINT_HTTP_VARY_STAT;

            if ( conn[ci].static_res == NOT_STATIC )
            {
                if ( conn[ci].modified )
                    PRINT_HTTP_LAST_MODIFIED(time_epoch2http(conn[ci].modified));
                else
                    PRINT_HTTP_LAST_MODIFIED(G_last_modified);

                PRINT_HTTP_EXPIRES;
            }
            else    /* static res */
            {
                PRINT_HTTP_LAST_MODIFIED(time_epoch2http(M_stat[conn[ci].static_res].modified));
            }
        }

        if ( conn[ci].static_res == NOT_STATIC )
            conn[ci].clen = conn[ci].p_curr_c - conn[ci].bring_data;
        else
            conn[ci].clen = M_stat[conn[ci].static_res].len;
    }

    /* Date */

    PRINT_HTTP_DATE;

    /* Connection */

    PRINT_HTTP_CONNECTION(ci);

    /* Content-Length */

    PRINT_HTTP_CONTENT_LEN(conn[ci].clen);

    /* Cookie */

    if ( conn[ci].static_res == NOT_STATIC && (conn[ci].status == 200 || conn[ci].status == 303) && !conn[ci].head_only )
    {
        if ( conn[ci].cookie_bring_l[0] )         /* logged in cookie has been produced */
        {
            if ( conn[ci].cookie_bring_l_exp[0] )
            {
                PRINT_HTTP_COOKIE_L_EXP(ci);    /* with expiration date */
            }
            else
            {
                PRINT_HTTP_COOKIE_L(ci);
            }
        }

        if ( conn[ci].cookie_bring_a[0] )         /* anonymous cookie has been produced */
        {
            if ( conn[ci].cookie_bring_a_exp[0] )
            {
                PRINT_HTTP_COOKIE_A_EXP(ci);    /* with expiration date */
            }
            else
            {
                PRINT_HTTP_COOKIE_A(ci);
            }
        }
    }

    /* Content-Type */

    if ( conn[ci].clen == 0 )   /* don't set for these */
    {                   /* this covers 301, 303 and 304 */
    }
    else if ( conn[ci].static_res != NOT_STATIC )   /* static resource */
    {
        print_content_type(ci, M_stat[conn[ci].static_res].type);
    }
    else if ( conn[ci].ctype == CONTENT_TYPE_USER )
    {
        sprintf(G_tmp, "Content-Type: %s\r\n", conn[ci].ctypestr);
        HBRING(G_tmp);
    }
    else if ( conn[ci].ctype != CONTENT_TYPE_UNSET )
    {
        print_content_type(ci, conn[ci].ctype);
    }

    if ( conn[ci].cdisp[0] )
    {
        sprintf(G_tmp, "Content-Disposition: %s\r\n", conn[ci].cdisp);
        HBRING(G_tmp);
    }

#ifndef NO_IDENTITY
    PRINT_HTTP_SERVER;
#endif

    PRINT_HTTP_END_OF_HEADER;

    DEBUG("Response status: %d", conn[ci].status);

#ifdef DUMP     /* low-level tests */
    DEBUG("Changing state to CONN_STATE_READY_TO_SEND_HEADER");
#endif
    conn[ci].conn_state = CONN_STATE_READY_TO_SEND_HEADER;

    DEBUG("\nResponse header:\n\n[%s]\n", conn[ci].header);

#ifdef DUMP     /* low-level tests */
    if ( G_logLevel>=LOG_DEBUG && conn[ci].clen > 0 && !conn[ci].head_only && conn[ci].static_res == NOT_STATIC && (conn[ci].ctype == CONTENT_TYPE_UNSET || conn[ci].ctype == RES_TEXT || conn[ci].ctype == RES_HTML) )
        log_long(conn[ci].bring_data, conn[ci].clen, "Content to send");
#endif

    conn[ci].last_activity = G_now;
    if ( conn[ci].usi ) US.last_activity = G_now;
}


/* --------------------------------------------------------------------------
   Print Content-Type to response header
-------------------------------------------------------------------------- */
static void print_content_type(int ci, char type)
{
    char    http_type[32]="text/plain";     /* default */

    if ( type == RES_HTML )
        strcpy(http_type, "text/html; charset=utf-8");
    else if ( type == RES_CSS )
        strcpy(http_type, "text/css");
    else if ( type == RES_JS )
        strcpy(http_type, "application/javascript");
    else if ( type == RES_GIF )
        strcpy(http_type, "image/gif");
    else if ( type == RES_JPG )
        strcpy(http_type, "image/jpeg");
    else if ( type == RES_ICO )
        strcpy(http_type, "image/x-icon");
    else if ( type == RES_PNG )
        strcpy(http_type, "image/png");
    else if ( type == RES_BMP )
        strcpy(http_type, "image/bmp");
    else if ( type == RES_SVG )
        strcpy(http_type, "image/svg+xml");
    else if ( type == RES_PDF )
        strcpy(http_type, "application/pdf");
    else if ( type == RES_AMPEG )
        strcpy(http_type, "audio/mpeg");
    else if ( type == RES_EXE )
        strcpy(http_type, "application/x-msdownload");
    else if ( type == RES_ZIP )
        strcpy(http_type, "application/zip");

    sprintf(G_tmp, "Content-Type: %s\r\n", http_type);
    HBRING(G_tmp);
}


/* --------------------------------------------------------------------------
   Verify IP & User-Agent against sesid in uses (anonymous users)
   Return user session array index if all ok
-------------------------------------------------------------------------- */
static bool a_usession_ok(int ci)
{
    int i;

    for (i=1; i<=MAX_SESSIONS; ++i)
    {
        if ( uses[i].sesid[0] && !uses[i].logged && 0==strcmp(conn[ci].cookie_in_a, uses[i].sesid)
/*              && 0==strcmp(conn[ci].ip, uses[i].ip) */
                && 0==strcmp(conn[ci].uagent, uses[i].uagent) )
        {
            DEBUG("Anonymous session found, usi=%d, sesid [%s]", i, uses[i].sesid);
            conn[ci].usi = i;
            return TRUE;
        }
    }

    /* not found */
    return FALSE;
}


/* --------------------------------------------------------------------------
   Close timebringed connections
-------------------------------------------------------------------------- */
static void close_old_conn()
{
    int     i;
    time_t  last_allowed;

    last_allowed = G_now - CONN_TIMEBRING;

    for ( i=0; i<MAX_CONNECTIONS; ++i )
    {
        if ( conn[i].conn_state != CONN_STATE_DISCONNECTED && conn[i].last_activity < last_allowed )
        {
            DEBUG("Closing timebringed connection %d", i);
            close_connection(i);
        }
    }
}


/* --------------------------------------------------------------------------
  close timebringed anonymous user sessions
-------------------------------------------------------------------------- */
static void close_uses_timebring()
{
    int     i;
    time_t  last_allowed;

    last_allowed = G_now - USES_TIMEBRING;

    for ( i=1; i<=MAX_SESSIONS; ++i )
    {
        if ( uses[i].sesid[0] && !uses[i].logged && uses[i].last_activity < last_allowed )
            close_a_uses(i);
    }
}


/* --------------------------------------------------------------------------
  close anonymous user session
-------------------------------------------------------------------------- */
static void close_a_uses(int usi)
{
    DEBUG("Closing anonymous session, usi=%d, sesid [%s]", usi, uses[usi].sesid);
    engine_uses_close(usi);
}


/* --------------------------------------------------------------------------
  reset connection after processing request
-------------------------------------------------------------------------- */
static void reset_conn(int ci, char conn_state)
{
#ifdef DUMP
    DEBUG("Resetting connection ci=%d, fd=%d, new state == %s\n", ci, conn[ci].fd, conn_state==CONN_STATE_CONNECTED?"CONN_STATE_CONNECTED":"CONN_STATE_DISCONNECTED");
#endif

    conn[ci].conn_state = conn_state;

    conn[ci].status = 200;
    conn[ci].method[0] = EOS;
    conn[ci].head_only = FALSE;
    conn[ci].post = FALSE;
    if ( conn[ci].data )
    {
        free(conn[ci].data);
        conn[ci].data = NULL;
    }
    conn[ci].was_read = 0;
    conn[ci].upgrade2https = FALSE;
    conn[ci].data_sent = 0;
    conn[ci].resource[0] = EOS;
    conn[ci].id[0] = EOS;
    conn[ci].uagent[0] = EOS;
    conn[ci].mobile = FALSE;
    conn[ci].referer[0] = EOS;
    conn[ci].keep_alive = FALSE;
    conn[ci].proto[0] = EOS;
    conn[ci].clen = 0;
    conn[ci].cookie_in_a[0] = EOS;
    conn[ci].cookie_in_l[0] = EOS;
    conn[ci].host[0] = EOS;
    strcpy(conn[ci].website, APP_WEBSITE);
    conn[ci].lang[0] = EOS;
    conn[ci].if_mod_since = 0;
    conn[ci].in_ctypestr[0] = EOS;
    conn[ci].in_ctype = CONTENT_TYPE_URLENCODED;
    conn[ci].boundary[0] = EOS;
    conn[ci].authorization[0] = EOS;
    conn[ci].auth_level = APP_DEF_AUTH_LEVEL;
    conn[ci].usi = 0;
    conn[ci].static_res = NOT_STATIC;
    conn[ci].ctype = RES_HTML;
    conn[ci].cdisp[0] = EOS;
    conn[ci].modified = 0;
    conn[ci].cookie_bring_a[0] = EOS;
    conn[ci].cookie_bring_a_exp[0] = EOS;
    conn[ci].cookie_bring_l[0] = EOS;
    conn[ci].cookie_bring_l_exp[0] = EOS;
    conn[ci].location[0] = EOS;
    REQ_BOT = FALSE;
    conn[ci].expect100 = FALSE;
    conn[ci].dont_cache = FALSE;
    conn[ci].keep_content = FALSE;
}


/* --------------------------------------------------------------------------
  parse HTTP request
  return HTTP status code
-------------------------------------------------------------------------- */
static int parse_req(int ci, long len)
{
    int     ret=200;
    long    hlen;
    char    *p_hend=NULL;
    long    i;
    long    j=0;
    char    flg_data=FALSE;
    char    now_label=TRUE;
    char    now_value=FALSE;
    char    was_cr=FALSE;
    char    label[MAX_LABEL_LEN+1];
    char    value[MAX_VALUE_LEN+1];
    char    *p_question=NULL;

    /* --------------------------------------------

    Shortest valid request:

    GET / HTTP/1.1      15 including \n +
    Host: 1.1.1.1       14 including \n = 29

    -------------------------------------------- */

    DEBUG("parse_req, ci=%d", ci);

    ++G_cnts_today.req;
    conn[ci].req = G_cnts_today.req;    /* superfluous? */

    DEBUG("\n------------------------------------------------\n %s  Request %ld\n------------------------------------------------\n", G_dt, conn[ci].req);

//  if ( conn[ci].conn_state != STATE_SENDING ) /* ignore Range requests for now */
//      conn[ci].conn_state = STATE_RECEIVED;   /* by default */

    if ( len < 14 ) /* ignore any junk */
    {
        DEBUG("request len < 14, ignoring");
        return 400; /* Bad Request */
    }

    /* look for end of header */

    p_hend = strstr(conn[ci].in, "\r\n\r\n");

    if ( !p_hend )
    {
        p_hend = strstr(conn[ci].in, "\n\n");

        if ( !p_hend )
        {
            if ( 0 == strncmp(conn[ci].in, "GET / HTTP/1.", 13) )   /* temporary solution for good looking partial requests */
            {
                strcat(conn[ci].in, "\n");  /* for values reading algorithm */
                p_hend = conn[ci].in + len;
            }
            else
            {
                DEBUG("Request syntax error, ignoring");
                return 400; /* Bad Request */
            }
        }
    }

    hlen = p_hend - conn[ci].in;    /* HTTP header length including first of the last new line characters to simplify parsing algorithm in the third 'for' loop below */

    /* temporarily insert EOS at the end of header to avoid logging POST data */

    char eoh = conn[ci].in[hlen];
    conn[ci].in[hlen] = EOS;
    DEBUG("Incoming buffer:\n\n[%s]\n", conn[ci].in);
    conn[ci].in[hlen] = eoh;

    ++hlen;     /* HTTP header length including first of the last new line characters to simplify parsing algorithm in the third 'for' loop below */

    /* parse the header -------------------------------------------------------------------------- */

    for ( i=0; i<hlen; ++i )    /* the first line is special -- consists of more than one token */
    {                                   /* the very first token is a request method */
        if ( isalpha(conn[ci].in[i]) )
        {
            if ( i < MAX_METHOD_LEN )
                conn[ci].method[i] = conn[ci].in[i];
            else
            {
                ERR("Method too long, ignoring");
                return 400; /* Bad Request */
            }
        }
        else    /* most likely space = end of method */
        {
            conn[ci].method[i] = EOS;

            /* check against the list of allowed methods */

            if ( 0==strcmp(conn[ci].method, "GET") )
            {
                /* just go ahead */
            }
            else if ( 0==strcmp(conn[ci].method, "POST") || 0==strcmp(conn[ci].method, "PUT") || 0==strcmp(conn[ci].method, "DELETE") )
            {
                conn[ci].post = TRUE;   /* read payload */
            }
            else if ( 0==strcmp(conn[ci].method, "OPTIONS") )
            {
                /* just go ahead */
            }
            else if ( 0==strcmp(conn[ci].method, "HEAD") )
            {
                conn[ci].head_only = TRUE;  /* send only a header */
            }
            else
            {
                ERR("Method [%s] not allowed, ignoring", conn[ci].method);
                return 405;
            }

            break;
        }
    }

    /* only for low-level tests ------------------------------------- */
//  DEBUG("method: [%s]", conn[ci].method);
    /* -------------------------------------------------------------- */

    i += 2;     /* skip " /" */

    for ( i; i<hlen; ++i )  /* URI */
    {
        if ( conn[ci].in[i] != ' ' && conn[ci].in[i] != '\t' )
        {
            if ( j < MAX_URI_LEN )
                conn[ci].uri[j++] = conn[ci].in[i];
            else
            {
                ERR("URI too long, ignoring");
                return 414; /* Request-URI Too Long */
            }
        }
        else    /* end of URI */
        {
            conn[ci].uri[j] = EOS;
            break;
        }
    }

    /* only for low-level tests ------------------------------------- */
//  DEBUG("URI: [%s]", conn[ci].uri);
    /* -------------------------------------------------------------- */

    j = 0;
    while ( i < hlen && conn[ci].in[i] != '\r' && conn[ci].in[i] != '\n' )
    {
        if ( conn[ci].in[i] != ' ' && j < 15 )
            conn[ci].proto[j++] = conn[ci].in[i];
        ++i;
    }
    conn[ci].proto[j] = EOS;
#ifdef DUMP
//    DEBUG("proto [%s]", conn[ci].proto);
#endif

    while ( i < hlen && conn[ci].in[i] != '\n' ) ++i;

    j = 0;

    for ( i; i<hlen; ++i )  /* next lines */
    {
        if ( !now_value && (conn[ci].in[i] == ' ' || conn[ci].in[i] == '\t') )  /* omit whitespaces */
            continue;

        if ( conn[ci].in[i] == '\n' && was_cr )
        {
            was_cr = FALSE;
            continue;   /* value has already been saved in a previous loop go */
        }

        if ( conn[ci].in[i] == '\r' )
            was_cr = TRUE;

        if ( conn[ci].in[i] == '\r' || conn[ci].in[i] == '\n' ) /* end of value. Caution: \n only if continue above is in place! */
        {
            if ( now_value )
            {
                value[j] = EOS;
                if ( j == 0 )
                    WAR("Value of %s is empty!", label);
                else
                    if ( (ret=set_http_req_val(ci, label, value+1)) != 200 ) return ret;
            }
            now_label = TRUE;
            now_value = FALSE;
            j = 0;
        }
        else if ( now_label && conn[ci].in[i] == ':' )  /* end of label, start of value */
        {
            label[j] = EOS;
            now_label = FALSE;
            now_value = TRUE;
            j = 0;
        }
        else if ( now_label )   /* label */
        {
            if ( j < MAX_LABEL_LEN )
                label[j++] = conn[ci].in[i];
            else
            {
                label[j] = EOS;
                WAR("Label [%s] too long, ignoring", label);
                return 400; /* Bad Request */
            }
        }
        else if ( now_value )   /* value */
        {
            value[j++] = conn[ci].in[i];
            if ( j == MAX_VALUE_LEN )   /* truncate here */
            {
                WAR("Truncating %s's value", label);
                value[j] = EOS;
                if ( (ret=set_http_req_val(ci, label, value+1)) != 200 ) return ret;
                now_value = FALSE;
            }
        }
    }

    /* behave as one good web server ------------------------------------- */

#ifndef DONT_LOOK_FOR_INDEX

    if ( conn[ci].uri[0]==EOS && G_index_present && REQ_GET )
    {
        INF("Serving index.html");
        strcpy(conn[ci].uri, "index.html");
    }

#endif /* DONT_LOOK_FOR_INDEX */

    /* split URI and resource / id --------------------------------------- */

    if ( conn[ci].uri[0] )  /* if not empty */
    {
        if ( (0==strcmp(conn[ci].uri, "favicon.ico") && !M_favicon_exists)
                || (0==strcmp(conn[ci].uri, "robots.txt") && !M_robots_exists)
                || (0==strcmp(conn[ci].uri, "apple-touch-icon.png") && !M_appleicon_exists) )
            return 404;     /* Not Found */

        strncpy(conn[ci].resource, conn[ci].uri, MAX_RESOURCE_LEN);
        conn[ci].resource[MAX_RESOURCE_LEN] = EOS;

        if ( p_question=strchr(conn[ci].resource, '/') )    /* there's an id part of URI */
        {
            conn[ci].resource[p_question-conn[ci].resource] = EOS;

            strncpy(conn[ci].id, ++p_question, MAX_ID_LEN);
            conn[ci].id[MAX_ID_LEN] = EOS;

            if ( p_question=strchr(conn[ci].id, '?') )
                conn[ci].id[p_question-conn[ci].id] = EOS;
        }
        else if ( p_question=strchr(conn[ci].resource, '?') )   /* no id but query string may be present */
        {
            conn[ci].resource[p_question-conn[ci].resource] = EOS;
        }

        DEBUG("resource: [%s]", conn[ci].resource);
        DEBUG("id: [%s]", conn[ci].id);

        conn[ci].static_res = is_static_res(ci, conn[ci].uri);     /* statics --> set the flag!!! */
        /* now, it may have set conn[ci].status to 304 */
    }

    /* get the required authorization level for this resource */

    if ( conn[ci].static_res == NOT_STATIC )
    {
        i = 0;
        while ( M_auth_levels[i].resource[0] != '-' )
        {
            if ( REQ(M_auth_levels[i].resource) )
            {
                conn[ci].auth_level = M_auth_levels[i].level;
                break;
            }
            ++i;
        }
    }
    else    /* don't do any checks for static resources */
    {
        conn[ci].auth_level = AUTH_LEVEL_NONE;
    }

    /* ignore Range requests for now -------------------------------------------- */

/*  if ( conn[ci].conn_state == STATE_SENDING )
    {
        DEBUG("conn_state == STATE_SENDING, this request will be ignored");
        return 200;
    } */

    DEBUG("bot = %s", REQ_BOT?"TRUE":"FALSE");

    /* update request counters -------------------------------------------------- */

    if ( REQ_BOT )
        ++G_cnts_today.req_bot;

    if ( conn[ci].mobile )
        ++G_cnts_today.req_mob;
    else
        ++G_cnts_today.req_dsk;

    /* Block IP? ---------------------------------------------------------------- */

#ifdef BLACKLISTAUTOUPDATE
        if ( check_block_ip(ci, "Resource", conn[ci].resource) )
            return 403;     /* Forbidden */
#endif

#ifdef DOMAINONLY
        if ( !G_test && 0!=strcmp(conn[ci].host, APP_DOMAIN) )
            return 301;     /* Moved permanently */
#endif

    /* handle the POST content -------------------------------------------------- */

    if ( conn[ci].post && conn[ci].clen > 0 )
    {
        /* i = number of request characters read so far */

        /* p_hend will now point to the content */

        if ( 0==strncmp(p_hend, "\r\n\r\n", 4) )
            p_hend += 4;
        else    /* was "\n\n" */
            p_hend += 2;

        len = conn[ci].in+len - p_hend;         /* remaining request length -- likely a content */

        if ( len > conn[ci].clen )
            return 400;     /* Bad Request */

        /* copy so far received POST data from conn[ci].in to conn[ci].data */

        if ( NULL == (conn[ci].data=(char*)malloc(conn[ci].clen+1)) )
        {
            ERR("Couldn't allocate %d bytes for POST data!!!", conn[ci].clen);
            return 500;     /* Internal Sever Error */
        }

        memcpy(conn[ci].data, p_hend, len);
        conn[ci].was_read = len;    /* if POST then was_read applies to data section only! */

        if ( len < conn[ci].clen )      /* the whole content not received yet */
        {                               /* this is the only case when conn_state != received */
            DEBUG("The whole content not received yet");
#ifdef DUMP
            DEBUG("Changing state to CONN_STATE_READING_DATA");
#endif
            conn[ci].conn_state = CONN_STATE_READING_DATA;
            return ret;
        }
        else    /* the whole content received with the header at once */
        {
            conn[ci].data[len] = EOS;
            DEBUG("POST data received with header");
        }
    }

    if ( conn[ci].status == 304 )   /* Not Modified */
        return 304;
    else
        return ret;
}


/* --------------------------------------------------------------------------
  set request properties read from HTTP request header
  caller is responsible for ensuring value length > 0
  return HTTP status code
-------------------------------------------------------------------------- */
static int set_http_req_val(int ci, const char *label, const char *value)
{
    int     len;
    char    new_value[MAX_VALUE_LEN+1];
    char    ulabel[MAX_LABEL_LEN+1];
    char    uvalue[MAX_VALUE_LEN+1];
    char    *p;
    int     i;

    /* only for low-level tests ------------------------------------- */
//  DEBUG("label: [%s], value: [%s]", label, value);
    /* -------------------------------------------------------------- */

    strcpy(ulabel, upper(label));

    if ( 0==strcmp(ulabel, "HOST") )
    {
#ifdef BLACKLISTAUTOUPDATE
        if ( check_block_ip(ci, "Host", value) )
            return 403;     /* Forbidden */
#endif
        strcpy(conn[ci].host, value);
    }
    else if ( 0==strcmp(ulabel, "USER-AGENT") )
    {
#ifdef BLACKLISTAUTOUPDATE
        if ( check_block_ip(ci, "User-Agent", value) )
            return 403;     /* Forbidden */
#endif
        strcpy(conn[ci].uagent, value);
        strcpy(uvalue, upper(value));
        if ( strstr(uvalue, "ANDROID") || strstr(uvalue, "IPHONE") || strstr(uvalue, "SYMBIAN") || strstr(uvalue, "BLACKBERRY") || strstr(uvalue, "MOBILE") )
        {
            conn[ci].mobile = TRUE;
        }

        DEBUG("mobile = %s", conn[ci].mobile?"TRUE":"FALSE");

/*      if ( !REQ_BOT &&
                (strstr(uvalue, "ADSBOT")
                || strstr(uvalue, "BAIDU")
                || strstr(uvalue, "UPTIMEBOT")
                || strstr(uvalue, "SEMRUSHBOT")
                || strstr(uvalue, "SEZNAMBOT")
                || strstr(uvalue, "SCANBOT")
                || strstr(uvalue, "SYSSCAN")
                || strstr(uvalue, "DOMAINSONO")
                || strstr(uvalue, "SURDOTLY")
                || strstr(uvalue, "DOTBOT")
                || strstr(uvalue, "ALPHABOT")
                || strstr(uvalue, "AHREFSBOT")
                || strstr(uvalue, "CRAWLER")
                || 0==strncmp(uvalue, "MASSCAN", 7)
                || 0==strncmp(uvalue, "CURL", 4)
                || 0==strncmp(uvalue, "CCBOT", 5)
                || 0==strcmp(uvalue, "TELESPHOREO")
                || 0==strcmp(uvalue, "MAGIC BROWSER")) )
        {
            REQ_BOT = TRUE;
        } */

        if ( !REQ_BOT &&
                (strstr(uvalue, "BOT")
                || strstr(uvalue, "SCAN")
                || strstr(uvalue, "CRAWLER")
                || strstr(uvalue, "SURDOTLY")
                || strstr(uvalue, "BAIDU")
                || strstr(uvalue, "ZGRAB")
                || strstr(uvalue, "DOMAINSONO")
                || strstr(uvalue, "NETCRAFT")
                || 0==strncmp(uvalue, "CURL", 4)
                || 0==strncmp(uvalue, "BUBING", 6)
                || 0==strncmp(uvalue, "CLOUD MAPPING", 13)
                || 0==strcmp(uvalue, "TELESPHOREO")
                || 0==strcmp(uvalue, "MAGIC BROWSER")) )
        {
            REQ_BOT = TRUE;
        }
    }
    else if ( 0==strcmp(ulabel, "CONNECTION") )
    {
        strcpy(uvalue, upper(value));
        if ( 0==strcmp(uvalue, "KEEP-ALIVE") )
            conn[ci].keep_alive = TRUE;
    }
    else if ( 0==strcmp(ulabel, "COOKIE") )
    {
        if ( strlen(value) < SESID_LEN+3 ) return 200;  /* no valid cookie but request still OK */

        /* parse cookies, set anonymous and / or logged in sesid */

        if ( NULL != (p=(char*)strstr(value, "as=")) )  /* anonymous sesid present? */
        {
            p += 3;
            if ( strlen(p) >= SESID_LEN )
            {
                strncpy(conn[ci].cookie_in_a, p, SESID_LEN);
                conn[ci].cookie_in_a[SESID_LEN] = EOS;
            }
        }
        if ( NULL != (p=(char*)strstr(value, "ls=")) )  /* logged in sesid present? */
        {
            p += 3;
            if ( strlen(p) >= SESID_LEN )
            {
                strncpy(conn[ci].cookie_in_l, p, SESID_LEN);
                conn[ci].cookie_in_l[SESID_LEN] = EOS;
            }
        }
    }
    else if ( 0==strcmp(ulabel, "REFERER") )
    {
        strcpy(conn[ci].referer, value);
//      if ( !conn[ci].uri[0] && value[0] )
//          INF("Referer: [%s]", value);
    }
    else if ( 0==strcmp(ulabel, "X-FORWARDED-FOR") )    /* keep first IP as client IP */
    {
        len = strlen(value);
        i = 0;

        while ( i<len && (value[i]=='.' || isdigit(value[i])) && i<INET_ADDRSTRLEN )
        {
            conn[ci].ip[i] = value[i];
            ++i;
        }

        conn[ci].ip[i] = EOS;

        DEBUG("%s's value: [%s]", label, conn[ci].ip);
    }
    else if ( 0==strcmp(ulabel, "CONTENT-LENGTH") )
    {
        conn[ci].clen = atol(value);
        if ( conn[ci].clen < 0 || (!conn[ci].post && conn[ci].clen >= IN_BUFSIZE) || (conn[ci].post && conn[ci].clen >= MAX_POST_DATA_BUFSIZE) )
        {
            ERR("Request too long, clen = %ld, sending 413", conn[ci].clen);
            return 413;
        }
        DEBUG("conn[ci].clen = %ld", conn[ci].clen);
    }
    else if ( 0==strcmp(ulabel, "ACCEPT-LANGUAGE") )    /* en-US en-GB pl-PL */
    {
        i = 0;
        while ( value[i] != EOS && value[i] != ',' && value[i] != ';' && i < 7 )
        {
            conn[ci].lang[i] = value[i];
            ++i;
        }

        conn[ci].lang[i] = EOS;

        DEBUG("conn[ci].lang: [%s]", conn[ci].lang);
    }
    else if ( 0==strcmp(ulabel, "CONTENT-TYPE") )
    {
        strcpy(conn[ci].in_ctypestr, value);

        len = strlen(value);
        if ( len > 18 && 0==strncmp(value, "multipart/form-data", 19) )
        {
            conn[ci].in_ctype = CONTENT_TYPE_MULTIPART;
//          DEBUG("%s's value: [%s]", label, value);
            if ( p=(char*)strstr(value, "boundary=") )
            {
                strcpy(conn[ci].boundary, p+9);
                DEBUG("boundary: [%s]", conn[ci].boundary);
            }
        }
    }
    else if ( 0==strcmp(ulabel, "AUTHORIZATION") )
    {
        strcpy(conn[ci].authorization, value);
    }
    else if ( 0==strcmp(ulabel, "FROM") )
    {
        strcpy(uvalue, upper(value));
        if ( !REQ_BOT && (strstr(uvalue, "GOOGLEBOT") || strstr(uvalue, "BINGBOT") || strstr(uvalue, "YANDEX") || strstr(uvalue, "CRAWLER")) )
            REQ_BOT = TRUE;
    }
    else if ( 0==strcmp(ulabel, "IF-MODIFIED-SINCE") )
    {
        conn[ci].if_mod_since = time_http2epoch(value);
    }
    else if ( !conn[ci].secure && !G_test && 0==strcmp(ulabel, "UPGRADE-INSECURE-REQUESTS") && 0==strcmp(value, "1") )
    {
        DEBUG("Client wants to upgrade to HTTPS");
        conn[ci].upgrade2https = TRUE;
    }
    else if ( 0==strcmp(ulabel, "EXPECT") )
    {
        if ( 0==strcmp(value, "100-continue") )
            conn[ci].expect100 = TRUE;
    }

    return 200;
}


/* --------------------------------------------------------------------------
   Check the rules and block IP if matches
   Return TRUE if blocked
-------------------------------------------------------------------------- */
static bool check_block_ip(int ci, const char *rule, const char *value)
{
    if ( G_test ) return FALSE;     // don't block for tests

#ifdef BLACKLISTAUTOUPDATE
    if ( (rule[0]=='H' && conn[ci].post && 0==strcmp(value, APP_IP))        /* Host */
            || (rule[0]=='U' && 0==strcmp(value, "Mozilla/5.0 Jorgee"))     /* User-Agent */
            || (rule[0]=='R' && 0==strcmp(value, "wp-login.php"))           /* Resource */
            || (rule[0]=='R' && 0==strcmp(value, "administrator"))          /* Resource */
            || (rule[0]=='R' && 0==strcmp(value, "phpmyadmin"))             /* Resource */
            || (rule[0]=='R' && strstr(value, "setup.php")) )               /* Resource */
    {
        engine_block_ip(conn[ci].ip, TRUE);
        conn[ci].keep_alive = FALSE;    /* disconnect */
        return TRUE;
    }
#endif

    return FALSE;
}


/* --------------------------------------------------------------------------
  return HTTP status description
-------------------------------------------------------------------------- */
static char *get_http_descr(int status_code)
{
    int i;

    for ( i=0; M_http_status[i].status != -1; ++i )
    {
        if ( M_http_status[i].status == status_code )  /* found */
            return (char*)&(M_http_status[i].description);
    }

    return NULL;
}


/* --------------------------------------------------------------------------
   Dump counters
-------------------------------------------------------------------------- */
static void dump_counters()
{
    ALWAYS("");
    ALWAYS("Counters:\n");
    ALWAYS("       req: %ld", G_cnts_today.req);
    ALWAYS("   req_dsk: %ld", G_cnts_today.req_dsk);
    ALWAYS("   req_mob: %ld", G_cnts_today.req_mob);
    ALWAYS("   req_bot: %ld", G_cnts_today.req_bot);
    ALWAYS("    visits: %ld", G_cnts_today.visits);
    ALWAYS("visits_dsk: %ld", G_cnts_today.visits_dsk);
    ALWAYS("visits_mob: %ld", G_cnts_today.visits_mob);
    ALWAYS("   blocked: %ld", G_cnts_today.blocked);
    ALWAYS("");
}


/* --------------------------------------------------------------------------
   Clean up
-------------------------------------------------------------------------- */
static void clean_up()
{
    char    command[256];

    M_shutdown = TRUE;

    ALWAYS("");
    ALWAYS("Cleaning up...\n");
    library_log_memory();
    dump_counters();

    app_done();

    if ( access(M_pidfile, F_OK) != -1 )
    {
        DEBUG("Removing pid file...");
#ifdef _WIN32   /* Windows */
        sprintf(command, "del %s", M_pidfile);
#else
        sprintf(command, "rm %s", M_pidfile);
#endif
        system(command);
    }

#ifdef DBMYSQL
    if ( G_dbconn )
        mysql_close(G_dbconn);
#endif
#ifdef HTTPS
    SSL_CTX_free(M_ssl_ctx);
    EVP_cleanup();
#endif
#ifdef ASYNC
    if (G_queue_req)
    {
        mq_close(G_queue_req);
        mq_unlink(G_req_queue_name);
    }
    if (G_queue_res)
    {
        mq_close(G_queue_res);
        mq_unlink(G_res_queue_name);
    }
#endif

#ifdef _WIN32   /* Windows */
    WSACleanup();
#endif  /* _WIN32 */

    log_finish();
}


/* --------------------------------------------------------------------------
   Signal response
-------------------------------------------------------------------------- */
static void sigdisp(int sig)
{
    library_update_time_globals();
    ALWAYS("");
    ALWAYS_T("Exiting due to receiving signal: %d", sig);
    clean_up();
    exit(1);
}


/* --------------------------------------------------------------------------
   Generic message page
-------------------------------------------------------------------------- */
static void gen_page_msg(int ci, int msg)
{
    char    str[1024];

    DEBUG("gen_page_msg");

    if ( app_gen_page_msg(ci, msg) ) return;    /* if custom message page has been generated */

    engine_get_msg_str(ci, str, msg);

    BRING("<!DOCTYPE html>");
    BRING("<html>");
    BRING("<head>");
    BRING("<title>%s</title>", APP_WEBSITE);
    if ( REQ_MOB )  // if mobile request
        BRING("<meta name=\"viewport\" content=\"width=device-width\">");
    BRING("</head>");
    BRING("<body><p>%s</p></body>", str);
    BRING("</html>");
}


/* --------------------------------------------------------------------------
   Init SSL for a server
-------------------------------------------------------------------------- */
static bool init_ssl()
{
#ifdef HTTPS
    const SSL_METHOD *method;
    /*
       From Hynek Schlawack's blog:
       https://hynek.me/articles/hardening-your-web-servers-ssl-ciphers
       https://www.ssllabs.com/ssltest
    */
    char ciphers[256]="ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS";

    DEBUG("init_ssl");

    /* libssl init */
    SSL_library_init();
    SSL_load_error_strings();

    /* libcrypto init */
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();

    G_ssl_library_initialized = TRUE;

    method = SSLv23_server_method();    /* negotiate the highest protocol version supported by both the server and the client */
//  method = TLS_server_method();       /* negotiate the highest protocol version supported by both the server and the client */
//  method = TLSv1_2_server_method();   /* TLS v.1.2 only */

    M_ssl_ctx = SSL_CTX_new(method);    /* create new context from method */

    if ( M_ssl_ctx == NULL )
    {
        ERR("SSL_CTX_new failed");
        return FALSE;
    }

    /* support ECDH using the most appropriate shared curve */

//  if ( SSL_CTX_set_ecdh_auto(M_ssl_ctx, 1) <= 0 )     /* undefined reference?? */
/*  {
        ERR("SSL_CTX_set_ecdh_auto failed");
        return FALSE;
    } */

    const long flags = SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3;
    SSL_CTX_set_options(M_ssl_ctx, flags);

    if ( G_cipherList[0] )
        strcpy(ciphers, G_cipherList);

    ALWAYS("        Using ciphers: [%s]", ciphers);

    SSL_CTX_set_cipher_list(M_ssl_ctx, ciphers);

    /* set the local certificate */

    ALWAYS("    Using certificate: [%s]", G_certFile);

    if ( SSL_CTX_use_certificate_file(M_ssl_ctx, G_certFile, SSL_FILETYPE_PEM) <= 0 )
    {
        ERR("SSL_CTX_use_certificate_file failed");
        return FALSE;
    }

    if ( G_certChainFile[0] )   /* set the chain file */
    {
        ALWAYS("Using cert chain file: [%s]", G_certChainFile);

        if ( SSL_CTX_load_verify_locations(M_ssl_ctx, G_certChainFile, NULL) <= 0 )
        {
            ERR("SSL_CTX_load_verify_locations failed");
            return FALSE;
        }
    }

   /* set the private key from KeyFile (may be the same as CertFile) */

    ALWAYS("    Using private key: [%s]", G_keyFile);

    if ( SSL_CTX_use_PrivateKey_file(M_ssl_ctx, G_keyFile, SSL_FILETYPE_PEM) <= 0 )
    {
        ERR("SSL_CTX_use_PrivateKey_file failed");
        return FALSE;
    }

    /* verify private key */

    if ( !SSL_CTX_check_private_key(M_ssl_ctx) )
    {
        ERR("Private key does not match the public certificate");
        return FALSE;
    }
#endif
    return TRUE;
}





/* ============================================================================================================= */
/* PUBLIC ENGINE FUNCTIONS (callbacks)                                                                           */
/* ============================================================================================================= */


/* --------------------------------------------------------------------------
   Set required authorization level for the resource
-------------------------------------------------------------------------- */
void fasty_set_auth_level(const char *resource, char level)
{
static int current=0;

    if ( current > MAX_RESOURCES-2 )
        return;

    strcpy(M_auth_levels[current].resource, resource);
    M_auth_levels[current].level = level;

    strcpy(M_auth_levels[++current].resource, "-");
}


/* --------------------------------------------------------------------------
   Start new anonymous user session
-------------------------------------------------------------------------- */
bool engine_uses_start(int ci)
{
    int     i;
    char    sesid[SESID_LEN+1];

    DEBUG("engine_uses_start");

    if ( G_sessions == MAX_SESSIONS )
    {
        WAR("User sessions exhausted");
        return FALSE;
    }

    ++G_sessions;   /* start from 1 */

    /* find first free slot */

    for ( i=1; i<=MAX_SESSIONS; ++i )
    {
        if ( uses[i].sesid[0] == EOS )
        {
            conn[ci].usi = i;
            break;
        }
    }

    /* generate sesid */

    fasty_random(sesid, SESID_LEN);

    INF("Starting new session, usi=%d, sesid [%s]", conn[ci].usi, sesid);

    /* add record to uses */

    strcpy(US.sesid, sesid);
    strcpy(US.ip, conn[ci].ip);
    strcpy(US.uagent, conn[ci].uagent);
    strcpy(US.referer, conn[ci].referer);
    strcpy(US.lang, conn[ci].lang);

    library_set_datetime_formats(US.lang);

    /* custom session init */

    app_uses_init(ci);

    /* set 'as' cookie */

    strcpy(conn[ci].cookie_bring_a, sesid);

    DEBUG("%d user session(s)", G_sessions);

    return TRUE;
}


/* --------------------------------------------------------------------------
   Close user session
-------------------------------------------------------------------------- */
void engine_uses_close(int usi)
{
    engine_uses_reset(usi);
    app_uses_reset(usi);

    G_sessions--;

    DEBUG("%d session(s) remaining", G_sessions);
}


/* --------------------------------------------------------------------------
   Reset user session
-------------------------------------------------------------------------- */
void engine_uses_reset(int usi)
{
    uses[usi].logged = FALSE;
    uses[usi].uid = 0;
    uses[usi].login[0] = EOS;
    uses[usi].email[0] = EOS;
    uses[usi].name[0] = EOS;
    uses[usi].abbring[0] = EOS;
    uses[usi].login_tmp[0] = EOS;
    uses[usi].email_tmp[0] = EOS;
    uses[usi].name_tmp[0] = EOS;
    uses[usi].abbring_tmp[0] = EOS;
    uses[usi].sesid[0] = EOS;
    uses[usi].ip[0] = EOS;
    uses[usi].uagent[0] = EOS;
    uses[usi].referer[0] = EOS;
    uses[usi].lang[0] = EOS;
    uses[usi].additional[0] = EOS;
}


/* --------------------------------------------------------------------------
   Send asynchronous request
-------------------------------------------------------------------------- */
void engine_async_req(int ci, const char *helper, const char *data, char response, int timebring)
{
#ifdef ASYNC

    async_req_t req;

    if ( G_last_call_id > 10000000 ) G_last_call_id = 0;

    req.hdr.call_id = ++G_last_call_id;
    req.hdr.ci = ci;

    if ( helper )
        strcpy(req.hdr.helper, helper);
    else
        req.hdr.helper[0] = EOS;

    req.hdr.response = response;

    if ( data )
        strcpy(req.data, data);
    else
        req.data[0] = EOS;

    DEBUG("Sending a message on behalf of ci=%d, call_id=%ld, helper [%s]", ci, req.hdr.call_id, req.hdr.helper);

    mq_send(G_queue_req, (char*)&req, ASYNC_REQ_MSG_SIZE, 0);

    if ( response )     /* we will wait */
    {
        /* add to ares (async response array) */

        int j;

        for ( j=0; j<MAX_ASYNC; ++j )
        {
            if ( ares[j].hdr.state == ASYNC_STATE_FREE )        /* free slot */
            {
                DEBUG("free slot %d found in ares", j);
                ares[j].hdr.call_id = req.hdr.call_id;
                ares[j].hdr.ci = ci;
                strcpy(ares[j].hdr.helper, helper);
                ares[j].hdr.state = ASYNC_STATE_SENT;
                ares[j].hdr.sent = G_now;
                if ( timebring < 0 ) timebring = 0;
                if ( timebring == 0 || timebring > ASYNC_MAX_TIMEBRING ) timebring = ASYNC_MAX_TIMEBRING;
                ares[j].hdr.timebring = timebring;
                break;
            }
        }

        /* set request state */
#ifdef DUMP
        DEBUG("Changing state to CONN_STATE_WAITING_FOR_ASYNC");
#endif
        conn[ci].conn_state = CONN_STATE_WAITING_FOR_ASYNC;
    }
#endif
}


/* --------------------------------------------------------------------------
   Set internal (generated) static resource data & size
-------------------------------------------------------------------------- */
void fasty_add_to_static_res(const char *name, const char *src)
{
    int i;

    i = first_free_stat();

    strcpy(M_stat[i].name, name);

    M_stat[i].len = strlen(src);   /* internal are text based */

    if ( NULL == (M_stat[i].data=(char*)malloc(M_stat[i].len+1)) )
    {
        ERR("Couldn't allocate %ld bytes for %s!!!", M_stat[i].len+1, M_stat[i].name);
        return;
    }

    strcpy(M_stat[i].data, src);

    M_stat[i].type = get_res_type(M_stat[i].name);
    M_stat[i].modified = G_now;
    M_stat[i].source = STATIC_SOURCE_INTERNAL;

    INF("%s (%ld bytes)", M_stat[i].name, M_stat[i].len);
}


/* --------------------------------------------------------------------------
   Add to blocked IP
-------------------------------------------------------------------------- */
void engine_block_ip(const char *value, bool autoblocked)
{
    char    fname[512];
    char    comm[512];

    if ( G_blockedIPList[0] == EOS ) return;

    if ( G_blacklist_cnt > MAX_BLACKLIST-1 )
    {
        WAR("G_blacklist_cnt at max (%d)!", MAX_BLACKLIST);
        return;
    }

    if ( ip_blocked(value) )
    {
        DEBUG("%s already blocked", value);
        return;
    }

    strcpy(G_blacklist[G_blacklist_cnt++], value);

    if ( G_blockedIPList[0] == '/' )    /* full path */
        strcpy(fname, G_blockedIPList);
    else if ( G_appdir[0] )
        sprintf(fname, "%s/bin/%s", G_appdir, G_blockedIPList);
    else
        strcpy(fname, G_blockedIPList);

    sprintf(comm, "echo \"%s\t# %sblocked on %s\" >> %s", value, autoblocked?"auto":"", G_dt, fname);
    system(comm);

    WAR("IP %s blacklisted", value);
}


/* --------------------------------------------------------------------------
  Get error description for user.

  There are 3 groups of messages:
  < 0               -- server errors
  > 0 && < 1000     -- user library errors/messages
  >= 1000           -- app errors/messages
-------------------------------------------------------------------------- */
void engine_get_msg_str(int ci, char *dest, int errcode)
{
    if ( errcode == OK )
        strcpy(dest, "OK");
    else if ( errcode == ERR_INT_SERVER_ERROR )
//      strcpy(dest, "Apologies, this is our fault. This helper is still under intense development and the problem will probably be solved in a few hours.");
        strcpy(dest, "Apologies, this is our fault. Please try again later.");
    else if ( errcode == ERR_SERVER_TOOBUSY )
        strcpy(dest, "Apologies, we are experiencing very high demand right now, please try again in a few minutes.");
    else if ( errcode == ERR_INVALID_REQUEST )
        strcpy(dest, "Invalid HTTP request");
    else if ( errcode == ERR_NOT_FOUND )
        strcpy(dest, "The page you're trying to access does not exist here.");
    else if ( errcode == ERR_UNAUTHORIZED )
        strcpy(dest, "I'm sorry but you don't have permission to see this.");
    else if ( errcode == ERR_FILE_TOO_BIG )
        strcpy(dest, "I'm sorry but your file is too big.");
#ifdef USERS
    else if ( errcode < 1000 )
        libusr_get_msg_str(ci, dest, errcode);
#endif
    else
        app_get_msg_str(ci, dest, errcode);
}


/* --------------------------------------------------------------------------
   Return true if host matches
-------------------------------------------------------------------------- */
bool engine_host(int ci, const char *host)
{
    char uhost[64];
    char conn_uhost[64];

    strcpy(uhost, upper(host));
    strcpy(conn_uhost, upper(conn[ci].host));

    return (0==strcmp(conn_uhost, uhost));
}


/* --------------------------------------------------------------------------
   Set response status
-------------------------------------------------------------------------- */
void engine_set_res_status(int ci, int status)
{
    conn[ci].status = status;
}


/* --------------------------------------------------------------------------
   Set response content type
-------------------------------------------------------------------------- */
void engine_set_res_content_type(int ci, const char *str)
{
    conn[ci].ctype = CONTENT_TYPE_USER;
    strcpy(conn[ci].ctypestr, str);
}


/* --------------------------------------------------------------------------
   Set location
-------------------------------------------------------------------------- */
void engine_set_res_location(int ci, const char *str, ...)
{
    va_list     plist;

    va_start(plist, str);
    vsprintf(conn[ci].location, str, plist);
    va_end(plist);
}


/* --------------------------------------------------------------------------
   Set response content disposition
-------------------------------------------------------------------------- */
void engine_set_res_content_disposition(int ci, const char *str, ...)
{
    va_list     plist;

    va_start(plist, str);
    vsprintf(conn[ci].cdisp, str, plist);
    va_end(plist);
}


/* --------------------------------------------------------------------------
   Write string to bringput buffer with buffer overwrite protection
-------------------------------------------------------------------------- */
void engine_bring_check(int ci, const char *str)
{
    int available = BRING_BUFSIZE - (conn[ci].p_curr_c - conn[ci].bring_data);

    if ( strlen(str) < available )  /* the whole string will fit */
    {
        conn[ci].p_curr_c = stpcpy(conn[ci].p_curr_c, str);
    }
    else    /* let's write only what we can. WARNING: no UTF-8 checking is done here! */
    {
        conn[ci].p_curr_c = stpncpy(conn[ci].p_curr_c, str, available-1);
        *conn[ci].p_curr_c = EOS;
    }
}


/* --------------------------------------------------------------------------
   Write string to bringput buffer with buffer resizing if necessary
-------------------------------------------------------------------------- */
void engine_bring_check_realloc(int ci, const char *str)
{
    if ( strlen(str) < conn[ci].bring_data_allocated - (conn[ci].p_curr_c-conn[ci].bring_data) )    /* the whole string will fit */
    {
        conn[ci].p_curr_c = stpcpy(conn[ci].p_curr_c, str);
    }
    else    /* resize bringput buffer and try again */
    {
        long used = conn[ci].p_curr_c - conn[ci].bring_data;
        char *tmp = (char*)realloc(conn[ci].bring_data, conn[ci].bring_data_allocated*2);
        if ( !tmp )
        {
            ERR("Couldn't reallocate bringput buffer for ci=%d, tried %ld bytes", ci, conn[ci].bring_data_allocated*2);
            return;
        }
        conn[ci].bring_data = tmp;
        conn[ci].bring_data_allocated = conn[ci].bring_data_allocated * 2;
        conn[ci].p_curr_c = conn[ci].bring_data + used;
        INF("Reallocated bringput buffer for ci=%d, new size = %ld bytes", ci, conn[ci].bring_data_allocated);
        engine_bring_check_realloc(ci, str);     /* call itself! */
    }
}


/* --------------------------------------------------------------------------
   Write binary data to bringput buffer with buffer resizing if necessary
-------------------------------------------------------------------------- */
void engine_bring_check_realloc_bin(int ci, const char *data, long len)
{
    if ( len < conn[ci].bring_data_allocated - (conn[ci].p_curr_c-conn[ci].bring_data) )    /* the whole data will fit */
    {
        memcpy(conn[ci].p_curr_c, data, len);
        conn[ci].p_curr_c += len;
    }
    else    /* resize bringput buffer and try again */
    {
        long used = conn[ci].p_curr_c - conn[ci].bring_data;
        char *tmp = (char*)realloc(conn[ci].bring_data, conn[ci].bring_data_allocated*2);
        if ( !tmp )
        {
            ERR("Couldn't reallocate bringput buffer for ci=%d, tried %ld bytes", ci, conn[ci].bring_data_allocated*2);
            return;
        }
        conn[ci].bring_data = tmp;
        conn[ci].bring_data_allocated = conn[ci].bring_data_allocated * 2;
        conn[ci].p_curr_c = conn[ci].bring_data + used;
        INF("Reallocated bringput buffer for ci=%d, new size = %ld bytes", ci, conn[ci].bring_data_allocated);
        engine_bring_check_realloc_bin(ci, data, len);       /* call itself! */
    }
}


/* --------------------------------------------------------------------------
   Output standard HTML header
-------------------------------------------------------------------------- */
void engine_bring_html_header(int ci)
{
    BRING("<!DOCTYPE html>");
    BRING("<html>");
    BRING("<head>");
    BRING("<title>%s</title>", APP_WEBSITE);
#ifdef APP_DESCRIPTION
	BRING("<meta name=\"description\" content=\"%s\">", APP_DESCRIPTION);
#endif
#ifdef APP_KEYWORDS
	BRING("<meta name=\"keywords\" content=\"%s\">", APP_KEYWORDS);
#endif
    if ( REQ_MOB )  // if mobile request
        BRING("<meta name=\"viewport\" content=\"width=device-width\">");
    BRING("</head>");
    BRING("<body>");
}


/* --------------------------------------------------------------------------
   Output standard HTML footer
-------------------------------------------------------------------------- */
void engine_bring_html_footer(int ci)
{
    BRING("</body>");
    BRING("</html>");
}


/* --------------------------------------------------------------------------
   Add CSS link to HTML head
-------------------------------------------------------------------------- */
void engine_append_css(int ci, const char *fname, bool first)
{
    if ( first )
    {
        DEBUG("first = TRUE; Defining ldlink()");
        BRING("function ldlink(n){var f=document.createElement('link');f.setAttribute(\"rel\",\"stylesheet\");f.setAttribute(\"type\",\"text/css\");f.setAttribute(\"href\",n);document.getElementsByTagName(\"head\")[0].appendChild(f);}");
    }
    BRING("ldlink('%s');", fname);
}


/* --------------------------------------------------------------------------
   Add script to HTML head
-------------------------------------------------------------------------- */
void engine_append_script(int ci, const char *fname, bool first)
{
    if ( first )
    {
        DEBUG("first = TRUE; Defining ldscript()");
        BRING("function ldscript(n){var f=document.createElement('script');f.setAttribute(\"type\",\"text/javascript\");f.setAttribute(\"src\",n);document.getElementsByTagName(\"head\")[0].appendChild(f);}");
    }
    BRING("ldscript('%s');", fname);
}


/* --------------------------------------------------------------------------
   Send error description as plain, pipe-delimited text
-------------------------------------------------------------------------- */
void engine_send_ajax_msg(int ci, int errcode)
{
    char    id[4]="msg";        /* HTML id */
    char    msg[256];
    char    cat='E';            /* category = 'Error' by default */

    if ( errcode == OK )
    {
        strcpy(id, "0");
        cat = 'I';
    }
//  else if ( errcode < 0 )     /* server error */
//  {
//  }
    else if ( errcode > 0 && errcode < 20 ) /* login */
    {
        strcpy(id, "loe");
    }
    else if ( errcode < 30 )    /* email */
    {
        strcpy(id, "eme");
    }
    else if ( errcode < 40 )    /* password */
    {
        strcpy(id, "pae");
    }
    else if ( errcode < 50 )    /* repeat password */
    {
        strcpy(id, "pre");
    }
    else if ( errcode < 60 )    /* old password */
    {
        strcpy(id, "poe");
    }
//  else if ( errcode < 100 )   /* other error */
//  {
//  }
    else if ( errcode < 200 )   /* warning (yellow) */
    {
        cat = 'W';
    }
    else if ( errcode < 1000 )  /* info (green) */
    {
        cat = 'I';
    }
//  else    /* app error */
//  {
//  }

    engine_get_msg_str(ci, msg, errcode);
    BRING("%s|%s|%c", id, msg, cat);

    DEBUG("library_send_ajax_msg: [%s]", G_tmp);

    conn[ci].ctype = RES_TEXT;
    RES_DONT_CACHE;
}


/* --------------------------------------------------------------------------
   URI-decode character
-------------------------------------------------------------------------- */
static int xctod(int c)
{
    if ( isdigit(c) )
        return c - '0';
    else if ( isupper(c) )
        return c - 'A' + 10;
    else if ( islower(c) )
        return c - 'a' + 10;
    else
        return 0;
}


/* --------------------------------------------------------------------------
   URI-decode src
-------------------------------------------------------------------------- */
static char *uri_decode(char *src, int srclen, char *dest, int maxlen)
{
    char    *endp=src+srclen;
    char    *srcp;
    char    *destp=dest;
    int     nwrote=0;

    for ( srcp=src; srcp<endp; ++srcp )
    {
        if ( *srcp == '+' )
            *destp++ = ' ';
        else if ( *srcp == '%' )
        {
            *destp++ = 16 * xctod(*(srcp+1)) + xctod(*(srcp+2));
            srcp += 2;
        }
        else    /* copy as it is */
            *destp++ = *srcp;

        ++nwrote;

        if ( nwrote == maxlen )
        {
            WAR("URI val truncated");
            break;
        }
    }

    *destp = EOS;

    return dest;
}


/* --------------------------------------------------------------------------
   URI-decode src, HTML-escape
   Duplicated code for speed
-------------------------------------------------------------------------- */
static char *uri_decode_html_esc(char *src, int srclen, char *dest, int maxlen)
{
    char    *endp=src+srclen;
    char    *srcp;
    char    *destp=dest;
    int     nwrote=0;
    char    tmp;

    maxlen -= 7;

    for ( srcp=src; srcp<endp; ++srcp )
    {
        if ( *srcp == '+' )
        {
            *destp++ = ' ';
            ++nwrote;
        }
        else if ( *srcp == '%' )
        {
            tmp = 16 * xctod(*(srcp+1)) + xctod(*(srcp+2));
            srcp += 2;

            if ( tmp == '\'' )      /* single quote */
            {
                *destp++ = '&';
                *destp++ = 'a';
                *destp++ = 'p';
                *destp++ = 'o';
                *destp++ = 's';
                *destp++ = ';';
                nwrote += 6;
            }
            else if ( tmp == '"' )  /* double quote */
            {
                *destp++ = '&';
                *destp++ = 'q';
                *destp++ = 'u';
                *destp++ = 'o';
                *destp++ = 't';
                *destp++ = ';';
                nwrote += 6;
            }
            else if ( tmp == '\\' ) /* backslash */
            {
                *destp++ = '\\';
                *destp++ = '\\';
                nwrote += 2;
            }
            else if ( tmp == '<' )
            {
                *destp++ = '&';
                *destp++ = 'l';
                *destp++ = 't';
                *destp++ = ';';
                nwrote += 4;
            }
            else if ( tmp == '>' )
            {
                *destp++ = '&';
                *destp++ = 'g';
                *destp++ = 't';
                *destp++ = ';';
                nwrote += 4;
            }
            else if ( tmp == '&' )
            {
                *destp++ = '&';
                *destp++ = 'a';
                *destp++ = 'm';
                *destp++ = 'p';
                *destp++ = ';';
                nwrote += 5;
            }
            else if ( tmp != '\r' && tmp != '\n' )
            {
                *destp++ = tmp;
                ++nwrote;
            }
        }
        else if ( *srcp == '\'' )    /* ugly but fast -- everything again */
        {
            *destp++ = '&';
            *destp++ = 'a';
            *destp++ = 'p';
            *destp++ = 'o';
            *destp++ = 's';
            *destp++ = ';';
            nwrote += 6;
        }
        else if ( *srcp == '"' )    /* double quote */
        {
            *destp++ = '&';
            *destp++ = 'q';
            *destp++ = 'u';
            *destp++ = 'o';
            *destp++ = 't';
            *destp++ = ';';
            nwrote += 6;
        }
        else if ( *srcp == '\\' )   /* backslash */
        {
            *destp++ = '\\';
            *destp++ = '\\';
            nwrote += 2;
        }
        else if ( *srcp == '<' )
        {
            *destp++ = '&';
            *destp++ = 'l';
            *destp++ = 't';
            *destp++ = ';';
            nwrote += 4;
        }
        else if ( *srcp == '>' )
        {
            *destp++ = '&';
            *destp++ = 'g';
            *destp++ = 't';
            *destp++ = ';';
            nwrote += 4;
        }
        else if ( *srcp == '&' )
        {
            *destp++ = '&';
            *destp++ = 'a';
            *destp++ = 'm';
            *destp++ = 'p';
            *destp++ = ';';
            nwrote += 5;
        }
        else if ( *srcp != '\r' && *srcp != '\n' )
        {
            *destp++ = *srcp;
            ++nwrote;
        }

        if ( nwrote > maxlen )
        {
            WAR("URI val truncated");
            break;
        }
    }

    *destp = EOS;

    return dest;
}


/* --------------------------------------------------------------------------
   URI-decode src, SQL-escape
   Duplicated code for speed
-------------------------------------------------------------------------- */
static char *uri_decode_sql_esc(char *src, int srclen, char *dest, int maxlen)
{
    char    *endp=src+srclen;
    char    *srcp;
    char    *destp=dest;
    int     nwrote=0;
    char    tmp;

    maxlen -= 3;

    for ( srcp=src; srcp<endp; ++srcp )
    {
        if ( *srcp == '+' )
        {
            *destp++ = ' ';
            ++nwrote;
        }
        else if ( *srcp == '%' )
        {
            tmp = 16 * xctod(*(srcp+1)) + xctod(*(srcp+2));
            srcp += 2;

            if ( tmp == '\'' )      /* single quote */
            {
                *destp++ = '\\';
                *destp++ = '\'';
                nwrote += 2;
            }
            else if ( *srcp == '"' )    /* double quote */
            {
                *destp++ = '\\';
                *destp++ = '"';
                nwrote += 2;
            }
            else if ( tmp == '\\' )     /* backslash */
            {
                *destp++ = '\\';
                *destp++ = '\\';
                nwrote += 2;
            }
        }
        else if ( *srcp == '\'' )   /* ugly but fast -- everything again */
        {
            *destp++ = '\\';
            *destp++ = '\'';
            nwrote += 2;
        }
        else if ( *srcp == '"' )    /* double quote */
        {
            *destp++ = '\\';
            *destp++ = '"';
            nwrote += 2;
        }
        else if ( *srcp == '\\' )   /* backslash */
        {
            *destp++ = '\\';
            *destp++ = '\\';
            nwrote += 2;
        }

        if ( nwrote > maxlen )
        {
            WAR("URI val truncated");
            break;
        }
    }

    *destp = EOS;

    return dest;
}


/* --------------------------------------------------------------------------
   Get query string value and URI-decode. Return TRUE if found.
-------------------------------------------------------------------------- */
bool get_qs_param(int ci, const char *fieldname, char *retbuf)
{
static char buf[MAX_URI_VAL_LEN*2+1];

    if ( get_qs_param_raw(ci, fieldname, buf, MAX_URI_VAL_LEN*2) )
    {
        if ( retbuf ) uri_decode(buf, strlen(buf), retbuf, MAX_URI_VAL_LEN);
        return TRUE;
    }
    else if ( retbuf ) retbuf[0] = EOS;

    return FALSE;
}


/* --------------------------------------------------------------------------
   Get, URI-decode and HTML-escape query string value. Return TRUE if found.
-------------------------------------------------------------------------- */
bool get_qs_param_html_esc(int ci, const char *fieldname, char *retbuf)
{
static char buf[MAX_URI_VAL_LEN*2+1];

    if ( get_qs_param_raw(ci, fieldname, buf, MAX_URI_VAL_LEN*2) )
    {
        if ( retbuf ) uri_decode_html_esc(buf, strlen(buf), retbuf, MAX_URI_VAL_LEN);
        return TRUE;
    }
    else if ( retbuf ) retbuf[0] = EOS;

    return FALSE;
}


/* --------------------------------------------------------------------------
   Get, URI-decode and SQL-escape query string value. Return TRUE if found.
-------------------------------------------------------------------------- */
bool get_qs_param_sql_esc(int ci, const char *fieldname, char *retbuf)
{
static char buf[MAX_URI_VAL_LEN*2+1];

    if ( get_qs_param_raw(ci, fieldname, buf, MAX_URI_VAL_LEN*2) )
    {
        if ( retbuf ) uri_decode_sql_esc(buf, strlen(buf), retbuf, MAX_URI_VAL_LEN);
        return TRUE;
    }
    else if ( retbuf ) retbuf[0] = EOS;

    return FALSE;
}


/* --------------------------------------------------------------------------
   Get query string value. Return TRUE if found.
-------------------------------------------------------------------------- */
bool get_qs_param_raw(int ci, const char *fieldname, char *retbuf, int maxlen)
{
    int     fnamelen;
    char    *p, *equals, *ampersand;
    int     len1;       /* fieldname len */
    int     len2;       /* value len */
    char    *querystring;
    int     vallen;

#ifdef DUMP
    DEBUG("get_qs_param_raw, fieldname [%s], maxlen = %d", fieldname, maxlen);
#endif

    fnamelen = strlen(fieldname);

    if ( conn[ci].post )
        querystring = conn[ci].data;
    else
        querystring = strchr(conn[ci].uri, '?');

    if ( querystring == NULL )
    {
        if ( retbuf ) retbuf[0] = EOS;
        return FALSE;    /* no question mark => no values */
    }

    if ( !conn[ci].post )
        ++querystring;      /* skip the question mark */

#ifdef DUMP
    DEBUG("get_qs_param_raw before loop");
#endif

    for ( p=querystring; *p!=EOS; )
    {
        equals = strchr(p, '=');    /* end of field name */
        ampersand = strchr(p, '&');    /* end of value */

        if ( ampersand )   /* more than one field */
        {
            len2 = ampersand - p;
        }
        else    /* no ampersand ==> only one field */
        {
            if ( !equals )
                return FALSE;
            else
                len2 = strlen(p);
        }

        if ( !equals || (ampersand && equals>ampersand) )
        {
            /* no '=' present in this field, move to next */
            ampersand += len2;
            continue;
        }

        len1 = equals - p;  /* field name length */

        if ( len1 == fnamelen && strncmp(fieldname, p, len1) == 0 )
        {
            /* found it */
#ifdef DUMP
            DEBUG("get_qs_param_raw equals+1: [%s]", equals+1);
#endif
            if ( retbuf )
            {
                vallen = len2 - len1 - 1;
                if ( vallen > maxlen )
                    vallen = maxlen;

                strncpy(retbuf, equals+1, vallen);
                retbuf[vallen] = EOS;
            }

            return TRUE;
        }

        /* try next value */

        p += len2;      /* skip current value */
        if ( *p == '&' ) ++p;   /* skip & */
    }

    /* not found */

    if ( retbuf ) retbuf[0] = EOS;

#ifdef DUMP
    DEBUG("get_qs_param_raw returning FALSE");
#endif

    return FALSE;
}


/* --------------------------------------------------------------------------
   Get incoming request data -- long string version. TRUE if found.
-------------------------------------------------------------------------- */
bool get_qs_param_long(int ci, const char *fieldname, char *retbuf)
{
static char buf[MAX_LONG_URI_VAL_LEN+1];

    if ( get_qs_param_raw(ci, fieldname, buf, MAX_LONG_URI_VAL_LEN) )
    {
        uri_decode(buf, strlen(buf), retbuf, MAX_LONG_URI_VAL_LEN);
        return TRUE;
    }

    return FALSE;
}


/* --------------------------------------------------------------------------
   Get text value from multipart-form-data
-------------------------------------------------------------------------- */
bool get_qs_param_multipart_txt(int ci, const char *fieldname, char *retbuf)
{
    char    *p;
    long    len;

    p = get_qs_param_multipart(ci, fieldname, &len, NULL);

    if ( !p ) return FALSE;

    if ( len > MAX_URI_VAL_LEN ) return FALSE;

    strncpy(retbuf, p, len);
    retbuf[len] = EOS;

    return TRUE;
}


/* --------------------------------------------------------------------------
   Experimental multipart-form-data receipt
   Return length or -1 if error
   If retfname is not NULL then assume binary data and it must be the last
   data element
-------------------------------------------------------------------------- */
char *get_qs_param_multipart(int ci, const char *fieldname, long *retlen, char *retfname)
{
    int     blen;           /* boundary length */
    char    *cp;            /* current pointer */
    char    *p;             /* tmp pointer */
    long    b;              /* tmp bytes count */
    char    fn[MAX_LABEL_LEN+1];    /* field name */
    char    *end;
    long    len;

    /* Couple of checks to make sure it's properly formatted multipart content */

    if ( conn[ci].in_ctype != CONTENT_TYPE_MULTIPART )
    {
        WAR("This is not multipart/form-data");
        return NULL;
    }

    if ( conn[ci].clen < 10 )
    {
        WAR("Content length seems to be too small for multipart (%ld)", conn[ci].clen);
        return NULL;
    }

    cp = conn[ci].data;

    if ( !conn[ci].boundary[0] )    /* find first end of line -- that would be end of boundary */
    {
        if ( NULL == (p=strchr(cp, '\n')) )
        {
            WAR("Request syntax error");
            return NULL;
        }

        b = p - cp - 2;     /* skip -- */

        if ( b < 2 )
        {
            WAR("Boundary appears to be too short (%ld)", b);
            return NULL;
        }
        else if ( b > 255 )
        {
            WAR("Boundary appears to be too long (%ld)", b);
            return NULL;
        }

        strncpy(conn[ci].boundary, cp+2, b);
        if ( conn[ci].boundary[b-1] == '\r' )
            conn[ci].boundary[b-1] = EOS;
        else
            conn[ci].boundary[b] = EOS;
    }

    blen = strlen(conn[ci].boundary);

    if ( conn[ci].data[conn[ci].clen-4] != '-' || conn[ci].data[conn[ci].clen-3] != '-' )
    {
        WAR("Content doesn't end with '--'");
        return NULL;
    }

    while (TRUE)    /* find the right section */
    {
        if ( NULL == (p=strstr(cp, conn[ci].boundary)) )
        {
            WAR("No (next) boundary found");
            return NULL;
        }

        b = p - cp + blen;
        cp += b;

        if ( NULL == (p=strstr(cp, "Content-Disposition: form-data;")) )
        {
            WAR("No Content-Disposition label");
            return NULL;
        }

        b = p - cp + 30;
        cp += b;

        if ( NULL == (p=strstr(cp, "name=\"")) )
        {
            WAR("No field name");
            return NULL;
        }

        b = p - cp + 6;
        cp += b;

//      DEBUG("field name starts from: [%s]", cp);

        if ( NULL == (p=strchr(cp, '"')) )
        {
            WAR("No field name closing quote");
            return NULL;
        }

        b = p - cp;

        if ( b > MAX_LABEL_LEN )
        {
            WAR("Field name too long (%ld)", b);
            return NULL;
        }

        strncpy(fn, cp, b);
        fn[b] = EOS;

//      DEBUG("fn: [%s]", fn);

        if ( 0==strcmp(fn, fieldname) )     /* found */
            break;

        cp += b;
    }

    /* find a file name */

    if ( retfname )
    {
        if ( NULL == (p=strstr(cp, "filename=\"")) )
        {
            WAR("No file name");
            return NULL;
        }

        b = p - cp + 10;
        cp += b;

    //  DEBUG("file name starts from: [%s]", cp);

        if ( NULL == (p=strchr(cp, '"')) )
        {
            WAR("No file name closing quote");
            return NULL;
        }

        b = p - cp;

        if ( b > 255 )
        {
            WAR("File name too long (%ld)", b);
            return NULL;
        }

        strncpy(fn, cp, b);
        fn[b] = EOS;        /* fn now contains file name */

        cp += b;
    }

    /* now look for the section header end where the actual data begins */

    if ( NULL == (p=strstr(cp, "\r\n\r\n")) )
    {
        WAR("No section header end");
        return NULL;
    }

    b = p - cp + 4;
    cp += b;        /* cp now points to the actual data */

    /* find bring data length */

    if ( !retfname )    /* text */
    {
        if ( NULL == (end=strstr(cp, conn[ci].boundary)) )
        {
            WAR("No closing boundary found");
            return NULL;
        }

        len = end - cp - 4;     /* minus CRLF-- */
    }
    else    /* potentially binary content -- calculate rather than use strstr */
    {
        len = conn[ci].clen - (cp - conn[ci].data) - blen - 8;  /* fast version */
                                                                /* Note that the file content must come as last! */
    }

    if ( len < 0 )
    {
        WAR("Ooops, something went terribly wrong! Data length = %ld", len);
        return NULL;
    }

    /* everything looks good so far */

    *retlen = len;

    if ( retfname )
        strcpy(retfname, fn);

    return cp;
}


/* --------------------------------------------------------------------------
   Return request header value
-------------------------------------------------------------------------- */
char *engine_get_header(int ci, const char *header)
{
static char value[MAX_VALUE_LEN+1];
    char uheader[MAX_LABEL_LEN+1];

    strcpy(uheader, upper(header));

    if ( 0==strcmp(uheader, "CONTENT-TYPE") )
    {
        strcpy(value, conn[ci].in_ctypestr);
        return value;
    }
    else if ( 0==strcmp(uheader, "AUTHORIZATION") )
    {
        strcpy(value, conn[ci].authorization);
        return value;
    }
    else
    {
        return NULL;
    }
}


/* --------------------------------------------------------------------------
   REST call -- pass request header value from the original request
-------------------------------------------------------------------------- */
void engine_rest_header_pass(int ci, const char *key)
{
    char value[MAX_VALUE_LEN+1];

    strcpy(value, engine_get_header(ci, key));

    if ( value[0] )
        REST_HEADER_SET(key, value);
}



#else   /* ASYNC_HELPER ====================================================================================== */


char        G_req_queue_name[256];
char        G_res_queue_name[256];
mqd_t       G_queue_req;                /* request queue */
mqd_t       G_queue_res;                /* response queue */
char        *G_req;
char        *G_res;


static char *M_pidfile;                 /* pid file name */


static void sigdisp(int sig);
static void clean_up(void);


/* --------------------------------------------------------------------------
   main
-------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    char config[256];

    /* library init ------------------------------------------------------ */

    fasty_library_initialization();

    /* read the config file or set defaults ------------------------------ */

    char exec_name[256];
    library_get_execution_name(exec_name, argv[0]);

//    if ( G_appdir[0] )
//    {
//        sprintf(config, "%s/bin/%s.conf", G_appdir, exec_name);
//        if ( !library_read_conf(config) )   /* no config file there */
//        {
//            sprintf(config, "%s.conf", exec_name);
//            library_read_conf(config);
//        }
//    }
//    else    /* no FASTYDIR -- try current dir */
//    {
//        sprintf(config, "%s.conf", exec_name);
//        library_read_conf(config);
//    }

    if ( G_appdir[0] )
    {
        sprintf(config, "%s/bin/fasty.conf", G_appdir);
        if ( !library_read_conf(config) )   /* no config file there */
        {
            strcpy(config, "fasty.conf");
            library_read_conf(config);
        }
    }
    else    /* no FASTYDIR -- try current dir */
    {
        strcpy(config, "fasty.conf");
        library_read_conf(config);
    }

    /* ------------------------------------------------------------------- */

    if ( !fasty_read_param_int("logLevel", &G_logLevel) )
        G_logLevel = 3;  /* info */

    if ( !fasty_read_param_int("logToStdbring", &G_logToStdbring) )
        G_logToStdbring = 0;

    if ( !fasty_read_param_int("RESTTimebring", &G_RESTTimebring) )
        G_RESTTimebring = CALL_REST_DEFAULT_TIMEBRING;

    /* start log --------------------------------------------------------- */

    char logprefix[64];

    sprintf(logprefix, "s_%d", G_pid);

    if ( !log_start(logprefix, G_test) )
		return EXIT_FAILURE;

    /* pid file ---------------------------------------------------------- */

    if ( !(M_pidfile=library_create_pid_file(logprefix)) )
		return EXIT_FAILURE;

	/* handle signals ---------------------------------------------------- */

	signal(SIGINT,  sigdisp);	/* Ctrl-C */
	signal(SIGTERM, sigdisp);
#ifndef _WIN32
	signal(SIGQUIT, sigdisp);	/* Ctrl-\ */
	signal(SIGTSTP, sigdisp);	/* Ctrl-Z */

    signal(SIGPIPE, SIG_IGN);   /* ignore SIGPIPE */
#endif

    /* open queues ------------------------------------------------------- */

#ifdef APP_ASYNC_ID
    sprintf(G_req_queue_name, "%s_%d", ASYNC_REQ_QUEUE, APP_ASYNC_ID);
    sprintf(G_res_queue_name, "%s_%d", ASYNC_RES_QUEUE, APP_ASYNC_ID);
#else
    strcpy(G_req_queue_name, ASYNC_REQ_QUEUE);
    strcpy(G_res_queue_name, ASYNC_RES_QUEUE);
#endif

	G_queue_req = mq_open(G_req_queue_name, O_RDONLY, NULL, NULL);

	if ( G_queue_req < 0 )
	{
		ERR("mq_open for req failed, errno = %d (%s)", errno, strerror(errno));
		clean_up();
		return EXIT_FAILURE;
	}

    INF("G_queue_req open OK");

	G_queue_res = mq_open(G_res_queue_name, O_WRONLY, NULL, NULL);

	if ( G_queue_res < 0 )
	{
		ERR("mq_open for res failed, errno = %d (%s)", errno, strerror(errno));
		clean_up();
		return EXIT_FAILURE;
	}

    INF("G_queue_res open OK");

    /* ------------------------------------------------------------------- */

	if ( !helpers_initialization() )
	{
		ERR("helpers_initialization failed");
		clean_up();
		return EXIT_FAILURE;
    }

    /* ------------------------------------------------------------------- */

    async_req_t req;
    async_res_t res;

    int prev_day = G_ptm->tm_mday;

    INF("Waiting...\n");

    while (1)
    {
        if ( mq_receive(G_queue_req, (char*)&req, ASYNC_REQ_MSG_SIZE, NULL) != -1 )
        {
            library_update_time_globals();

            /* start new log file every day */

            if ( G_ptm->tm_mday != prev_day )
            {
                log_finish();

                if ( !log_start(logprefix, G_test) )
                {
                    clean_up();
                    return EXIT_FAILURE;
                }

                prev_day = G_ptm->tm_mday;
            }

            DEBUG_T("Message received");

            if ( G_logLevel > LOG_INF )
                DEBUG_T("ci = %d, helper [%s], call_id = %ld", req.hdr.ci, req.hdr.helper, req.hdr.call_id);
            else
                INF_T("%s called (id=%ld)", req.hdr.helper, req.hdr.call_id);

            res.hdr.call_id = req.hdr.call_id;
            res.hdr.ci = req.hdr.ci;
            strcpy(res.hdr.helper, req.hdr.helper);

            /* ----------------------------------------------------------- */

            DEBUG("Processing...");
            G_req = req.data;
            G_res = res.data;
            res.hdr.err_code = helper_app_process_req(req.hdr.helper, req.data);

            /* ----------------------------------------------------------- */

            if ( req.hdr.response )
            {
                DEBUG("Sending response...");
                mq_send(G_queue_res, (char*)&res, ASYNC_RES_MSG_SIZE, 0);
                DEBUG("Sent\n");
            }
            else
            {
                DEBUG("Response not required\n");
            }

            /* ----------------------------------------------------------- */

			log_flush();
        }
    }

	clean_up();

	return EXIT_SUCCESS;
}


/* --------------------------------------------------------------------------
   Signal response
-------------------------------------------------------------------------- */
static void sigdisp(int sig)
{
    library_update_time_globals();
    ALWAYS("");
    ALWAYS_T("Exiting due to receiving signal: %d", sig);
    clean_up();
    exit(1);
}


/* --------------------------------------------------------------------------
   Clean up
-------------------------------------------------------------------------- */
static void clean_up()
{
    char    command[256];

    ALWAYS("");
    ALWAYS("Cleaning up...\n");
    library_log_memory();

    helpers_done();

    if ( access(M_pidfile, F_OK) != -1 )
    {
        DEBUG("Removing pid file...");
#ifdef _WIN32   /* Windows */
        sprintf(command, "del %s", M_pidfile);
#else
        sprintf(command, "rm %s", M_pidfile);
#endif
        system(command);
    }

    if (G_queue_req)
    {
        mq_close(G_queue_req);
        mq_unlink(G_req_queue_name);
    }
    if (G_queue_res)
    {
        mq_close(G_queue_res);
        mq_unlink(G_res_queue_name);
    }

    log_finish();
}

#endif  /* ASYNC_HELPER */
