light-play
==========

Resource light AirTunes player for streaming ALAC music files to Airport Express devices.

What is it?
-----------
Light-play is currently a command line tool for sending your ALAC music files to an AirPort Express (or compatible?) device. It uses very little resources, hence its name. It should evolve into a light weight server to stream audio to AirPort Express and/or Apple TV devices. Only audio formats directly supported by the devices will be supported in light-play (ie no encoding/transcoding of audio). The server will have a web user interface to select audio and possibly use and maintain playlists.

Because of the resource friendly nature, it is suited well for small devices like routers running OpenWrt or Raspberry Pi's. (Only tested on OpenWrt for now)

How to use it
-------------
Light-play is a command line tool. The following command line arguments are valid:
	    Usage: light-play [-?hcpvlo] <url> <filename>
	    
	    -? | -h          Print this usage message
	    -c[ ]<password>  Set password for using AirPort Express
	    -p[ ]<portname>  Set name/number of AirTunes port (default: 5000)
	    -v[e|w|i|d]      Set logging verbosity (default: w)
				 e: only errors
				 w: errors and warnings (default)
				 i: errors, warnings and info
				 d: all (includes debug info)
	    -l[ ]<filename>  Set logging to specified file
	    -o[ ]<offset>    Set offset (in seconds) from begin of file where to start playing

If you encounter a problem, please use -vd and check the resulting log. Adding debug information to the log will give very detailed description of both the m4a file parsing as well as the communication with the Airport Express device.

At the moment only a single file can be played per invocation of the application. See below for an explanation of light-play's future functionality.

What will/can it become?
------------------------
The next release is going to turn the player into a server which can send files (consecutively) to the Airport Express. A web based user interface will allow the user to select the files to play. Next feature will probably be the usage of iTunes playlists. So if you store your iTunes music library on a NAS, it can be served by light-play. After that, support for Apple TV is foreseen.

How does it work?
-----------------
Light-play streams m4a file content directly to the AirPort Express device. Absolutely no decoding/transcoding of the audio content is taking place. Furthermore, since Airport devices (and Apple TV devices most probably as well) do NOT require any encryption of the audio-data (in contrast to popular usage), streaming is not much more than putting the file content in a network packet (with a small header prefixed). For light-play to know where to find the actual audio content within a m4a file, the file is parsed first. During this parsing the location of the audio content is retrieved as well as some necessary information with respect to duration of the file and timescale.

Requirements
------------
Light-play should compile and run on most Linux systems.  The light-play application uses standard C libraries, the Linux pthread library and a MD5 implementation from Alexander Peslyak (aka Solar Designer). It will also compile and run on Mac OS X (with the right tools installed, like XCode command line tools and/or gcc compiler through MacPorts).

The runtime requirements are very low. The CPU usage on a NETGEAR WNDR3700 (Atheros AR7161, 680Mhz processor, with 64Mb RAM) is around 1% when the router is furthermore mostly idle. Memory is only allocated for the largest packet size in the audio file and is used (consecutively) for all packages. So no huge amounts of memory allocated. This last is useful since ALAC files can become fairly large (considering the usage on small devices).

Why not handle more audio formats?
----------------------------------
At first only ALAC will be supported, since the 'old' Airport Express devices only support this. Later on, support for AAC might be added for use with Apple TV devices.

The philosophy behind light-play is that it is light because no encoding/transcoding is performed on the audio content. Adding support for more audio formats would require encoding/transcoding. For such situation plenty of other audio players already exist.

More info
---------
Some more info might be found at http://ErikOnBike.blogspot.nl
