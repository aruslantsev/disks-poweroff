# disks-poweroff

Tool for turning off HDDs if power management using hdparm is ineffective. This tool checks if 
any data have been read or written to disk and executes ```hdparm -yY $device``` for selected 
devices after timeout.
