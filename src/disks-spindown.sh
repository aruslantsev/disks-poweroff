#!/usr/bin/env bash
# Copyright (c) 2021-2024 Andrei Ruslantsev

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


function get_disk_stats() {
  while IFS= read -r line; do
    disk=$(echo "$line" | awk '{print $3}')
    if [ "$disk" = "$1" ]; then
      sectors_read=$(echo "$line" | awk '{print $6}')
      sectors_written=$(echo "$line" | awk '{print $10}')
      res="${disk} ${sectors_read} ${sectors_written}"
    fi
  done < /proc/diskstats
  echo $res
}


function main() {
  # Read config file and initialize variables
  source $1 2>&1 > /dev/null
  disks=${disks:-$(ls /dev/sd[a-z] | cut -d'/' -f3 | xargs)}
  timeout=${timeout:-1800}
  polling_interval=${polling_interval:-60}

  logger -p daemon.info "Starting spindown timer for disks ${disks}."
  logger -p daemon.info "Polling interval: ${polling_interval}."
  logger -p daemon.info "Timeout before spindown: ${timeout}."

  declare -A previous_stats
  declare -A current_stats
  declare -A modification_time

  # Populate stats at start
  current_scan=$(date +'%s')
  for disk in $disks; do
    current_stats["${disk}"]=$(get_disk_stats $disk)
    modification_time["${disk}"]=$current_scan
  done
  sleep 5

  while true; do
    previous_scan=$current_scan
    current_scan=$(date +'%s')
    # Recheck disks
    for disk in $disks; do
      # Save previous_stats
      previous_stats["${disk}"]=${current_stats[$disk]}
      current_stats["${disk}"]=$(get_disk_stats $disk)
      if [ "${previous_stats[$disk]}" != "${current_stats[$disk]}" ]; then
        # If data was read or written, update last active time
        modification_time["${disk}"]=$current_scan
        logger -p daemon.debug ${disk} is active
      else
        # If nothing changed, count time from last activity and compare to timeout
        timediff=$(($current_scan-modification_time["${disk}"]))
        logger -p daemon.debug ${disk} is inactive for ${timediff} seconds
        if [ $timediff -ge $timeout ]; then
          # time difference if greater than timeout. Check if disk is already sleeping
          status=$(smartctl -n standby /dev/${disk} >> /dev/null; echo $?)
          if [ $status != 2 ]; then
            # Status is not 2: disk is not sleeping
            logger -p daemon.debug ${disk} is going to sleep
            hdparm -yY /dev/${disk} > /dev/null
          else
            # Status 2: disk is sleeping or smartctl exited with error
            logger -p daemon.debug ${disk} is already sleeping
          fi
        fi
      fi
    done
    sleep $polling_interval
  done
}


if [ "$#" != 1 ]; then
  logger -t disks-spindown -p daemon.info No config file passed. Using /etc/disks-spindown.conf
  config_file=/etc/disks-spindown.conf
else
  config_file="${1}"
fi

main "$config_file"
