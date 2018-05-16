#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unordered_map>

#include "ts/ts.h"
#include "ts/ink_defs.h"
#include "ts/ink_inet.h"

#define PLUGIN_NAME "thomas_remap"
#define VIA_VER "http/1.1 "


std::string keyword;
std::string hoh_server;
IpEndpoint hoh_server_addr;
IpEndpoint hohs_server_addr;
int hoh_server_port;
int hohs_server_port;
bool hoh_server_mode;
std::unordered_map<std::string, std::string> host_remap;

enum THOMAS_SCHEME{
  THOMAS_HTTP,
  THOMAS_HTTPS,
  THOMAS_FTP,
  THOMAS_FTPS,
  THOMAS_UNKONW
};

static std::string get_via_str(std::string host);
static std::string get_hostname_from_via_str(std::string via);

static void
add_host_to_via(TSMBuffer req_bufp, TSMLoc req_loc, std::string host){
  TSMLoc host_loc     = TS_NULL_MLOC;
  TSMLoc new_host_loc = TS_NULL_MLOC;

  std::string via_str = get_via_str(host);

  do {   
    if ((host_loc = TSMimeHdrFieldFind(req_bufp, req_loc, TS_MIME_FIELD_VIA, -1)) != TS_NULL_MLOC) {
      TSDebug(PLUGIN_NAME, "%s header found, destory it", TS_MIME_FIELD_VIA);
      TSMimeHdrDestroy(req_bufp, host_loc);
    } 

    // First create a new field in the client request header 
    if (TSMimeHdrFieldCreateNamed(req_bufp, req_loc, TS_MIME_FIELD_VIA, -1, &new_host_loc) != TS_SUCCESS) {
      TSError("[%s] Unable to create new field", PLUGIN_NAME);
      break;
    }


    if (TSMimeHdrFieldValueStringInsert(req_bufp, req_loc, new_host_loc, -1, via_str.c_str(), -1) != TS_SUCCESS) {
      TSError("[%s] Unable to create new field", PLUGIN_NAME);
      break;
    }
    TSDebug(PLUGIN_NAME, "set field val to: %s", via_str.c_str());

    if (TSMimeHdrFieldAppend(req_bufp, req_loc, new_host_loc) != TS_SUCCESS) {
      TSError("[%s] Unable to append new field", PLUGIN_NAME);
      break;
    }
  
  } while(0);

  TSDebug(PLUGIN_NAME, "add %s to VIA done", host.c_str());
  TSHandleMLocRelease(req_bufp, req_loc, host_loc);
  TSHandleMLocRelease(req_bufp, req_loc, new_host_loc);
  return;
}


static void
update_host_name(TSMBuffer req_bufp, TSMLoc req_loc, std::string newhost){
  TSMLoc host_loc     = TS_NULL_MLOC;
  TSMLoc new_host_loc = TS_NULL_MLOC;

  do {
    if ((host_loc = TSMimeHdrFieldFind(req_bufp, req_loc, TS_MIME_FIELD_HOST, -1)) != TS_NULL_MLOC) {
      const char* old_host;
      int host_len;
      old_host = TSMimeHdrFieldValueStringGet(req_bufp, req_loc, host_loc, -1, &host_len);
      std::string oldhost(old_host, host_len);
      std::size_t index;
      if ((index = oldhost.find_last_of(":")) != std::string::npos){
        newhost += oldhost.substr(index);
      }
      TSMimeHdrFieldValueStringSet(req_bufp, req_loc, host_loc, -1, newhost.c_str(), -1);
      TSDebug(PLUGIN_NAME, "change HOST to %s", newhost.c_str());
    } else {
      TSDebug(PLUGIN_NAME, "%s header NOT found", TS_MIME_FIELD_HOST);

      if (TSMimeHdrFieldCreateNamed(req_bufp, req_loc, TS_MIME_FIELD_HOST, -1, &new_host_loc) != TS_SUCCESS) {
        TSError("[%s] Unable to create new field", PLUGIN_NAME);
        break;
      }

      if (TSMimeHdrFieldValueStringInsert(req_bufp, req_loc, new_host_loc, -1, newhost.c_str(), -1) != TS_SUCCESS) {
        TSError("[%s] Unable to create new field", PLUGIN_NAME);
        break;
      }
      TSDebug(PLUGIN_NAME, "set field val to: %s", newhost.c_str());

      if (TSMimeHdrFieldAppend(req_bufp, req_loc, new_host_loc) != TS_SUCCESS) {
        TSError("[%s] Unable to append new field", PLUGIN_NAME);
        break;
      }
    }
  
  } while(0);

  TSHandleMLocRelease(req_bufp, req_loc, host_loc);
  TSHandleMLocRelease(req_bufp, req_loc, new_host_loc);
  return;
}


static std::string
get_host_from_via(TSMBuffer req_bufp, TSMLoc req_loc){
  TSMLoc via_loc     = TS_NULL_MLOC;

  std::string ret;
  do {
    if ((via_loc = TSMimeHdrFieldFind(req_bufp, req_loc, TS_MIME_FIELD_VIA, -1)) == TS_NULL_MLOC) {
      TSDebug(PLUGIN_NAME, "%s header NOT found", TS_MIME_FIELD_VIA);
      break;
    } 
    const char* via;
    int len;
    via = TSMimeHdrFieldValueStringGet(req_bufp, req_loc, via_loc, -1, &len);
    std::string viastr(via, len);
    ret = get_hostname_from_via_str(viastr);
    TSMimeHdrFieldDestroy(req_bufp, req_loc, via_loc);
  } while(0);

  TSHandleMLocRelease(req_bufp, req_loc, via_loc);
  TSDebug(PLUGIN_NAME, "get %s from VIA", ret.c_str());
  return ret;
}

static void
update_host_in_url(TSMBuffer req_bufp, TSMLoc req_loc, std::string newhost){
  TSMLoc url_loc = TS_NULL_MLOC;
  do {
    if (TSHttpHdrUrlGet(req_bufp, req_loc, &url_loc) != TS_SUCCESS) {
      TSError("[%s] Unable to retrieve url", PLUGIN_NAME);
      break;
    }

    std::size_t index = newhost.find(":");
    std::string host = newhost.substr(0, index);
    int port = 0;
    if (index != std::string::npos){
      port = std::stoi(newhost.substr(index + 1)); 
    } 

    const char* p_host;
    int host_len;
    if ((p_host = TSUrlHostGet(req_bufp, url_loc, &host_len)) == NULL) {
      TSDebug(PLUGIN_NAME, "host NOT found in url");
      break;
    } else {
      TSUrlHostSet(req_bufp, url_loc, host.c_str(), -1);
      TSDebug(PLUGIN_NAME, "host set to %s in URL", host.c_str());
    }
  
    if (port != 0){
      TSUrlPortSet(req_bufp, url_loc, port);
      TSDebug(PLUGIN_NAME, "port set to %d in URL", port);
    }

  } while(0);

  TSHandleMLocRelease(req_bufp, req_loc, url_loc);
  return;
}

static std::string
get_real_host(TSMBuffer req_bufp, TSMLoc req_loc){
  TSMLoc url_loc      = TS_NULL_MLOC;
  TSMLoc host_loc     = TS_NULL_MLOC;
  std::string ret;
  do {

    TSDebug(PLUGIN_NAME, "retrieve host from HOST");
    if ((host_loc = TSMimeHdrFieldFind(req_bufp, req_loc, TS_MIME_FIELD_HOST, -1)) != TS_NULL_MLOC) {
      const char* p_host;
      int host_len;
      p_host = TSMimeHdrFieldValueStringGet(req_bufp, req_loc, host_loc, -1, &host_len);
      ret = std::string(p_host, host_len);
      break;
    } 
    TSDebug(PLUGIN_NAME, "%s header NOT found", TS_MIME_FIELD_HOST);

    if (TSHttpHdrUrlGet(req_bufp, req_loc, &url_loc) == TS_SUCCESS) {
      TSDebug(PLUGIN_NAME, "retrieve host from url");
      const char* p_host;
      int host_len;
      if ((p_host = TSUrlHostGet(req_bufp, url_loc, &host_len)) == NULL) {
        TSDebug(PLUGIN_NAME, "host NOT found in url");
        break;
      } 

      ret = std::string(p_host, host_len);
      break;
    } 
  } while(0);

  TSHandleMLocRelease(req_bufp, req_loc, url_loc);
  TSHandleMLocRelease(req_bufp, req_loc, host_loc);
  return ret;
}

static std::string
get_via_str(std::string host){
  std::string ret = VIA_VER;
  ret += keyword;
  ret += host;
  return ret;
}

static std::string
get_hostname_from_via_str(std::string via){
  // http/1.1 p1, http/1.1 keyp2, http/1.0 p3
  std::string ret;
  std::string pre = VIA_VER;
  pre += keyword;
  std::size_t start = via.find(pre);
  if (start == std::string::npos){
    return ret;
  }
  ret = via.substr(start + strlen(pre.c_str()));
  std::size_t end = ret.find(",");
  return ret.substr(0, end);
}

static THOMAS_SCHEME
get_scheme(TSMBuffer req_bufp, TSMLoc req_loc){
  THOMAS_SCHEME ret = THOMAS_UNKONW;
  TSMLoc url_loc = TS_NULL_MLOC;
  do { 
    if (TSHttpHdrUrlGet(req_bufp, req_loc, &url_loc) == TS_SUCCESS) {
      TSDebug(PLUGIN_NAME, "retrieve scheme from URL");
      const char* p_scheme;
      int scheme_len;
      if ((p_scheme = TSUrlSchemeGet(req_bufp, url_loc, &scheme_len)) == NULL) {
        TSDebug(PLUGIN_NAME, "scheme NOT found in url");
        break;
      } 
      std::string sc(p_scheme, scheme_len);
      TSDebug(PLUGIN_NAME, "scheme %s", sc.c_str());
      if (strncmp(p_scheme, "http", scheme_len) == 0 && strlen("http") == scheme_len){
        ret = THOMAS_HTTP;
      } else if (strncmp(p_scheme, "https", scheme_len) == 0 && strlen("https") == scheme_len){
        ret = THOMAS_HTTPS;
      } else if (strncmp(p_scheme, "ftp", scheme_len) == 0 && strlen("ftp") == scheme_len){
        ret = THOMAS_FTP;
      } else if (strncmp(p_scheme, "ftps", scheme_len) == 0 && strlen("ftps") == scheme_len){
        ret = THOMAS_FTPS;
      }
    } 
  } while(0);
  
  TSDebug(PLUGIN_NAME, "scheme is %d", ret);

  TSHandleMLocRelease(req_bufp, req_loc, url_loc);
  return ret;
}

static std::string
get_fake_host(std::string realhost){
  // realhost can be www.dell.com:443
  // return www.taobo.com
  std::string fakehost;
  for (auto& k : host_remap){
    std::size_t index = realhost.find(k.first);
    if (index != std::string::npos){
      fakehost = realhost.replace(index, k.first.size(), k.second);
      return fakehost.substr(0, fakehost.find(":")); 
    }
  }
  fakehost = "www.taobao.com";
  return fakehost;
  
}

static void
set_server_info(TSHttpTxn txnp, TSMBuffer req_bufp, TSMLoc req_loc, THOMAS_SCHEME scheme){

  IpEndpoint* ep;
  switch(scheme){
  case THOMAS_HTTP:
    ep = &hoh_server_addr;
    break;
  case THOMAS_HTTPS:
    ep = &hohs_server_addr;
    break;
  default:
    ep = NULL;
    TSError("[%s] not supported scheme %d", PLUGIN_NAME, scheme);
    break;
  }
  
  char ipstr[128] = {0};
  TSDebug(PLUGIN_NAME, "set server ip to %s", ats_ip_nptop(ep, ipstr, 128));
  TSHttpTxnServerAddrSet(txnp, &ep->sa);
  return;
}

static void
update_header(TSHttpTxn txnp, TSCont contp ATS_UNUSED)
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
      TSDebug(PLUGIN_NAME, "ignore CONNECT request");
      break;
    }

    if (hoh_server_mode){
      // server mode. get real host from via. realhost canbe www.g.com:443
      std::string realhost = get_host_from_via(req_bufp, req_loc);
      TSDebug(PLUGIN_NAME, "server mode, realhost is %s", realhost.c_str());
      if (realhost.size() != 0){
        update_host_in_url(req_bufp, req_loc, realhost);
        update_host_name(req_bufp, req_loc, realhost);
      }
    } else {
      // client mode. add host to via. realhost canbe www.g.com:443
      std::string realhost = get_real_host(req_bufp, req_loc);
      TSDebug(PLUGIN_NAME, "client mode, realhost is %s, hohserver is %s", realhost.c_str(), hoh_server.c_str());
      add_host_to_via(req_bufp, req_loc, realhost);
      THOMAS_SCHEME scheme = get_scheme(req_bufp, req_loc);
      set_server_info(txnp, req_bufp, req_loc, scheme);
      std::string fakehost = get_fake_host(realhost); // www.b.com:443
      TSDebug(PLUGIN_NAME,"fake host is %s", fakehost.c_str());
      update_host_in_url(req_bufp, req_loc, fakehost);
      update_host_name(req_bufp, req_loc, fakehost);
    }

  } while(0);

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return;
}


static int
update_header_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    update_header(txnp, contp);
    break;
  default:
    break;
  }
  return 0;
}


static void
build_host_remap(){
  host_remap["google"] =  "baidu";
  host_remap["gmail"] = "sina";
  host_remap["youtube"] = "youku";
  host_remap["twitter"] = "weibo";
  host_remap["facebook"] = "renren";
  host_remap["cnn"] = "xinhua";
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "tzhangshare@gmail.com";
  do{
    if (TSPluginRegister(&info) != TS_SUCCESS) {
      TSError("[%s] Plugin registration failed", PLUGIN_NAME);
      break;
    }

    if (argc < 2) {
      TSError("[%s] Usage: %s keyword [http_server_ip:port,https_server_ip:port]|[null]", PLUGIN_NAME, argv[0]);
      break;
    }

    keyword = argv[1];    // somekey
    hoh_server = argv[2]; // 127.0.0.1:8080,127.0.0.1:443
    hoh_server_mode = (hoh_server[0] == '0'|| (strncasecmp(hoh_server.c_str(), "null", 4) == 0));
    
    if (!hoh_server_mode){ 
      std::string http_server;
      std::string https_server;
      std::size_t index = hoh_server.find(",");
      if (index == std::string::npos){
        TSError("[%s] Usage: para 2 format: http_server_ip:port,https_server_ip:port", PLUGIN_NAME);  
        break;
      }
      http_server = hoh_server.substr(0, index);
      https_server = hoh_server.substr(index + 1);
      
      index = http_server.find(":");
      if (index != std::string::npos){
        hoh_server_port = std::stoi(http_server.substr(index+1));
      } else {
        hoh_server_port = 8080;
      }

      index = https_server.find(":");
      if (index != std::string::npos){
        hohs_server_port = std::stoi(https_server.substr(index+1));
      } else {
        hohs_server_port = 443;
      }


      if (ats_ip_pton(http_server.c_str(), &hoh_server_addr)){
        TSError("[%s] second para should be ipv4 format", PLUGIN_NAME);
        break;
      }

      if (ats_ip_pton(https_server.c_str(), &hohs_server_addr)){
        TSError("[%s] second para should be ipv4 format", PLUGIN_NAME);
        break;
      }
    }
    build_host_remap();
    
    
    char ipstr1[128];
    char ipstr2[128];
    TSDebug(PLUGIN_NAME, "keyword is %s, parent is %s and %s", keyword.c_str(),  
                          ats_ip_nptop(&hoh_server_addr, ipstr1, 128), ats_ip_nptop(&hohs_server_addr, ipstr2, 128));

    /* Create a continuation with a mutex as there is a shared global structure
      containing the headers to add */
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(update_header_plugin, TSMutexCreate()));
    return;
  } while(0);

  TSError("[%s] Plugin not initialized", PLUGIN_NAME);
  return;
}
