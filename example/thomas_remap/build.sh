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


hohserver="null"
if [ $1"" = "client" ]
then
  hohserver="10.203.108.10:8080,10.203.108.10:443"
  echo $hohserver
fi

sed -i '/thomas_remap.so/d' ${INSTALL_DIR}/etc/trafficserver/plugin.config
echo "thomas_remap.so  keyword  ${hohserver}" >> ${INSTALL_DIR}/etc/trafficserver/plugin.config

${INSTALL_DIR}/bin/trafficserver restart



