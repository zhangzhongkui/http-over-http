#!/bin/sh
#openssl genrsa -out rootCA.key 2048
#openssl req -new -x509 -key rootCA.key -out rootCA.crt -subj '/C=DK/L=Aarhus/O=frogger CA/CN=thomas.com' -days 999
COMMON_NAME=$1
BASEPATH=$2
if [ ${COMMON_NAME}"" = "" ]
then
    echo COMMON_NAME is empty
    exit -1
fi

if [ ${BASEPATH}"" = "" ]
then
    BASEPATH="./"
fi
cd $BASEPATH
CACHE_DIR=${BASEPATH}cert_cache/

#[ -f ${CACHE_DIR}${COMMON_NAME}.crt ] && [ -f ${CACHE_DIR}${COMMON_NAME}.key ] echo "exist" && exit 0

CACHE_SIZE=`du ${CACHE_DIR}| awk '{print $1}'`
if [[ ${CACHE_SIZE} > 10000 ]]
then
  cd ${CACHE_DIR}
  ls -lt |tail -600 |xargs rm -f
fi

openssl genrsa -out "${CACHE_DIR}${COMMON_NAME}.key" 2048
openssl req -new -key "${CACHE_DIR}${COMMON_NAME}.key" -out "${CACHE_DIR}${COMMON_NAME}.csr" -subj "/C=DK/L=Aarhus/O=frogger CA/CN=${COMMON_NAME}"
openssl x509 -req -in "${CACHE_DIR}${COMMON_NAME}.csr" -CA ${BASEPATH}rootCA.crt -CAkey ${BASEPATH}rootCA.key -CAcreateserial -out "${CACHE_DIR}${COMMON_NAME}.crt" -days 365
rm -f ${CACHE_DIR}${COMMON_NAME}.csr
exit $?
