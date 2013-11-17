light-play
==========

Resource light AirTunes player for ALAC music files.

AirTunes player aimed to play music files which are directly supported by Airport Express devices. Only ALAC files are therefore supported right now. The player requires very little memory and very little CPU-resources. Player is developed for use on small platforms like OpenWrt and/or Raspberry Pi (not tested on the Raspberry Pi). A future version might have support for AirPlay (audio) as well.

See the following blog for some more information on how the resource can be kept low.
http://erikonbike.blogspot.nl

This command line player uses only very basic C libs. The only exception is the pthreads lib.
A MD5 module from Solar Designer <solar at openwall.com> is the only external code in use. See http://openwall.info/wiki/people/solar/software/public-domain-source-code/md5

All code is written by me, but some ideas have been taken from other sources like JustePort and forked-daapd. The streaming of audio to the AirPort Express without using RSA encryption for authentication, seems new. I have not found other players doing this. It will probably work on AirPlay as well (a first test shows positive results).

Have fun.
Erik
