WHOAMI=$(whoami)
if [ $WHOAMI != "root" ]
then
  echo you must use root
  exit
fi

INSTALL_DIR=/opt/ats
${INSTALL_DIR}/bin/tsxs -v -I../../lib/ -I./ -o thomas_https.so thomas_https.cc

if [ $? != 0 ]
then
  echo !!!!!!!!!!!!!!build failed!!!!!!!!!!!!!!!!!!!!!!!
  exit
fi

${INSTALL_DIR}/bin/tsxs -i -o thomas_https.so

cat /dev/null > ${INSTALL_DIR}/var/log/trafficserver/traffic.out
cat /dev/null > ${INSTALL_DIR}/var/log/trafficserver/diags.log
cat /dev/null > ${INSTALL_DIR}/var/log/trafficserver/manager.log
cat /dev/null > ${INSTALL_DIR}/var/log/trafficserver/error.log

rm -fr ${INSTALL_DIR}/var/log/trafficserver/crash*
rm -fr ${INSTALL_DIR}/var/log/trafficserver/*old

${INSTALL_DIR}/bin/trafficserver restart
${INSTALL_DIR}/bin/traffic_ctl config set proxy.config.ssl.server.cert.path etc/trafficserver/ssl
${INSTALL_DIR}/bin/traffic_ctl config set proxy.config.ssl.server.private_key.path  etc/trafficserver/ssl
${INSTALL_DIR}/bin/traffic_ctl config set proxy.config.admin.user_id "#-1"

${INSTALL_DIR}/bin/traffic_ctl config set proxy.config.url_remap.remap_required 0
${INSTALL_DIR}/bin/traffic_ctl config set proxy.config.reverse_proxy.enabled 0
${INSTALL_DIR}/bin/traffic_ctl config set proxy.config.http.server_ports "8080 443:ssl"
${INSTALL_DIR}/bin/traffic_ctl config set proxy.config.http.insert_client_ip 0
${INSTALL_DIR}/bin/traffic_ctl config set proxy.config.http.insert_squid_x_forwarded_for 0

${INSTALL_DIR}/bin/traffic_ctl config set proxy.config.diags.debug.enabled 1
${INSTALL_DIR}/bin/traffic_ctl config set proxy.config.diags.debug.tags "thomas.*"

if [ ! -d "${INSTALL_DIR}/etc/trafficserver/ssl" ]
then
  mkdir ${INSTALL_DIR}/etc/trafficserver/ssl
  chmod a+w ${INSTALL_DIR}/etc/trafficserver/ssl
fi
cp ./rootCA.* ${INSTALL_DIR}/etc/trafficserver/ssl/
cp ./create_cert.sh ${INSTALL_DIR}/etc/trafficserver/ssl/create_cert.sh
chmod a+x ${INSTALL_DIR}/etc/trafficserver/ssl/create_cert.sh
chmod a+r ${INSTALL_DIR}/etc/trafficserver/ssl/rootCA.*

if [ ! -d "${INSTALL_DIR}/etc/trafficserver/ssl/cert_cache" ]
then
  mkdir ${INSTALL_DIR}/etc/trafficserver/ssl/cert_cache
  chmod a+w ${INSTALL_DIR}/etc/trafficserver/ssl/cert_cache
fi

grep -E '^dest_ip=*'  ${INSTALL_DIR}/etc/trafficserver/ssl_multicert.config
if [ $? != 0 ]
then
  echo "dest_ip=*   ssl_cert_name=rootCA.crt  ssl_key_name=rootCA.key" >> ${INSTALL_DIR}/etc/trafficserver/ssl_multicert.config
fi

sed -i '/thomas_https.so/d' ${INSTALL_DIR}/etc/trafficserver/plugin.config
echo thomas_https.so >> ${INSTALL_DIR}/etc/trafficserver/plugin.config
cp ./create_cert.sh ${INSTALL_DIR}/etc/trafficserver/ssl/create_cert.sh

sleep 10
${INSTALL_DIR}/bin/trafficserver restart
