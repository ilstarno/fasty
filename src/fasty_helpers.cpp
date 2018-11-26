/*
 * @Author: Indrit Zeqiri 
 * @Date: 2018-11-26 15:47:41 
 * @Last Modified by:   Indrit Zeqiri 
 * @Last Modified time: 2018-11-26 15:47:41 
 */


#include <fasty.h>



/* --------------------------------------------------------------------------
   Helper
-------------------------------------------------------------------------- */
int getCustomer(const char *req)
{
    BRING("Hello from getCustomer helper!");
    return OK;
}


/* --------------------------------------------------------------------------
   Helper
-------------------------------------------------------------------------- */
int getAccounts(const char *req)
{
    BRING("Hello from getAccounts helper!");
    return OK;
}


/* ======================================================================= */
/* ========================== END OF HELPERS ============================ */
/* ======================================================================= */



/* --------------------------------------------------------------------------
   Entry point
-------------------------------------------------------------------------- */
int helper_app_process_req(const char *helper, const char *req)
{
    if ( S("getCustomer") )
        return getCustomer(req);
    else if ( S("getAccounts") )
        return getAccounts(req);

    return ERR_ASYNC_NO_SUCH_HELPER;
}


/* --------------------------------------------------------------------------
   Server start
   Return true if successful
-------------------------------------------------------------------------- */
bool helpers_initialization()
{
    return true;
}


/* --------------------------------------------------------------------------
   Server stop
-------------------------------------------------------------------------- */
void helpers_done()
{
}
