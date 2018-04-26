#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "ts/ts.h"
#include "ts/ink_defs.h"


#include <string>
//#include <>

#define PLUGIN_NAME "thomas_https"


static bool 
isSkypeRequest(TSHttpTxn txnp)
{
  return false;
}

static bool 
isTorRequest(TSHttpTxn txnp)
{
  return false;
}

static bool
updateHost(TSMBuffer req_bufp, TSMLoc req_loc, std::string realhost, int realport)
{
  TSMLoc url_loc = TS_NULL_MLOC;
  TSMLoc host_loc = TS_NULL_MLOC;
  do{
    // step 1:
    // update the host in URL
    if (TSHttpHdrUrlGet(req_bufp, req_loc, &url_loc) != TS_SUCCESS) {
      TSError("[%s] Unable to retrieve url", PLUGIN_NAME);
      break;
    }

    int host_len = 0;
    const char* phost;
    phost = TSUrlHostGet(req_bufp, url_loc, &host_len);
    phost = (phost == NULL ? "nullhost" : phost);
    host_len = (phost == NULL ? strlen("nullhost") : host_len);

    int port;
    port = TSUrlPortGet(req_bufp, url_loc);

    std::string urlhost(phost, host_len);
    TSDebug(PLUGIN_NAME, "%s:%d -> %s:%d in URL", 
                          urlhost.c_str(), port, realhost.c_str(), realport);

    if (TSUrlHostSet(req_bufp, url_loc, realhost.c_str(), realhost.size()) != TS_SUCCESS){
      TSError("[%s] Unable to set host", PLUGIN_NAME);
      break;
    }

    if (0 != realport){
      if (TSUrlPortSet(req_bufp, url_loc, 443) != TS_SUCCESS){
        TSError("[%s] Unable to set port", PLUGIN_NAME);
        break;
      }
    }


    // step 2: 
    // update the Host MIME filed
    
    int hostlen;
    const char *value;
    if ((host_loc = TSMimeHdrFieldFind(req_bufp, req_loc, TS_MIME_FIELD_HOST, -1)) == TS_NULL_MLOC) {
      TSError("[%s] Host header not found", PLUGIN_NAME);
      // we may create Host field here
    } else {
      TSDebug(PLUGIN_NAME, "We have \"Host\" header in request");
      value = TSMimeHdrFieldValueStringGet(req_bufp, req_loc, host_loc, -1, &hostlen);
      TSDebug(PLUGIN_NAME, "Header value: %.*s", hostlen, value);
      
      std::string newhost;
      if (realport != 0){
        newhost = realhost + ":" + std::to_string(realport);
      } else {
        std::string hosthead(value, hostlen);
        std::string port;
        if (hosthead.find(":") != std::string::npos){
          // port is ":443"
          port = hosthead.substr(hosthead.find(":"));
        }
        newhost = realhost + port;
      }

      if (TS_SUCCESS != TSMimeHdrFieldValueStringSet(req_bufp, req_loc, host_loc, 0, newhost.c_str(), newhost.size())){
        TSError("[%s] failed to set host", PLUGIN_NAME);
        break;
      }
      TSDebug(PLUGIN_NAME, "set host to %s in header", newhost.c_str());
    }
    return true;
  } while(0);

  TSHandleMLocRelease(req_bufp, req_loc, url_loc);
  TSHandleMLocRelease(req_bufp, req_loc, host_loc);
  return false;
}

static int
get_ssl_server_port(){
  TSMgmtString servers = nullptr;
  if (TSMgmtStringGet("proxy.config.http.server_ports", &servers) != TS_SUCCESS){
    return 0;
  }

  TSDebug(PLUGIN_NAME, "CONFIG proxy.config.http.server_ports STRING %s", servers);

  char* ssl = strstr(servers, ":ssl");
  if (ssl == NULL){
    TSDebug(PLUGIN_NAME, "no ssl tag");
    return 0;
  }
  char *tmp = ssl;
  while(*tmp != ' ' && tmp != servers){
    tmp--;
    TSDebug(PLUGIN_NAME, "%c", *tmp);
  }
  if (*tmp == ' '){
    tmp++;
  }
  
  int len = ssl - tmp;
  if (len == 0){
    TSDebug(PLUGIN_NAME, "no port number, %s", tmp);
    return 0;
  }

  std::string port(tmp, len);
  TSDebug(PLUGIN_NAME,"ssl port %s", port.c_str());
  return std::stoi(port);
}

static void
update_connect_header(TSHttpTxn txnp, TSCont contp ATS_UNUSED)
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

    if (strncasecmp(p_method, "CONNECT", method_len) != 0){
      TSDebug(PLUGIN_NAME, " Not a CONNECT request");
      break;
    }

    if (isSkypeRequest(txnp) || isTorRequest(txnp)){
      TSDebug(PLUGIN_NAME,"not HTTPS request");
      break;
    }
    int sslport = get_ssl_server_port();
    if (sslport == 0){
      TSDebug(PLUGIN_NAME,"not listening on ssl port");
      break;
    }
    TSDebug(PLUGIN_NAME, "HTTPS CONNECT request, modify destination");
    updateHost(req_bufp, req_loc, "127.0.0.1", sslport);

  } while (0);

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  return;
}


static int
update_header_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    
    update_connect_header(txnp, contp);
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

  if (argc > 1) {
    TSError("[%s] %s do need para ", PLUGIN_NAME, argv[0]);
    goto error;
  }

  TSDebug(PLUGIN_NAME," plugin is ready ");
  /* Create a continuation with a mutex as there is a shared global structure
     containing the headers to add */
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(update_header_plugin, TSMutexCreate()));
  goto done;

error:
  TSError("[%s] Plugin not initialized", PLUGIN_NAME);

done:
  return;
}