lcdpulse
========

lcdpulse is a program that shows the volume of the default pulseaudio sink on lcdproc.

Installing
==========

Install the libpulse development files, on most debian distributions this is:<br>
```$sudo apt-get install libpulse-dev```

check out the repository:<br>
```$git clone https://github.com/bobo1on1/lcdpulse.git```
  
This will create a new directory called lcdpulse, to compile:<br>
```
$cd lcdpulse
$./waf configure
$./waf
```
  
To install on your system:<br>
```$./waf install```
  
Using
=====

To start lcdpulse, simply enter the lcdpulse command in a terminal without arguments:<br>
```
$lcdpulse
Pulse: Connecting
Pulse: Authorizing
Pulse: Setting name
Pulse: Ready
default sink is "jack_out"
LCDProc: Connected to "localhost":13666
Pulse: volume: 49%
LCDProc read: connect LCDproc 0.5.5 protocol 0.3 lcd wid 16 hgt 2 cellwid 5 cellhgt 8
```

It will print whatever it reads from lcdproc, and it will show messages about the pulseaudio volume and default sink.<br>
When it connects to lcdproc, it will create a screen with background priority,
when the volume is changed, it will change the priority of this screen to alert for 2 seconds.<br>

lcdpulse will try to connect to LCDd running on localhost by default,
if you want to connect to LCDd running on a different host or port,
you can specify it on the command line:<br>
```
  $lcdpulse somemachine.mynetwork.org
  $lcdpulse localhost:1337
```
  
Also it supports ipv6 addresses, but you have to add brackets if you want to specify the port:<br>
```
  $lcdpulse 1111:2222:3333:4444:5555:6666:7777:8888
  $lcdpulse [aaaa:bbbb:cccc:dddd:eeee:ffff:1111:2222]:12345
```
  
If you want to daemonize lcdpulse, you can pass the --fork argument, it will then fork a child process
and close stdout and stderr:<br>
```
  $lcdpulse --fork
```
