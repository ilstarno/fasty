/*
 * @Author: Indrit Zeqiri 
 * @Date: 2018-11-26 15:44:54 
 * @Last Modified by:   Indrit Zeqiri 
 * @Last Modified time: 2018-11-26 15:44:54 
 */


#include "fasty.h"


#define BUFSIZE         8196

#define REASON_CONNECT  1
#define REASON_WRITE    2
#define REASON_READ     3

#define STOP_COMMAND    "sudo $FASTYDIR/bin/fastystop"
#define START_COMMAND   "sudo $FASTYDIR/bin/fastystart"


int      G_httpPort;


char     M_watcherStopCmd[256];
char     M_watcherStartCmd[256];
int      M_watcherLogRestart;


void restart(char reason);


/* --------------------------------------------------------------------------
   main
-------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    char config[256];
    int  sockfd;
    int  conn;
    int  bytes;
    char buffer[BUFSIZE];
static struct sockaddr_in serv_addr;

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

    if ( !fasty_read_param_int("watcherLogLevel", &G_logLevel) )
        G_logLevel = 0;  /* don't create log file */

    if ( !fasty_read_param_int("watcherLogToStdbring", &G_logToStdbring) )
        G_logToStdbring = 0;

    if ( !fasty_read_param_int("httpPort", &G_httpPort) )
        G_httpPort = 80;

    if ( !fasty_read_param_str("watcherStopCmd", M_watcherStopCmd) )
        strcpy(M_watcherStopCmd, STOP_COMMAND);

    if ( !fasty_read_param_str("watcherStartCmd", M_watcherStartCmd) )
        strcpy(M_watcherStartCmd, START_COMMAND);

    if ( !fasty_read_param_int("watcherLogRestart", &M_watcherLogRestart) )
        M_watcherLogRestart = 3;

    /* start log --------------------------------------------------------- */

    if ( !log_start("watcher", FALSE) )
		return EXIT_FAILURE;

    /* ------------------------------------------------------------------- */

    INF("Trying to connect...");

    if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
    {
        ERR("socket failed, errno = %d (%s)", errno, strerror(errno));
        log_finish();
		return EXIT_FAILURE;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serv_addr.sin_port = htons(G_httpPort);

    if ( (conn=connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) < 0 )
    {
        ERR("connect failed, errno = %d (%s)", errno, strerror(errno));
        close(sockfd);
        restart(REASON_CONNECT);
        log_finish();
        return EXIT_SUCCESS;
    }

    INF("Connected");

    /* ------------------------------------------------------------------- */

    INF("Sending request...");

    char *p=buffer;     /* stpcpy is more convenient and faster than strcat */

    p = stpcpy(p, "GET / HTTP/1.1\r\n");
    p = stpcpy(p, "Host: 127.0.0.1\r\n");
    p = stpcpy(p, "User-Agent: Fasty Watcher Bot\r\n");   /* don't bother Fasty with creating a user session */
    p = stpcpy(p, "Connection: close\r\n");
    p = stpcpy(p, "\r\n");

    bytes = write(sockfd, buffer, strlen(buffer));

    if ( bytes < 18 )
    {
        ERR("write failed, errno = %d (%s)", errno, strerror(errno));
        close(conn);
        close(sockfd);
        restart(REASON_WRITE);
        log_finish();
        return EXIT_SUCCESS;
    }

    /* ------------------------------------------------------------------- */

    INF("Reading response...");

    bytes = read(sockfd, buffer, BUFSIZE);

    if ( bytes > 7 && 0==strncmp(buffer, "HTTP/1.1", 8) )
    {
        INF("Response OK");
    }
    else
    {
        ERR("read failed, errno = %d (%s)", errno, strerror(errno));
        close(conn);
        close(sockfd);
        restart(REASON_READ);
        log_finish();
        return EXIT_SUCCESS;
    }

    /* ------------------------------------------------------------------- */

    close(conn);
    close(sockfd);

    log_finish();

    return EXIT_SUCCESS;
}


/* --------------------------------------------------------------------------
   Restart
-------------------------------------------------------------------------- */
void restart(char reason)
{
    if ( M_watcherLogRestart > 0 )
    {
        G_logLevel = M_watcherLogRestart;
        log_start("watcher", FALSE);
    }

    if ( reason == REASON_CONNECT )
        ALWAYS("Couldn't connect");
    else if ( reason == REASON_WRITE )
        ALWAYS("Couldn't send the request");
    else if ( reason == REASON_READ )
        ALWAYS("Couldn't read the response");

    ALWAYS("Restarting...");

    INF("Stopping...");
    INF(M_watcherStopCmd);
    system(M_watcherStopCmd);

    INF("Waiting 1 second...");
    sleep(1);

    INF("Starting...");
    INF(M_watcherStartCmd);
    system(M_watcherStartCmd);

#ifdef APP_ADMIN_EMAIL
    if ( strlen(APP_ADMIN_EMAIL) )
        fasty_email(APP_ADMIN_EMAIL, "Fasty restart", "Fasty Watcher had to restart web server.");
#endif
}
