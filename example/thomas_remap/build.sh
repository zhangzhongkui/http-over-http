WHOAMI=$(whoami)
if [ $WHOAMI != "root" ]
then
  echo you must use root
  exit
fi

INSTALL_DIR=/opt/ats
${INSTALL_DIR}/bin/tsxs -v -I../../lib/ -I./ -o thomas_remap.so thomas_remap.cc

if [ $? != 0 ]
then
  echo !!!!!!!!!!!!!!build failed!!!!!!!!!!!!!!!!!!!!!!!
  exit
fi

${INSTALL_DIR}/bin/tsxs -i -o thomas_remap.so
cat /dev/null > ${INSTALL_DIR}/var/log/trafficserver/traffic.out
cat /dev/null > ${INSTALL_DIR}/var/log/trafficserver/diags.log
cat /dev/null > ${INSTALL_DIR}/var/log/trafficserver/manager.log
cat /dev/null > ${INSTALL_DIR}/var/log/trafficserver/error.log

rm -fr ${INSTALL_DIR}/var/log/trafficserver/crash*
rm -fr ${INSTALL_DIR}/var/log/trafficserver/*old

${INSTALL_DIR}/bin/trafficserver restart
#${INSTALL_DIR}/bin/traffic_ctl config set proxy.config.ssl.server.cert.path etc/trafficserver/ssl
