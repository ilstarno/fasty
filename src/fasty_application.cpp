/*
 * @Author: Indrit Zeqiri 
 * @Date: 2018-11-26 15:47:58 
 * @Last Modified by:   Indrit Zeqiri 
 * @Last Modified time: 2018-11-26 15:47:58 
 */

#include <fasty.h>


/* --------------------------------------------------------------------------
   Main entry point for HTTP request
-------------------------------------------------------------------------- */
int app_process_req(int ci)
{
    int ret=OK;

    if ( REQ("") )  // landing page
    {
        BRING_HTML_HEADER;
        BRING("<h1>%s</h1>", APP_WEBSITE);
        BRING("<h2>Welcome to your superfast web application!</h2>");
        BRING("<p>Click <a href=\"welcome\">here</a> to try me.</p>");
        BRING_HTML_FOOTER;
    }
    else if ( REQ("welcome") )  // welcoming bot
    {
        BRING_HTML_HEADER;
        BRING("<h1>%s</h1>", APP_WEBSITE);

        // show form

        BRING("<p>Please enter your first name:</p>");
        BRING("<form action=\"welcome\"><input name=\"firstname\" autofocus> <input type=\"submit\" value=\"Run\"></form>");

        QSVAL qs_firstname;  // query string value

        // bid welcome

        if ( QS("firstname", qs_firstname) )  // firstname present in query string, copy it to qs_firstname
        {
            DEBUG("query string arrived with firstname %s", qs_firstname);  // this will write to the log file
            BRING("<p>Welcome %s, my dear friend!</p>", qs_firstname);
        }

        // show link to main page

        BRING("<p><a href=\"/\">Back to landing page</a></p>");

        BRING_HTML_FOOTER;
    }
    else  // page not found
    {
        ret = ERR_NOT_FOUND;  // this will return status 404 to the browser
    }

    return ret;
}


/* --------------------------------------------------------------------------
   Finish page rendering after CALL_ASYNC has returned helper response
-------------------------------------------------------------------------- */
void app_async_done(int ci, const char *helper, const char *data, int err_code)
{
}


/* --------------------------------------------------------------------------
   App custom init
   Return true if successful
-------------------------------------------------------------------------- */
bool app_initialization(int argc, char *argv[])
{
    return true;
}


/* --------------------------------------------------------------------------
   App clean-up
-------------------------------------------------------------------------- */
void app_done()
{
}


/* --------------------------------------------------------------------------
   Called when starting new anonymous user session
-------------------------------------------------------------------------- */
void app_uses_init(int ci)
{
}


/* --------------------------------------------------------------------------
   Called when starting new logged in user session
-------------------------------------------------------------------------- */
void app_luses_init(int ci)
{
}


/* --------------------------------------------------------------------------
   Called when closing user session
-------------------------------------------------------------------------- */
void app_uses_reset(int usi)
{
}


/* --------------------------------------------------------------------------
   Custom message page can be generated here.
   If returns true it means custom page has been generated,
   otherwise generic page will be displayed by the engine.
-------------------------------------------------------------------------- */
bool app_gen_page_msg(int ci, int msg)
{
    return false;   /* use engine generic page */
}


/* --------------------------------------------------------------------------
   Get error description for user
-------------------------------------------------------------------------- */
void app_get_msg_str(int ci, char *dest, int errcode)
{
}
