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

const size_t MAX_HOSTNAME_LEN = 64;
const size_t MAX_NIC_NAME_LEN = 64;

class QPInfo {
   public:
    uint32_t qp_num;                  // QP编号
    uint16_t lid;                     // 本地标识符 (LID)
    uint8_t gid[16];                  // 全球标识符 (GID)
    uint32_t mtu;                     // 最大传输单元 (MTU)
    char hostname[MAX_HOSTNAME_LEN];  // 主机名
    char nic_name[MAX_NIC_NAME_LEN];  // 网络接口名 (如 rdma0)

    // 构造函数
    QPInfo(uint32_t qp_num = 0, uint16_t lid = 0,
           const uint8_t* gid_ptr = nullptr, uint32_t mtu = 0,
           const std::string& hostname = "", const std::string& nic_name = "")
        : qp_num(qp_num), lid(lid), mtu(mtu) {
        if (gid_ptr != nullptr) {
            std::memcpy(gid, gid_ptr, 16);
        } else {
            std::memset(gid, 0, 16);  // 如果未指定GID，则初始化为0
        }
        std::strncpy(this->hostname, hostname.c_str(), MAX_HOSTNAME_LEN);
        this->hostname[MAX_HOSTNAME_LEN - 1] = '\0';  // 确保字符串以 '\0' 结尾

        std::strncpy(this->nic_name, nic_name.c_str(), MAX_NIC_NAME_LEN);
        this->nic_name[MAX_NIC_NAME_LEN - 1] = '\0';  // 确保字符串以 '\0' 结尾
    }

    // 拷贝构造函数
    QPInfo(const QPInfo& other) {
        qp_num = other.qp_num;
        lid = other.lid;
        mtu = other.mtu;
        std::memcpy(gid, other.gid, 16);
        std::strncpy(hostname, other.hostname, MAX_HOSTNAME_LEN);
        std::strncpy(nic_name, other.nic_name, MAX_NIC_NAME_LEN);
    }

    QPInfo& operator=(const QPInfo& other) {
        if (this != &other) {
            qp_num = other.qp_num;
            lid = other.lid;
            mtu = other.mtu;
            std::memcpy(gid, other.gid, 16);
            std::strncpy(hostname, other.hostname, MAX_HOSTNAME_LEN);
            std::memcpy(nic_name, other.nic_name, MAX_NIC_NAME_LEN);
        }
        return *this;
    }

    // 输出信息
    void print() const {
        std::cout << "QP Number: " << qp_num << "\n";
        std::cout << "LID: " << lid << "\n";
        std::cout << "MTU: " << mtu << "\n";
        std::cout << "GID: ";
        for (int i = 0; i < 16; ++i) {
            std::cout << std::hex << static_cast<int>(gid[i]);
            if (i < 15) std::cout << ":";
        }
        std::cout << "\n";
        std::cout << "Hostname: " << hostname << "\n";
        std::cout << "NIC Name: " << nic_name << "\n";
    }

    // 设置GID
    void set_gid(const uint8_t* gid_ptr) {
        if (gid_ptr != nullptr) {
            std::memcpy(gid, gid_ptr, 16);
        }
    }

    friend std::ostream& operator<<(std::ostream& os, const QPInfo& info) {
        os << "Hostname: " << info.hostname << ", "
           << "NIC Name: " << info.nic_name << ", MTU: " << info.mtu << ", "
           << "QP Number: " << info.qp_num << ", "
           << "LID: " << info.lid << ", "
           << "GID: ";
        for (int i = 0; i < 16; ++i) {
            os << std::hex << static_cast<int>(info.gid[i]);
            if (i < 15) os << ":";
        }

        return os;  // 返回 os 以兼容 ostream
    }

    std::string to_string() const {
        auto ss = std::stringstream();
        ss << *this;
        return ss.str();
    }

    std::string serialize() const {
        std::string serializedData;
        
        // 将每个字段转换为字符串并拼接到 serializedData 中
        serializedData += "qp_num:" + std::to_string(qp_num) + ";";
        serializedData += "lid:" + std::to_string(lid) + ";";
        serializedData += "gid:";
        for (int i = 0; i < 16; i++) {
            serializedData += std::to_string(static_cast<int>(gid[i])) + ",";
        }
        serializedData += ";mtu:" + std::to_string(mtu) + ";";
        serializedData += "hostname:" + std::string(hostname) + ";";
        serializedData += "nic_name:" + std::string(nic_name);

        // printf("local serializedData: %s\n", serializedData.c_str());

        return serializedData;
    }

    void deserialize(const std::string& serializedData) {
        std::istringstream iss(serializedData);
        std::string token;

        // printf("remote serializedData: %s\n", serializedData.c_str());
        
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
                while (std::getline(gidStream, gidToken, ',')) {
                    gid[i++] = static_cast<uint8_t>(std::stoi(gidToken));
                }
            } else if (key == "mtu") {
                mtu = static_cast<uint32_t>(std::stoi(value));
            } else if (key == "hostname") {
                std::strncpy(hostname, value.c_str(), sizeof(hostname) - 1);
                hostname[sizeof(hostname) - 1] = '\0'; // 确保以'\0'结尾
            } else if (key == "nic_name") {
                std::strncpy(nic_name, value.c_str(), sizeof(nic_name) - 1);
                nic_name[sizeof(nic_name) - 1] = '\0'; // 确保以'\0'结尾
            }
        }
    }
};

// void collect_global_qp_info(
//     const std::vector<QPInfo>& local_qp_info,
//     std::map<std::string, std::map<std::string, QPInfo>>& all_qp_infos,
//     bool print = false);

#endif  // QPINFO_H
