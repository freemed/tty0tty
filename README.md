
# tty0tty - linux null modem emulator v1.2 


The tty0tty directory tree is divided in:

  **module** - linux kernel module null-modem  
  **pts** - null-modem using ptys (without handshake lines)


## Null modem pts (unix98): 

  When run connect two pseudo-ttys and show the connection names:
  
  (/dev/pts/1) <=> (/dev/pts/2)  

  the connection is:
  
  TX -> RX  
  RX <- TX  



## Module:

 The module is tested in kernel 3.10.2 (debian) 

  When loaded, create 8 ttys interconnected:
  
  /dev/tnt0  <=>  /dev/tnt1  
  /dev/tnt2  <=>  /dev/tnt3  
  /dev/tnt4  <=>  /dev/tnt5  
  /dev/tnt6  <=>  /dev/tnt7  

  the connection is:
  
  TX   ->  RX  
  RX   <-  TX  
  RTS  ->  CTS  
  CTS  <-  RTS  
  DSR  <-  DTR  
  CD   <-  DTR  
  DTR  ->  DSR  
  DTR  ->  CD  
  

## Requirements:

  For building the module kernel-headers or kernel source are necessary.

## Installation:

Download the tty0tty package from one of these sources:
Clone the repo https://github.com/freemed/tty0tty

```
git clone https://github.com/freemed/tty0tty
```

Build the kernel module from provided source

```
cd tty0tty-1.2/module
make
```

Install the new kernel module into the kernel modules directory

```
sudo make modules_install
```

NOTE: if module signing is enabled, in order for depmop to complete, you may
need to create file certs/x509.genkey in the kernel modules include directory
and generate file signing_key.pem using openssl, guides are available online.

Appropriate permissions are provided thanks to a udev rule located under:

```
/etc/udev/rules.d/50-tty0tty.rules
```

NOTE: you need to add yourself to the dialout group (and do a full relog), with:

```
sudo usermod -a -G dialout ${USER}
```

Load the module

```
sudo udevadm control --reload-rules
sudo depmod
sudo modprobe tty0tty
```

You should see new serial ports in ```/dev/``` (```ls /dev/tnt*```)
You can now access the serial ports as /dev/tnt0 (1,2,3,4 etc) Note that the consecutive ports are interconnected. For example, /dev/tnt0 and /dev/tnt1 are connected as if using a direct cable.

Persisting across boot:

edit the file /etc/modules (Debian) or /etc/modules.conf

```
nano /etc/modules
```
and add the following line:

```
tty0tty
```

Note that this method will not make the module persist over kernel updates so if you ever update your kernel, make sure you build tty0tty again repeat the process.

## Debian package

In order to build the dkms Debian package

```
sudo apt-get update && sudo apt-get install -y dh-make dkms build-essential
debuild -uc -us
```

## Yocto package

In order to integrate tty0tty into Yocto, please copy contents of 'yocto' folder add to your local.conf

```
IMAGE_INSTALL_append = " \
 tty0tty-module \
"
```

## Contact

For e-mail suggestions :  lcgamboa@yahoo.com
