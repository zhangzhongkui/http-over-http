#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "ts/ts.h"
#include "ts/ink_defs.h"
#include "ts/ink_inet.h"


#include <string>


#define PLUGIN_NAME "thomas_auth"
#define X_AUTH      "X-AUTH-THOMAS"

static int           is_server  = 0;
static char*         x_auth_val = NULL;



static bool
check_x_auth(TSMBuffer req_bufp, TSMLoc req_loc){
  
  TSMLoc x_auth_loc = TS_NULL_MLOC;
  int len;
  const char *value;
  if ((x_auth_loc = TSMimeHdrFieldFind(req_bufp, req_loc, X_AUTH, -1)) == TS_NULL_MLOC) {
    TSError("[%s] %s header not found", PLUGIN_NAME, X_AUTH);
    return false;
  } 

  TSDebug(PLUGIN_NAME, "We have %s header in request", X_AUTH);
  value = TSMimeHdrFieldValueStringGet(req_bufp, req_loc, x_auth_loc, -1, &len);
  TSDebug(PLUGIN_NAME, "Header value: %.*s", len, value);
  
  bool ret = false;
  if (strncmp(value, x_auth_val, len) == 0){
    TSDebug(PLUGIN_NAME,"auth pass");
    ret = true;
  }

  if (TSMimeHdrFieldDestroy(req_bufp, req_loc, x_auth_loc) != TS_SUCCESS) {
    TSError("[%s] failed to destory", PLUGIN_NAME);
    ret = false;
  } 
  

  TSHandleMLocRelease(req_bufp, req_loc, x_auth_loc);
  return ret;
}

static void
add_x_auth(TSMBuffer req_bufp, TSMLoc req_loc){
  TSMLoc x_auth_loc    = TS_NULL_MLOC;
  TSMLoc new_field_loc = TS_NULL_MLOC;

  do {   
    if ((x_auth_loc = TSMimeHdrFieldFind(req_bufp, req_loc, X_AUTH, -1)) != TS_NULL_MLOC) {
      TSError("[%s] %s header exist alread", PLUGIN_NAME, X_AUTH);
      break;
    } 

    // First create a new field in the client request header 
    if (TSMimeHdrFieldCreateNamed(req_bufp, req_loc, X_AUTH, -1, &new_field_loc) != TS_SUCCESS) {
      TSError("[%s] Unable to create new field", PLUGIN_NAME);
      break;
    }


    if (TSMimeHdrFieldValueStringInsert(req_bufp, req_loc, new_field_loc, -1, x_auth_val, -1) != TS_SUCCESS) {
      TSError("[%s] Unable to create new field", PLUGIN_NAME);
      break;
    }
    TSDebug(PLUGIN_NAME, "set field val to: %s", x_auth_val);

    if (TSMimeHdrFieldAppend(req_bufp, req_loc, new_field_loc) != TS_SUCCESS) {
      TSError("[%s] Unable to append new field", PLUGIN_NAME);
      break;
    }
  
  } while(0);

  TSHandleMLocRelease(req_bufp, req_loc, x_auth_loc);
  TSHandleMLocRelease(req_bufp, req_loc, new_field_loc);
  return;
}


static void
x_auth_header(TSHttpTxn txnp, TSCont contp ATS_UNUSED)
{
  TSMBuffer req_bufp;
  TSMLoc req_loc;
  do{
    if (TSHttpTxnClientReqGet(txnp, &req_bufp, &req_loc) != TS_SUCCESS) {
      TSError("[%s] Unable to retrieve client request header", PLUGIN_NAME);
      break;
    }
    int method_len = 0;
    const char* p_method;
    p_method = TSHttpHdrMethodGet(req_bufp, req_loc, &method_len);

    if (strncasecmp(p_method, "CONNECT", method_len) == 0){
      TSDebug(PLUGIN_NAME, "it is CONNECT request");
      break;
    }

    if (is_server){
      if (!check_x_auth(req_bufp, req_loc)){
        char addr[20] = {0};
        const char* as = ats_ip_ntop(TSHttpTxnClientAddrGet(txnp), addr, 20);
        TSError("[%s], auth failed IP %s", PLUGIN_NAME, as);

        TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
        return;
      }
    } else {
      add_x_auth(req_bufp, req_loc);
    }

  } while (0);

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  return;
}


static int
check_header_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    
    x_auth_header(txnp, contp);
    return 0;
  default:
    break;
  }
  return 0;
}


void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "tzhangshare@gmail.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    goto error;
  }

  if (argc < 3) {
    TSError("[%s] usage: %s mode x-auth-val", PLUGIN_NAME, argv[0]);
    goto error;
  }

  is_server = std::stoi(argv[1]);
  
  size_t encode_len;
  int rawlen;
  rawlen = (strlen(argv[2]) > 1024 ? 1024 : strlen(argv[2]));
  x_auth_val = (char*)TSmalloc(2 * rawlen);
  TSBase64Encode(argv[2], strlen(argv[2]), x_auth_val, 2*rawlen, &encode_len);
  x_auth_val[encode_len] = '\0';

  TSDebug(PLUGIN_NAME,"running mode is %s, %s", (is_server?"server":"client"), x_auth_val);
  /* Create a continuation with a mutex as there is a shared global structure
     containing the headers to add */
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(check_header_plugin, TSMutexCreate()));
  goto done;

error:
  TSError("[%s] Plugin not initialized", PLUGIN_NAME);

done:
  return;
}