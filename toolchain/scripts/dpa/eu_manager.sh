#!/bin/bash
dpaeumgmt='/opt/mellanox/doca/tools//dpaeumgmt'
dev_name='mlx5_1'
if [ $# -eq 0 ]; then
    echo "Error: No arguments provided"
    exit 1
fi

if [ "$1" == "create" ]; then
    if [ $# -ne 3 ]; then
        echo "Error: Invalid number of arguments"
        printf "Usage: $0 create <eu_id> <eu_name>\n"
        exit 1
    fi
    sudo $dpaeumgmt eu_group create -d $dev_name -n $3 -r $2
elif [ "$1" == "delete" ]; then
    if [ $# -ne 2 ]; then
        echo "Error: Invalid number of arguments"
        printf "Usage: $0 create <eu_name>\n"
        exit 1
    fi
    sudo $dpaeumgmt eu_group destroy -d $dev_name --name_group $2
elif [ "$1" == "list" ]; then
    if [ $# -ne 1 ]; then
        echo "Error: Invalid number of arguments"
        printf "Usage: $0 list\n"
        exit 1
    fi
    sudo $dpaeumgmt eu_group query -d $dev_name
else
    echo "Error: Invalid argument"
    exit 1
fi
