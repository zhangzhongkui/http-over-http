#wget http://archive.eclipse.org/tools/ptp/builds/2.1.0/I.I200811031726/rdt-server-linux-1.0.tar
#tar xvf rdt-server-linux-1.0.tar 
#cd rdt-server/
#perl ./server.pl 1080 &

sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt-get update
sudo apt-get install autogen autoconf libtool
sudo apt-get install make gcc-7 g++-7
sudo apt-get install libssl-dev
sudo apt-get install libpcre3 libpcre3-dev
sudo apt-get install libz-dev
sudo apt-get install openjdk-8-jre-headless

autoreconf -if
./configure --prefix=/opt/ts --enable-experimental-plugins --enable-example-plugins
