#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <unistd.h>
#include <stdlib.h>
#include <memory>
#include <string>

#include "ts/ts.h"
#include "ts/ink_defs.h"



#define PLUGIN_NAME "thomas_https"

char* cert_dir;
int   ssl_server_port = 0;

void* my_malloc(int len){
  void* p = TSmalloc(len);
  TSDebug(PLUGIN_NAME,"my_malloc %p", p);
  return p;
}

void my_free(void* p){
  TSDebug(PLUGIN_NAME,"my_free %p", p);
  if (nullptr != p){
    TSfree(p);
  }
}

typedef std::unique_ptr<char, decltype(&my_free)>  UNIQ_PTR;

static UNIQ_PTR path_join(const char* p1, const char* p2);
static UNIQ_PTR  str_join(const char* p1, const char* p2);
static bool is_file_exist(const char* path, const char*fn, int mode);

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
is_file_exist(const char* path, const char*fn, int mode){

  UNIQ_PTR up = path_join(path, fn);

  if (access(up.get(), mode) < 0){
    TSError("[%s] access %s failed", PLUGIN_NAME, up.get());
    return false;
  }
  TSDebug(PLUGIN_NAME, "%s exist", up.get());
  return true;
}

static UNIQ_PTR
path_join(const char* p1, const char* p2){
  UNIQ_PTR part1(nullptr, my_free);
  if (p1 != nullptr && p2 != nullptr && p1[strlen(p1) - 1] != '/' ){
     part1 = str_join(p1, "/");
  } else {
     part1 = str_join(p1, nullptr);
  }
  return str_join(part1.get(), p2);
}

static UNIQ_PTR
str_join(const char* p1, const char* p2){
  int p1_len = 0;
  if (p1 != nullptr){
    p1_len = strlen(p1);
  }
  int p2_len = 0;
  if (p2 != nullptr){
    p2_len = strlen(p2);
  }

  int total_len = p1_len + p2_len + 2;

  char* fp = (char*)my_malloc(total_len);
  if (fp == NULL){
    TSError("[%s] malloc in path_join failed", PLUGIN_NAME);
    return UNIQ_PTR(nullptr, my_free);
  }
  memset(fp, 0, total_len);
  if (p1 != nullptr){
    TSstrlcat(fp, p1, total_len);
  }
  if (p2 != nullptr){
    TSstrlcat(fp, p2, total_len);
  }
  UNIQ_PTR up(fp, my_free);
  return up;
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
    
    TSDebug(PLUGIN_NAME, "HTTPS CONNECT request, modify destination");
    updateHost(req_bufp, req_loc, "127.0.0.1", ssl_server_port);

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


int
CB_Life_Cycle(TSCont, TSEvent, void *)
{
  TSDebug(PLUGIN_NAME,"CB_Life_Cycle in");
  // By now the SSL library should have been initialized,
  // We can safely parse the config file and load the ctx tables
  // Load_Configuration();


  return TS_SUCCESS;
}

int
CB_Pre_Accept(TSCont /*contp*/, TSEvent event, void *edata)
{
  TSDebug(PLUGIN_NAME,"CB_Pre_Accept in");
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);
  
  
  // All done, reactivate things
  TSVConnReenable(ssl_vc);
  return TS_SUCCESS;
}


int
CB_servername(TSCont /*contp*/, TSEvent /*event*/, void *edata)
{
  TSDebug(PLUGIN_NAME,"CB_servername in");
  TSVConn ssl_vc         = reinterpret_cast<TSVConn>(edata);

  TSSslConnection sslobj = TSVConnSSLConnectionGet(ssl_vc);
  SSL *ssl               = reinterpret_cast<SSL *>(sslobj);

  bool success = false;

  const char *server_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);

  TSDebug(PLUGIN_NAME, "SNI %s", server_name);

  do{
    if (chdir(cert_dir) < 0){
      TSError("[%s] chdir failed ", PLUGIN_NAME);
      break;
    }

    int sni_len;
    sni_len = strlen(server_name);
    if (sni_len > 1000){
      break;
    }

    UNIQ_PTR up_cache_path = path_join(cert_dir, "cert_cache");
    UNIQ_PTR up_key = path_join(up_cache_path.get(), str_join(server_name, ".key").get());
    UNIQ_PTR up_crt = path_join(up_cache_path.get(), str_join(server_name, ".crt").get());
    if (!is_file_exist(up_key.get(), nullptr, R_OK) || !is_file_exist(up_crt.get(), nullptr, R_OK)){
      UNIQ_PTR up = str_join(cert_dir, "create_cert.sh ");
      up = str_join(up.get(), server_name);
      int ret = system(up.get());
      if (ret != 0){
        TSError("[%s] create cert %s failed, ret %d ", PLUGIN_NAME, up.get(), ret);
        break;
      }
    }

    if (!is_file_exist(up_key.get(), nullptr, R_OK)){
      break;
    }

    if (!is_file_exist(up_crt.get(), nullptr, R_OK)){
      break;
    }

    /*
    SSL_CTX *ssl_ctx = SSL_CTX_new(SSLv23_client_method());

    BIO *cert_bio = BIO_new_file(up_crt.get(), "r");
    X509 *cert    = PEM_read_bio_X509_AUX(cert_bio, nullptr, nullptr, nullptr);
    BIO_free(cert_bio);
    if (SSL_CTX_use_certificate(ssl_ctx, cert) < 1) {
      TSDebug(PLUGIN_NAME, "Failed to load cert file");
      SSL_CTX_free(ssl_ctx);
      //return TS_SUCCESS;
    }
    SSL_set_SSL_CTX(ssl, ssl_ctx);
    */
    
    if (!SSL_use_certificate_file(ssl, up_crt.get(), SSL_FILETYPE_PEM)){
      TSDebug(PLUGIN_NAME, "Failed to load pub key file");
      break;
    }

    if (!SSL_use_PrivateKey_file(ssl, up_key.get(), SSL_FILETYPE_PEM)) {
      TSDebug(PLUGIN_NAME, "Failed to load priv key file");
      break;
    }

    success = true;

  } while(0);

  if (success){
    TSVConnReenable(ssl_vc);
  } else {
    TSDebug(PLUGIN_NAME, "tunnel the ssl vc");
    TSVConnTunnel(ssl_vc);
  }
  
  return TS_SUCCESS;
}

int
CB_cert(TSCont /*contp*/, TSEvent /*event*/, void *edata)
{
  TSDebug(PLUGIN_NAME,"CB_cert in");
  TSVConn ssl_vc         = reinterpret_cast<TSVConn>(edata);
  
  TSVConnReenable(ssl_vc);
  return TS_SUCCESS;
}

bool add_ssl_hooks(){

  bool success   = false;
  TSCont cb_pa   = nullptr; // pre-accept callback continuation
  TSCont cb_lc   = nullptr; // life cycle callback continuuation
  TSCont cb_sni  = nullptr; // SNI callback continuuation
  TSCont cb_cert = nullptr; // cert callback continuuation

  if (TSTrafficServerVersionGetMajor() < 5) {
    TSError(PLUGIN_NAME " requires Traffic Server 5.0 or later");
  } else if (nullptr == (cb_pa = TSContCreate(&CB_Pre_Accept, TSMutexCreate()))) {
    TSError(PLUGIN_NAME " Failed to pre-accept callback");
  } else if (nullptr == (cb_lc = TSContCreate(&CB_Life_Cycle, TSMutexCreate()))) {
    TSError(PLUGIN_NAME " Failed to lifecycle callback");
  } else if (nullptr == (cb_sni = TSContCreate(&CB_servername, TSMutexCreate()))) {
    TSError(PLUGIN_NAME " Failed to create SNI callback");
  } else if (nullptr == (cb_cert = TSContCreate(&CB_cert, TSMutexCreate()))) {
    TSError(PLUGIN_NAME " Failed to create cert callback");
  } else {
    TSLifecycleHookAdd(TS_LIFECYCLE_PORTS_INITIALIZED_HOOK, cb_lc);
    TSHttpHookAdd(TS_VCONN_START_HOOK, cb_pa);
    TSHttpHookAdd(TS_SSL_SNI_HOOK, cb_sni);
    TSHttpHookAdd(TS_SSL_CERT_HOOK, cb_cert);
    success = true;
  }
  
  if (!success) {
    if (cb_pa) {
      TSContDestroy(cb_pa);
    }
    if (cb_lc) {
      TSContDestroy(cb_lc);
    }
    if (cb_sni) {
      TSContDestroy(cb_sni);
    }
    TSError(PLUGIN_NAME " not initialized");
    return false;
  }
  TSDebug(PLUGIN_NAME, "Plugin ssl hooks %s", success ? "online" : "offline");
  return true;
}

bool add_http_hooks(){
  /* Create a continuation with a mutex as there is a shared global structure
     containing the headers to add */
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(update_header_plugin, TSMutexCreate()));
  TSDebug(PLUGIN_NAME, "Plugin http hooks online");
  return true;
}

bool check_cert_files(){
  const char* config_dir = TSConfigDirGet();

  cert_dir = TSstrdup(path_join(config_dir, "ssl/").get());
  if (cert_dir == NULL){
    return false;
  }

  if (!is_file_exist(cert_dir, "create_cert.sh", X_OK)){
    return false;
  }

  if (!is_file_exist(cert_dir, "rootCA.key", R_OK)){
    return false;
  }

  if (!is_file_exist(cert_dir, "rootCA.crt", R_OK)){
    return false;
  }

  return true;
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

  ssl_server_port = get_ssl_server_port();
  if (ssl_server_port == 0){
    TSError("[%s] not listening on ssl port ", PLUGIN_NAME);
    goto error;
  }

  if (!check_cert_files()){
    goto error;
  }
  
  if (add_ssl_hooks() && add_http_hooks()){
    TSDebug(PLUGIN_NAME," plugin is ready ");
  } else {
    goto error;
  }

  goto done;

error:
  TSError("[%s] Plugin not initialized", PLUGIN_NAME);

done:
  return;
}