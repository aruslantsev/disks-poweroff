/* 
Copyright (c) 2021-2023 Andrei Ruslantsev

This program is free software: you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation, either
version 3 of the License, or any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program.
If not, see <https://www.gnu.org/licenses/>. 
*/

#include <tuple>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>


enum states {
    ACTIVE,
    IDLE,
    POWEROFF,
};


struct DiskState {
    enum states state;
    double timestamp;
};


struct DiskSectors {
    std::string sectors_read;
    std::string sectors_written;
};


std::tuple<std::string, std::string, std::string> parse_line(std::string line) {
    /*
    Parses line and returns device, sectors read and sectors written
    8       0 sda 19912 11150 4603573 10996 76961 88315 4666256 72070 0 92637 83075 0 0 0 0 13 8
    ==  ===================================
     1  major number
     2  minor mumber
     3  device name
     6  sectors read
    10  sectors written
    ==  ===================================
    */

    std::vector<std::string> splitted;
    line = boost::regex_replace(line, boost::regex("[' ']{2,}"), " ");
    boost::algorithm::trim(line);
    boost::split(splitted, line, boost::is_any_of(" "));
    std::string &diskname = splitted[2];
    std::string &sectors_read = splitted[5];
    std::string &sectors_written = splitted[9];
    return {diskname, sectors_read, sectors_written};
};


std::string normalize_name(std::string disk) {
    std::vector<std::string> splitted;
    boost::algorithm::trim(disk);
    boost::algorithm::to_lower(disk);
    boost::split(splitted, disk, boost::is_any_of("/"));
    return splitted.back();
};


class DisksPoweroff {
    public:
        int polling_interval;
        std::vector<std::string> devices;
        DisksPoweroff(std::string config_path)
        {
        };
        void parse_stats(){};
        void compare_state(){};
        void send_cmd(){};
        void run(){
            while (true) {
                parse_stats();
                compare_state();
                send_cmd();
                polling_interval = 5;
                std::cout << "Cycle\n";
                std::this_thread::sleep_for(std::chrono::seconds(polling_interval));
            }
        };
};


int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " config_path" << "\n";
        return 1;
    }
    std::string disk, written, read;

    DisksPoweroff disks_poweroff(argv[1]);
    disks_poweroff.run();

    tie(disk, read, written) = parse_line(" 253       0 dm-0 4427735 0 764012960 1975224 10010485 0 1190249536 120592768 0 7406036 122645676 136166 0 460220616 77684 0 0");
    std::cout << disk << " " << read << " " <<  written << "\n";
    std::cout << normalize_name("/dev/SDA ") << "\n";
    return 0;
}



/*
PROGRAM_NAME = "disks-poweroff"
DEVICES = "devices"
TIMEOUT = "timeout"
POLLING_INTERVAL = "polling_interval"

DEFAULT_TIMEOUT = 1800
DEFAULT_POLLING_INTERVAL = 10



class DisksPowerOff:
    def __init__(self, configfile: str):
        """Parse config"""
        syslog.openlog(ident=PROGRAM_NAME, facility=syslog.LOG_DAEMON)
        config = configparser.ConfigParser()
        config.read(configfile)

        # Find all physical disks. Read disks from config. If none passed, use all
        possible_devices = [dev for dev in os.listdir('/dev') if re.match(r"[sh]d[a-z]\Z", dev)]
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

*/
