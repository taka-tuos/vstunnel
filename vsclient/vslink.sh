socat -d -d pty,link=/dev/vslink0,group-late=uucp,raw,echo=0 pty,link=/dev/vslink1,group-late=uucp,raw,echo=0
