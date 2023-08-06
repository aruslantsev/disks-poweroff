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
#include <filesystem>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>


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
    private:
        const int DEFAULT_TIMEOUT = 1800;
        const int DEFAULT_POLLING_INTERVAL = 10;
    public:
        int polling_interval;
        int timeout;
        std::vector<std::string> devices;

        DisksPoweroff(std::string config_path) {
            // parse config
            boost::property_tree::ptree property_tree;
            boost::property_tree::ini_parser::read_ini(config_path, property_tree);
            // get all parameters from config
            polling_interval = property_tree.get<int>("disks_poweroff.polling_interval", DEFAULT_POLLING_INTERVAL);
            timeout = property_tree.get<int>("disks_poweroff.timeout", DEFAULT_TIMEOUT);
            std::string devices_string = property_tree.get<std::string>("disks_poweroff.devices", std::string());
            std::vector<std::string> config_devices = boost::split(devices, devices_string, boost::is_any_of(","));
            // find all disks in /dev
            for (const auto & entry : std::filesystem::directory_iterator("/dev"))
                std::cout << entry.path() << std::endl;
            // intersect config devices and available disks
        };

        void parse_stats() {};

        void compare_state() {};

        void send_cmd() {};

        void run(){
            while (true) {
                parse_stats();
                compare_state();
                send_cmd();
                polling_interval = 5;
                std::this_thread::sleep_for(std::chrono::seconds(polling_interval));
            }
        };
};


int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " config_path" << std::endl;
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

class DisksPowerOff:
    def __init__(self, configfile: str):
        """Parse config"""
        syslog.openlog(ident=PROGRAM_NAME, facility=syslog.LOG_DAEMON)

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
