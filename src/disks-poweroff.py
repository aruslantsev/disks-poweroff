# Copyright (c) 2021-2022 Andrei Ruslantsev

# This program is free software: you can redistribute it and/or modify it under the terms
# of the GNU General Public License as published by the Free Software Foundation, either
# version 3 of the License, or any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
# without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with this program.
# If not, see <https://www.gnu.org/licenses/>.

import configparser
import copy
import os
import re
import subprocess
import sys
import syslog
import time


def parse_diskstats_line(line):
    """
    Parses line like foillowing
    8       0 sda 19912 11150 4603573 10996 76961 88315 4666256 72070 0 92637 83075 0 0 0 0 13 8
    ==  ===================================
     1  major number
     2  minor mumber
     3  device name
     6  sectors read
    10  sectors written
    ==  ===================================

    :return: device_name
    :return: sectors_read
    :return: sectors_written
    """

    # remove multiple spaces
    line = re.sub(' +', ' ', line).strip().split(' ')
    device_name = line[2]
    sectors_read = line[5]
    sectors_written = line[9]
    return device_name, sectors_read, sectors_written


class DisksPowerOff:
    def __init__(self, configfile):
        """Parse config"""
        syslog.openlog(ident="disks-poweroff", facility=syslog.LOG_DAEMON)

        config = configparser.ConfigParser()
        config.read(configfile)

        # Find all physical disks
        possible_devices = [dev for dev in os.listdir('/dev') if re.match("[sh]d[a-z]\Z", dev)]
        # Read disks from config. If none passed, use all
        try:
            disks = config["disks-poweroff"]["devices"].strip().split(",")
        except KeyError:
            disks = possible_devices
            syslog.syslog(syslog.LOG_WARNING,
                          "Missing 'devices' section in config. Using all possible devices")

        def normalize_disk(disk):
            disk = disk.strip()
            if "/" in disk:  # e.g. /dev/sda instead of sda
                disk = disk.split("/")[-1]
            return disk

        disks = [normalize_disk(disk) for disk in disks]
        self.disks = [disk for disk in disks if disk in possible_devices]

        syslog.syslog(syslog.LOG_INFO, f"Working with disks: {', '.join(self.disks)}")

        # If the disk is idle during timeout, we will turn it off
        timeout = config["disks-poweroff"].get("timeout", "1800")  # defaulting to 30 min
        try:
            self.timeout = int(timeout)
        except ValueError:
            syslog.syslog(
                syslog.LOG_WARNING,
                "Invalid config record for 'timeout', setting default value 1800 seconds")
            self.timeout = 1800

        # Polling interval in seconds
        polling_interval = config["disks-poweroff"].get("polling_interval", "5")  # 5 seconds
        try:
            self.polling_interval = int(polling_interval)
        except ValueError:
            syslog.syslog(
                syslog.LOG_WARNING,
                "Invalid config record for 'polling_interval', setting default value 5 seconds")
            self.polling_interval = 5

        self.diskstats = {}
        self.diskstats_prev = {}
        self.disk_statuses = {}
        self.dump_log = False

    def poll(self):
        """Checks if any bytes were read of written to disk"""
        self.diskstats_prev = copy.deepcopy(self.diskstats)
        self.diskstats = {}

        with open("/proc/diskstats", "r") as fd:
            for line in fd.readlines():
                disk, sectors_read, sectors_written = parse_diskstats_line(line)
                if disk in self.disks:
                    self.diskstats[disk] = [sectors_read, sectors_written]

    def compare(self):
        """Compare disk stats"""
        for disk in self.disks:
            # state (sectors written or sectors read) not changed and not empty
            if (
                    (self.diskstats_prev.get(disk, []) == self.diskstats.get(disk, []))
                    and (self.diskstats.get(disk, []) != [])
            ):
                # disk not in idle or poweroff state
                if (
                        (self.disk_statuses.get(disk, [None, None])[0] != "IDLE")
                        and (self.disk_statuses.get(disk, [None, None])[0] != "POWEROFF")
                ):
                    # it's time to change status and write line to log
                    self.dump_log = True
                    self.disk_statuses[disk] = ["IDLE", time.time()]
            else:
                # state changed
                if self.disk_statuses.get(disk, [None, None])[0] != "ACTIVE":
                    # if disk was not active, write info to log
                    self.dump_log = True
                # even if disk was in active state, update timer
                self.disk_statuses[disk] = ["ACTIVE", time.time()]

    def poweroff(self):
        for disk in self.disks:
            disk_status = self.disk_statuses.get(disk, ["ACTIVE", time.time()])
            if (
                    ((disk_status[0] == "IDLE") or (disk_status[0] == "POWEROFF"))
                    and (time.time() - disk_status[1] >= self.timeout)
            ):
                # Recheck if disk is sleeping every time
                smartctl = subprocess.Popen(
                    ["smartctl", "-n", "standby", f"/dev/{disk}"],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.STDOUT)
                smartctl.communicate()

                if smartctl.returncode != 2:  # sleeping
                    # WARNING: also returncode == 2 if smartctl failed
                    hdparm = subprocess.Popen(
                        ["hdparm", "-yY", f"/dev/{disk}"],
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.STDOUT)
                    hdparm.communicate()

                    if hdparm.returncode != 0:
                        syslog.syslog(syslog.LOG_ERR, f"hdparm failed for {disk}")

                if self.disk_statuses[disk][0] != "POWEROFF":
                    self.dump_log = True
                self.disk_statuses[disk][0] = "POWEROFF"

                # It is needed to repoll some disks here, because read sectors and written sectors
                # values increase after smartctl or hdparm call. My Samsung 850 EVO needs this

    def run(self):
        while True:
            self.poll()
            self.compare()
            self.poweroff()

            if self.dump_log:
                mesg = "Disks state changed: " + " ".join(
                    [f"{k}: {self.disk_statuses.get(k, [None, None])[0]}; "
                     for k in self.disk_statuses]
                )
                syslog.syslog(syslog.LOG_INFO, mesg)
                self.dump_log = False

            time.sleep(self.polling_interval)


def main():
    disks_poweroff = DisksPowerOff(sys.argv[1])
    disks_poweroff.run()


if __name__ == "__main__":
    main()
