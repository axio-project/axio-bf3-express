#ifndef QPINFO_H
#define QPINFO_H

#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>

const size_t MAX_HOSTNAME_LEN = 64;
const size_t MAX_NIC_NAME_LEN = 64;

class QPInfo {
   public:
    uint32_t qp_num;                  // Queue Pair Number
    uint16_t lid;                     // Local Identifier (LID)
    uint8_t gid[16];                  // Global Identifier (GID)
    uint8_t gid_table_index;          // GID Table Index
    uint32_t mtu;                     // Maximum Transmission Unit (MTU)
    uint8_t mac_addr[6];              // MAC Address
    char hostname[MAX_HOSTNAME_LEN];  // Hostname
    char nic_name[MAX_NIC_NAME_LEN];  // Network Interface Name (e.g., rdma0)
    bool is_initialized;              // Initialization status

    // Constructor
    QPInfo(uint32_t qp_num = 0, uint16_t lid = 0,
           const uint8_t* gid_ptr = nullptr, uint32_t mtu = 0,
           const std::string& hostname = "", const std::string& nic_name = "")
        : qp_num(qp_num),
          lid(lid),
          gid_table_index(0),
          mtu(mtu),
          is_initialized(false) {
        if (gid_ptr != nullptr) {
            std::memcpy(gid, gid_ptr, 16);
        } else {
            std::memset(gid, 0, 16);  // Initialize GID to 0 if not specified
        }
        std::memset(mac_addr, 0, 6);  // Initialize MAC address to 0
        std::strncpy(this->hostname, hostname.c_str(), MAX_HOSTNAME_LEN);
        this->hostname[MAX_HOSTNAME_LEN - 1] = '\0';  // Ensure null termination

        std::strncpy(this->nic_name, nic_name.c_str(), MAX_NIC_NAME_LEN);
        this->nic_name[MAX_NIC_NAME_LEN - 1] = '\0';  // Ensure null termination
    }

    // Copy constructor
    QPInfo(const QPInfo& other) {
        qp_num = other.qp_num;
        lid = other.lid;
        mtu = other.mtu;
        gid_table_index = other.gid_table_index;
        std::memcpy(gid, other.gid, 16);
        std::memcpy(mac_addr, other.mac_addr, 6);
        std::strncpy(hostname, other.hostname, MAX_HOSTNAME_LEN);
        std::strncpy(nic_name, other.nic_name, MAX_NIC_NAME_LEN);
        is_initialized = other.is_initialized;
    }

    QPInfo& operator=(const QPInfo& other) {
        if (this != &other) {
            qp_num = other.qp_num;
            lid = other.lid;
            mtu = other.mtu;
            gid_table_index = other.gid_table_index;
            std::memcpy(gid, other.gid, 16);
            std::memcpy(mac_addr, other.mac_addr, 6);
            std::strncpy(hostname, other.hostname, MAX_HOSTNAME_LEN);
            std::strncpy(nic_name, other.nic_name, MAX_NIC_NAME_LEN);
            is_initialized = other.is_initialized;
        }
        return *this;
    }

    // Print information
    void print() const {
        std::cout << "QP Number: " << std::dec << qp_num << "\n";
        std::cout << "LID: " << std::dec << lid << "\n";
        std::cout << "MTU: " << std::dec << mtu << "\n";
        std::cout << "GID Table Index: " << std::dec << static_cast<int>(gid_table_index) << "\n";
        std::cout << "GID: ";
        for (int i = 0; i < 16; ++i) {
            std::cout << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(gid[i]);
            if (i < 15) std::cout << ":";
        }
        std::cout << std::dec << "\n";
        std::cout << "MAC Address: ";
        for (int i = 0; i < 6; ++i) {
            std::cout << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(mac_addr[i]);
            if (i < 5) std::cout << ":";
        }
        std::cout << std::dec << "\n";
        std::cout << "Hostname: " << hostname << "\n";
        std::cout << "NIC Name: " << nic_name << "\n";
        std::cout << "Initialized: " << (is_initialized ? "Yes" : "No") << "\n";
    }

    // Set GID
    void set_gid(const uint8_t* gid_ptr) {
        if (gid_ptr != nullptr) {
            std::memcpy(gid, gid_ptr, 16);
        }
    }

    // Set MAC address
    void set_mac(const uint8_t* mac_ptr) {
        if (mac_ptr != nullptr) {
            std::memcpy(mac_addr, mac_ptr, 6);
        }
    }

    friend std::ostream& operator<<(std::ostream& os, const QPInfo& info) {
        os << "Hostname: " << info.hostname << ", "
           << "NIC Name: " << info.nic_name << ", MTU: " << info.mtu << ", "
           << "QP Number: " << info.qp_num << ", "
           << "LID: " << info.lid << ", "
           << "GID Index: " << static_cast<int>(info.gid_table_index) << ", "
           << "MAC: ";
        for (int i = 0; i < 6; ++i) {
            os << std::hex << static_cast<int>(info.mac_addr[i]);
            if (i < 5) os << ":";
        }
        os << ", GID: ";
        for (int i = 0; i < 16; ++i) {
            os << std::hex << static_cast<int>(info.gid[i]);
            if (i < 15) os << ":";
        }

        return os;  // Return os to support chaining
    }

    std::string to_string() const {
        auto ss = std::stringstream();
        ss << *this;
        return ss.str();
    }

    std::string serialize() const {
        std::string serializedData;
        
        // Convert each field to string and concatenate to serializedData
        serializedData += "qp_num:" + std::to_string(qp_num) + ";";
        serializedData += "lid:" + std::to_string(lid) + ";";
        serializedData += "gid:";
        for (int i = 0; i < 16; i++) {
            serializedData += std::to_string(static_cast<int>(gid[i])) + ",";
        }
        serializedData += ";gid_table_index:" + std::to_string(static_cast<int>(gid_table_index)) + ";";
        serializedData += "mac:";
        for (int i = 0; i < 6; i++) {
            serializedData += std::to_string(static_cast<int>(mac_addr[i])) + ",";
        }
        serializedData += ";mtu:" + std::to_string(mtu) + ";";
        serializedData += "hostname:" + std::string(hostname) + ";";
        serializedData += "nic_name:" + std::string(nic_name) + ";";
        serializedData += "is_initialized:" + std::to_string(is_initialized);

        return serializedData;
    }

    void deserialize(const std::string& serializedData) {
        std::istringstream iss(serializedData);
        std::string token;
        
        while (std::getline(iss, token, ';')) {
            std::istringstream tokenStream(token);
            std::string key;
            std::string value;

            std::getline(tokenStream, key, ':');
            std::getline(tokenStream, value, ':');

            if (key == "qp_num") {
                qp_num = static_cast<uint32_t>(std::stoi(value));
            } else if (key == "lid") {
                lid = static_cast<uint16_t>(std::stoi(value));
            } else if (key == "gid") {
                std::istringstream gidStream(value);
                std::string gidToken;
                int i = 0;
                while (std::getline(gidStream, gidToken, ',') && i < 16) {
                    gid[i++] = static_cast<uint8_t>(std::stoi(gidToken));
                }
            } else if (key == "gid_table_index") {
                gid_table_index = static_cast<uint8_t>(std::stoi(value));
            } else if (key == "mac") {
                std::istringstream macStream(value);
                std::string macToken;
                int i = 0;
                while (std::getline(macStream, macToken, ',') && i < 6) {
                    mac_addr[i++] = static_cast<uint8_t>(std::stoi(macToken));
                }
            } else if (key == "mtu") {
                mtu = static_cast<uint32_t>(std::stoi(value));
            } else if (key == "hostname") {
                std::strncpy(hostname, value.c_str(), sizeof(hostname) - 1);
                hostname[sizeof(hostname) - 1] = '\0'; // Ensure null termination
            } else if (key == "nic_name") {
                std::strncpy(nic_name, value.c_str(), sizeof(nic_name) - 1);
                nic_name[sizeof(nic_name) - 1] = '\0'; // Ensure null termination
            } else if (key == "is_initialized") {
                is_initialized = (std::stoi(value) != 0);
            }
        }
    }
};

// Global QP info collection function (commented out)
// void collect_global_qp_info(
//     const std::vector<QPInfo>& local_qp_info,
//     std::map<std::string, std::map<std::string, QPInfo>>& all_qp_infos,
//     bool print = false);

#endif  // QPINFO_H
