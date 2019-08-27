## ATS. install package for ubuntu16
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt-get update
sudo apt-get install autogen autoconf libtool
sudo apt-get install make gcc-7 g++-7
sudo apt-get install libssl-dev
sudo apt-get install libpcre3 libpcre3-dev
sudo apt-get install libz-dev
sudo apt-get install openjdk-8-jre-headless # used for remote project

autoreconf -if
./configure --prefix=/opt/ts --enable-experimental-plugins --enable-example-plugins
sudo make
sudo make install
chmod a+w academic-proxy-serial.txt
# rm config && ln -s config config
sudo cp /opt/ts/bin/trafficserver /etc/init.d/trafficserver
sudo chmod a+x /etc/init.d/trafficserver
sudo systemctl daemon-reload
sudo systemctl restart trafficserver



## Front. danted
sudo apt-get install dante-server

rm -fr /home/ubuntu/danted.conf
cat <<EOT >> /home/ubuntu/danted.conf
internal: eth0 port = 1080
external: eth0
socksmethod: none #username #none rfc931
clientmethod: none
user.privileged: root
user.unprivileged: nobody
user.libwrap: nobody
compatibility: sameport
client pass {
        from: 0.0.0.0/0 port 1-65535 to: 0.0.0.0/0
        log: connect disconnect error
}
socks block {
        from: 0.0.0.0/0 to: lo0
        log: connect error
}
socks pass {
        from: 0.0.0.0/0 to: 0.0.0.0/0
        command: bind connect udpassociate
        log: connect error
}
EOT

sudo rm -fr /etc/danted.conf
sudo cp /home/ubuntu/danted.conf /etc/danted.conf
sudo systemctl daemon-reload
sudo systemctl restart danted


rm -fr /home/ubuntu/config.json
cat <<EOT >> /home/ubuntu/config.json
{  
  "server":"0.0.0.0",
  "server_port":1043,
  "local_address":"127.0.0.1",
  "local_port":61080,
  "password":"Baidu*ali!@#$1234",
  "timeout":600,
  "method":"camellia-256-cfb"
}
EOT
sudo rm -fr /etc/shadowsocks/config.json
sudo cp /home/ubuntu/config.json /etc/shadowsocks/config.json
sudo systemctl daemon-reload
sudo systemctl restart shadowsocks


## Front. install fake DNS
sudo apt install python
sudo git clone https://github.com/LionSec/katoolin.git && sudo cp katoolin/katoolin.py /usr/bin/katoolin
sudo chmod a+x /usr/bin/katoolin 
sudo katoolin #install kali tool repo
sudo apt-get install dnschef
sudo touch /etc/dnschef.conf
#sudo nohup dnschef --fakeip 172.31.20.77 --file /etc/dnschef.conf &
rm -fr /home/ubuntu/dnschef
cat <<EOT >> /home/ubuntu/dnschef
#! /bin/sh
### BEGIN INIT INFO
# Provides:		dnschef
# Required-Start:	$remote_fs $syslog
# Required-Stop:	$remote_fs $syslog
# Default-Start:	2 3 4 5
# Default-Stop:		
# Short-Description:	fake dns server
### END INIT INFO
set -e
case "\$1" in
  start)
    dnschef --fakeip 172.31.20.77 --file /etc/dnschef.conf& >/dev/null 2>&1
    ;;
  stop)
    kill -9 `pgrep -f dnschef`
    ;;
  reload|force-reload)
    ;;
  restart)
    ;;
  try-restart)
    ;;
  status)
    ps -ef|grep python|grep dnschef
    ;;
  *)
    exit 1
esac
exit 0
EOT
sudo cp -f /home/ubuntu/dnschef /etc/init.d/dnschef
sudo chmod a+x /etc/init.d/dnschef
sudo systemctl reset-failed
sudo systemctl daemon-reload
sudo systemctl start dnschef


## Front. point the front's dns server to fake dns
sudo vim /etc/systemd/resolved.conf # add 127.0.0.1 to the file 
sudo rm -f /etc/resolv.conf 
sudo ln -s /run/systemd/resolve/resolv.conf /etc/resolv.conf
sudo service systemd-resolved restart

## RemoteProject
wget http://archive.eclipse.org/tools/ptp/builds/2.1.0/I.I200811031726/rdt-server-linux-1.0.tar
tar xvf rdt-server-linux-1.0.tar 
cd rdt-server/
perl ./server.pl 1080 &
