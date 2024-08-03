#!/bin/bash
#
# Copyright (c) 2021-2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
#
# This software product is a proprietary product of NVIDIA CORPORATION &
# AFFILIATES (the "Company") and all right, title, and interest in and to the
# software product, including all associated intellectual property rights, are
# and shall remain exclusively with the Company.
#
# This software product is governed by the End User License Agreement
# provided with the software product.
#

script_path="$0"
os_name=`cat /etc/os-release | grep -w 'NAME=' | cut -d '"' -f2 | cut -d ' ' -f1`
backup_file='/tmp/ovs_backup.txt'
bridge_name="ovsbr1"

function usage() {
        echo "Syntax: $script_path --start|-s --destroy|-d [--help|-h]"
        echo "Flag \"--start|-s\": Establishes L4 OVS firewall."
        echo "Flag \"--destroy|-d\": Reverts the OVS bridges configurations to its previous state."
}

# Delete from ovsbr1 bridge and ports from bridges that we use and backup them for future restore
function delete_prev_bridges() {
        if [ -f $backup_file ]; then
                echo "$backup_file (backup file) exists! Make sure to delete it before running the script."
                exit 0
        fi
        PF0=p0
        PF0_REP=pf0hpf

        echo "Deleting $bridge_name bridge (if exists) and deleting $PF0 and $PF0_REP from all bridges."

        ovs-vsctl list-ports $bridge_name > /dev/null 2>&1
        # If $bridge_name exists then backup it for future restore and then delete it
        if [[ $? == 0 ]]; then
                for port in `ovs-vsctl list-ports $bridge_name`; do
                        echo "$bridge_name $port" >> $backup_file
                done
        fi
        ovs-vsctl del-br $bridge_name > /dev/null 2>&1
        # Delete PF0 and PF0_REP ports from all other bridges and backup them for future restore
        for bridge in `ovs-vsctl show |  grep -oP 'Bridge \K\w[^\s]+'`; do
                ovs-vsctl del-port $bridge $PF0 > /dev/null 2>&1
                # If succeeded to delete, add to backup
                if [[ $? == 0 ]]; then
                        echo "$bridge $PF0" >> $backup_file
                fi
                ovs-vsctl del-port $bridge $PF0_REP > /dev/null 2>&1
                # If succeeded to delete, add to backup
                if [[ $? == 0 ]]; then
                        echo "$bridge $PF0_REP" >> $backup_file
                fi
        done
}

function restore_prev_bridges() {
        echo "Restoring previous OVS bridges configurations."
        if [ ! -f $backup_file ]; then
                echo "Backup file not found! Failed to restore previous OVS bridges configurations."
        fi
        ovs-vsctl del-br $bridge_name > /dev/null 2>&1
        while read var1 var2 ; do
                ovs-vsctl add-br $var1 > /dev/null 2>&1
                ovs-vsctl add-port $var1 $var2 > /dev/null 2>&1
        done < $backup_file
        rm -f $backup_file
}

if [ "$#" -ne 1 ]; then
        echo "Illegal number of arguments"
        usage
        exit 0
fi

if [[ "$1" == "--help" || "$1" == "-h" ]]; then
        usage
        exit 0
fi

if [[ "$1" != "--destroy" && "$1" != "-d" && "$1" != "--start" && "$1" != "-s" ]]; then
        echo "Illegal argument"
        usage
        exit 0
fi

# If command is destroy then revert the ovs bridges to their previous configurations
if [[ "$1" == "--destroy" || "$1" == "-d" ]]; then
        restore_prev_bridges
        exit 0
fi

# Delete and backup existing bridges and port that we use
delete_prev_bridges

# Configure the OVS bridge
ovs-vsctl add-br $bridge_name
ovs-vsctl add-port $bridge_name p0;
ovs-vsctl add-port $bridge_name pf0hpf;
# Enable hardware offload
ovs-vsctl set Open_vSwitch . other_config:hw-offload=true;
# Restart and enable openvswitch service
if [[ "$os_name" == "Ubuntu" ]]; then
        systemctl restart openvswitch-switch
elif [[ "$os_name" == "CentOS" ]]; then
        systemctl restart openvswitch
fi

ovs-vsctl show

# Configure OVS L4 firewall rules
# Lowest priority entry, if no match then drop
ovs-ofctl add-flow $bridge_name table=0,priority=1,action=drop
# Forward ARP traffic
ovs-ofctl add-flow $bridge_name table=0,priority=10,arp,action=normal
# Forward IP traffic to OVS table1 and set connection tracking
ovs-ofctl add-flow $bridge_name "table=0,priority=100,ip,ct_state=-trk,actions=ct(table=1)"
# Forward tracked IP traffic from port2 to table1
ovs-ofctl add-flow $bridge_name "table=1,in_port=2,ip,ct_state=+trk,action=ct(commit),1"
# Block IP traffic from  port1
ovs-ofctl add-flow $bridge_name "table=1,in_port=1,ip,ct_state=+trk,action=drop"



