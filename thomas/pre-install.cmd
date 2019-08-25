#wget http://archive.eclipse.org/tools/ptp/builds/2.1.0/I.I200811031726/rdt-server-linux-1.0.tar
#tar xvf rdt-server-linux-1.0.tar 
#cd rdt-server/
#perl ./server.pl 1080 &

sudo apt-get update
sudo apt-get install autogen autoconf libtool
sudo apt-get install g++ make
sudo apt-get install libssl-dev
sudo apt-get install libpcre3 libpcre3-dev
sudo apt-get install libz-dev
sudo apt install openjdk-8-jre-headless

./configure --prefix=/opt/ts --enable-experimental-plugins --enable-example-plugins
