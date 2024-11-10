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
#include <map>
#include <string>
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <filesystem>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/process.hpp>
#include <boost/log/trivial.hpp>


const int ACTIVE = 0, IDLE = 1, POWEROFF = 2;


template <typename Iter>
std::string join(Iter begin, Iter end, std::string const& separator)
{
  std::ostringstream result;
  if (begin != end)
    result << *begin++;
  while (begin != end)
    result << separator << *begin++;
  return result.str();
};



struct DiskState {
    int state;
    std::time_t timestamp;

    DiskState() {};
    DiskState(int init_state, std::time_t init_timestamp) {
        state = init_state;
        timestamp = init_timestamp;
    };
};


bool operator==(const DiskState& lhs, const DiskState& rhs) {
    return lhs.state == rhs.state;
};


struct DiskSectors {
    std::string sectors_read;
    std::string sectors_written;

    DiskSectors () {};
    DiskSectors(std::string init_sectors_read, std::string init_sectors_written) {
        sectors_read = init_sectors_read;
        sectors_written = init_sectors_written;
    };
};


bool operator==(const DiskSectors& lhs, const DiskSectors& rhs) {
    return (lhs.sectors_read == rhs.sectors_read) & (lhs.sectors_written == rhs.sectors_written);
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
}


std::string normalize_name(std::string disk) {
    std::vector<std::string> splitted;
    boost::algorithm::trim(disk);
    boost::algorithm::to_lower(disk);
    boost::split(splitted, disk, boost::is_any_of("/"));
    return splitted.back();
}


class DisksPoweroff {
    private:
        const int DEFAULT_TIMEOUT = 1800;
        const int DEFAULT_POLLING_INTERVAL = 10;
    public:
        int polling_interval;
        int timeout;
        std::vector<std::string> devices;
        std::map<std::string, DiskSectors> diskstats, diskstats_prev;
        std::map<std::string, DiskState> disk_states;

        DisksPoweroff(std::string config_path) {
            // parse config
            boost::property_tree::ptree property_tree;
            boost::property_tree::ini_parser::read_ini(config_path, property_tree);

            // get all parameters from config
            polling_interval = property_tree.get<int>(
                "disks_poweroff.polling_interval", DEFAULT_POLLING_INTERVAL
            );
            timeout = property_tree.get<int>("disks_poweroff.timeout", DEFAULT_TIMEOUT);

            // find all disks in /dev
            std::vector<std::string> available_devices;
            for (const auto &entry: std::filesystem::directory_iterator("/dev")) {
                std::string device = entry.path();
                device = normalize_name(device);
                if (boost::regex_match(device, boost::regex("[sh]d[a-z]")))
                    available_devices.push_back(device);
            }

            std::cout 
                << "Available devices: " 
                << join(available_devices.begin(), available_devices.end(), ", ") 
                << std::endl;

            // intersect config devices and available disks
            std::vector<std::string> config_devices;
            std::string devices_string = property_tree.get<std::string>(
                "disks_poweroff.devices", std::string()
            );
            if (devices_string != "") {
                config_devices = boost::split(devices, devices_string, boost::is_any_of(","));
            } else {
                std::cout << "Devices section in config is empty" << std::endl;
                config_devices = available_devices;
            }

            std::cout 
                << "Devices in config: " 
                << join(config_devices.begin(), config_devices.end(), ", ") 
                << std::endl;

            for (const auto &element: available_devices) {
                if (
                    std::find(
                        config_devices.begin(), config_devices.end(), element
                    ) != config_devices.end()
                )
                    devices.push_back(element);
            }

            std::cout << "Starting disks_poweroff" << std::endl;
            std::cout 
                << "polling interval: " 
                << polling_interval 
                << ", timeout: " 
                << timeout 
                << std::endl;
            std::cout << "devices: " << join(devices.begin(), devices.end(), ", ") << std::endl;

            config_devices.clear();
            available_devices.clear();
            
            // delete &config_devices;
            // delete &available_devices;
        }

        void parse_stats() {
            diskstats_prev.clear();
            for (const auto &elem : diskstats) {
                diskstats_prev[elem.first] = elem.second;
            }
            diskstats.clear();

            std::string line;
            std::ifstream infile("/proc/diskstats");
            std::string device, sectors_read, sectors_written;
            while (std::getline(infile, line)) {
                std::tie(device, sectors_read, sectors_written) = parse_line(line);
                if (std::find(devices.begin(), devices.end(), device) != devices.end()) {
                    diskstats[device] = DiskSectors(sectors_read, sectors_written);
                }
            }
        }

        void compare_state() {
            for (const auto &disk : devices) {
                if (
                    (diskstats.find(disk) != diskstats.end()) 
                    && (diskstats_prev.find(disk) != diskstats_prev.end()) 
                    && (diskstats_prev[disk] == diskstats[disk])
                ) {
                    // Disk is active but nothing was read or written
                    // Skip if disk is IDLE or POWEROFF
                    if (disk_states[disk].state == ACTIVE) {
                        disk_states[disk] = DiskState(IDLE, std::time(0));
                        std::cout << "Disk " << disk << " changed state to IDLE" << std::endl;
                    }
                } else {
                    if (
                        (diskstats.find(disk) == diskstats.end()) 
                        || (disk_states[disk].state != ACTIVE)
                    ) {
                        disk_states[disk] = DiskState(ACTIVE, std::time(0));
                        std::cout << "Disk " << disk << " changed state to ACTIVE" << std::endl;
                    }
                }
            }
        }

        void send_cmd() {
            for (const auto &disk : devices) {
                if (disk_states.find(disk) != disk_states.end()) {
                    DiskState disk_state = disk_states[disk];
                    if (
                        ((disk_state.state == IDLE) || (disk_state.state == POWEROFF))
                        && ((std::time(0) - disk_state.timestamp) > timeout)
                    ) {
                        int status = boost::process::system(
                            "smartctl", "-n", "standby", "/dev/" + disk
                        );
                        if (status != 2) {
                            std::cout << "smartctl returned 2" << std::endl;
                            int hdparm_status = boost::process::system(
                                "hdparm", "-yY", "/dev/" + disk\
                            );
                            if (hdparm_status != 0)
                                std::cout << "hdparm failed for /dev/" << disk << std::endl;
                        }
                        if (disk_states[disk].state != POWEROFF) {
                            std::cout 
                                << "Disk /dev/" 
                                << disk 
                                << " changed state to POWEROFF" 
                                << std::endl;
                        }
                        disk_states[disk].state = POWEROFF;
                    }
                }
            }
        }


        void run() {
            while (true) {
                parse_stats();
                compare_state();
                send_cmd();
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
    return 0;
}


// BOOST_LOG_TRIVIAL(debug) << "A debug severity message";
// BOOST_LOG_TRIVIAL(info) << "An informational severity message";
// BOOST_LOG_TRIVIAL(warning) << "A warning severity message";
// BOOST_LOG_TRIVIAL(error) << "An error severity message";
// BOOST_LOG_TRIVIAL(fatal) << "A fatal severity message";
