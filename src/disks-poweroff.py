# Copyright (c) 2021-2023 Andrei Ruslantsev

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
from typing import Tuple, Dict, Any
import time

PROGRAM_NAME = "disks-poweroff"
DEVICES = "devices"
TIMEOUT = "timeout"
POLLING_INTERVAL = "polling_interval"

DEFAULT_TIMEOUT = 1800
DEFAULT_POLLING_INTERVAL = 10

ACTIVE = "ACTIVE"
IDLE = "IDLE"
POWEROFF = "POWEROFF"


class DiskState:
    def __init__(self, state: str, timestamp: float):
        self.state = state
        self.timestamp = timestamp

    def __repr__(self) -> str:
        return f"{self.__class__.__name__}(state='{self.state}', timestamp={self.timestamp})"

    def __eq__(self, other: Any) -> bool:
        if isinstance(other, DiskState):
            return self.state == other.state
        return False


class DiskSectors:
    def __init__(self, sectors_read: str, sectors_written: str):
        self.sectors_read = sectors_read
        self.sectors_written = sectors_written

    def __repr__(self):
        return (
            f"{self.__class__.__name__}"
            + f"(sectors_read={self.sectors_read}, sectors_written={self.sectors_written})"
        )

    def __eq__(self, other: Any) -> bool:
        if isinstance(other, DiskSectors):
            return (
                (self.sectors_read == other.sectors_read)
                and (self.sectors_written == other.sectors_written)
            )

        return False


def parse_diskstats_line(line: str) -> Tuple[str, str, str]:
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


def normalize_name(disk: str) -> str:
    """Normalize disk name"""
    disk = disk.strip()
    if "/" in disk:  # e.g. /dev/sda instead of sda
        disk = disk.split("/")[-1]
    return disk


class DisksPowerOff:
    def __init__(self, configfile: str):
        """Parse config"""
        syslog.openlog(ident=PROGRAM_NAME, facility=syslog.LOG_DAEMON)
        config = configparser.ConfigParser()
        config.read(configfile)

        # Find all physical disks. Read disks from config. If none passed, use all
        possible_devices = [dev for dev in os.listdir('/dev') if re.match("[sh]d[a-z]\Z", dev)]
        try:
            disks = config[PROGRAM_NAME][DEVICES].strip().split(",")
        except KeyError:
            disks = possible_devices
            syslog.syslog(
                syslog.LOG_WARNING,
                f"Missing '{DEVICES}' section in config. Using all possible devices"
            )

        disks = [normalize_name(disk) for disk in disks]
        self.disks = [disk for disk in disks if disk in possible_devices]

        syslog.syslog(syslog.LOG_INFO, f"Working with disks: {', '.join(self.disks)}")

        # If the disk is idle during timeout, we will turn it off
        timeout = config[PROGRAM_NAME].get(TIMEOUT, str(DEFAULT_TIMEOUT))
        try:
            self.timeout = int(timeout)
        except ValueError:
            syslog.syslog(
                syslog.LOG_WARNING,
                (
                    f"Invalid config record for '{TIMEOUT}'"
                    + f"setting default value {DEFAULT_TIMEOUT} seconds"
                )
            )
            self.timeout = DEFAULT_TIMEOUT

        # Polling interval in seconds
        polling_interval = config[PROGRAM_NAME].get(
            POLLING_INTERVAL, str(DEFAULT_POLLING_INTERVAL)
        )  # 5 seconds
        try:
            self.polling_interval = int(polling_interval)
        except ValueError:
            syslog.syslog(
                syslog.LOG_WARNING,
                (
                    f"Invalid config record for '{POLLING_INTERVAL}', "
                    + f"setting default value {DEFAULT_POLLING_INTERVAL} seconds"
                )
            )
            self.polling_interval = DEFAULT_POLLING_INTERVAL

        syslog.syslog(
            syslog.LOG_INFO, 
            (
                "Running with parameters: "
                + f"timeout={self.timeout}, polling_interval={self.polling_interval}"
            )
        )

        self.diskstats: Dict[str, DiskSectors] = {}
        self.diskstats_prev: Dict[str, DiskSectors] = {}
        self.disk_states: Dict[str, DiskState] = {}

    def poll(self):
        """Checks if any bytes were read of written to disk"""
        self.diskstats_prev = copy.deepcopy(self.diskstats)
        self.diskstats = {}

        with open("/proc/diskstats", "r") as fd:
            for line in fd.readlines():
                disk, sectors_read, sectors_written = parse_diskstats_line(line)
                if disk in self.disks:
                    self.diskstats[disk] = DiskSectors(
                        sectors_read=sectors_read,
                        sectors_written=sectors_written
                    )

    def compare(self):
        """Compare disk stats"""
        for disk in self.disks:
            # stats (sectors written or sectors read) not changed and not empty
            if (
                disk in self.diskstats
                and disk in self.diskstats_prev
                and (self.diskstats_prev[disk] == self.diskstats[disk])
            ):
                # disk not in idle or poweroff state
                if (
                        (self.disk_states[disk].state != IDLE)
                        and (self.disk_states[disk].state != POWEROFF)
                ):
                    # it's time to change state
                    self.disk_states[disk] = DiskState(state=IDLE, timestamp=time.time())
                    syslog.syslog(syslog.LOG_DEBUG, f"Disk {disk} state changed to {IDLE}")
            else:
                # state changed
                if disk not in self.disk_states or self.disk_states[disk].state != ACTIVE:
                    self.disk_states[disk] = DiskState(state=ACTIVE, timestamp=time.time())
                    syslog.syslog(syslog.LOG_DEBUG, f"Disk {disk} state changed to {ACTIVE}")

    def poweroff(self):
        for disk in self.disks:
            if disk in self.disk_states:
                disk_state = self.disk_states[disk]
                if (
                    (
                        (disk_state.state == IDLE)
                        or (disk_state.state == POWEROFF)
                    )
                    and (time.time() - disk_state.timestamp >= self.timeout)
                ):
                    # Recheck if disk is sleeping every time. It may wake up unexpectedly
                    smartctl = subprocess.Popen(
                        ["smartctl", "-n", "standby", f"/dev/{disk}"],
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.STDOUT)
                    smartctl.communicate()

                    if smartctl.returncode != 2:  # not sleeping
                        # WARNING: also returncode == 2 when smartctl failed
                        hdparm = subprocess.Popen(
                            ["hdparm", "-yY", f"/dev/{disk}"],
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.STDOUT)
                        hdparm.communicate()

                        if hdparm.returncode != 0:
                            syslog.syslog(syslog.LOG_ERR, f"hdparm failed for {disk}")

                    if self.disk_states[disk].state != POWEROFF:
                        syslog.syslog(syslog.LOG_DEBUG, f"Disk {disk} state changed to {POWEROFF}")
                    self.disk_states[disk].state = POWEROFF

                    # Dome drives are need to be repolled, because number of read and written
                    # sectors increases after smartctl or hdparm call.
                    # For example, my Samsung 850 EVO needs this.

    def run(self):
        while True:
            self.poll()
            self.compare()
            self.poweroff()
            time.sleep(self.polling_interval)


def main():
    disks_poweroff = DisksPowerOff(sys.argv[1])
    disks_poweroff.run()


if __name__ == "__main__":
    main()
