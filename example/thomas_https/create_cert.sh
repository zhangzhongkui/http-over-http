#!/bin/env sh
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

[ -f ${BASEPATH}${COMMON_NAME}.crt ] && echo "exist" && exit 0

openssl genrsa -out "${BASEPATH}${COMMON_NAME}.key" 2048
openssl req -new -key "${BASEPATH}${COMMON_NAME}.key" -out "${BASEPATH}${COMMON_NAME}.csr" -subj "/C=DK/L=Aarhus/O=frogger CA/CN=${COMMON_NAME}"
openssl x509 -req -in "${BASEPATH}${COMMON_NAME}.csr" -CA ${BASEPATH}rootCA.crt -CAkey ${BASEPATH}rootCA.key -CAcreateserial -out "${BASEPATH}${COMMON_NAME}.crt"
exit $?
