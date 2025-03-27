#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>

#include "common.h"
#include "log.h"

namespace nicc
{
using json = nlohmann::json;
/**
 * \brief Host configuration structure for remote and local hosts
 */
struct HostConfig {
    std::string ipv4;           ///< IPv4 address
    uint16_t mgnt_port;         ///< Management port
    uint16_t data_port;         ///< Data port
    std::vector<std::string> connect_to; ///< Components to connect to
};

/**
 * \brief General structure for DAG components
 */ 
struct DAGComponent {
    std::string name;
    component_typeid_t component_id;
    std::map<std::string, std::string> data_path;  // store data_path
    std::map<std::string, std::vector<std::string>> ctrl_path; // store ctrl_path
    std::vector<std::map<std::string, std::string>> ctrl_entries; // store ctrl_path entries
};

/**
 * \brief read json file of app DAG and parse it
 */
class AppDAG {
public:
    AppDAG(std::string file_path) : _file_path(file_path) {
        load_from_file();
    }

    /**
     * \brief Get remote host configurations
     * \return Vector of remote host configurations
     */
    const std::vector<HostConfig>& get_remote_hosts() const { return this->_remote_hosts; }

    /**
     * \brief Get local host configurations
     * \return Vector of local host configurations
     */
    const std::vector<HostConfig>& get_local_hosts() const { return this->_local_hosts; }

/**
 * ----------------------Public Methods----------------------
 */ 
    void load_from_file() {
        std::ifstream file(this->_file_path);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open file " + this->_file_path);
        }

        json j;
        file >> j;

        // update app name
        this->_app_name = j["app_name"].get<std::string>();
        
        // Parse remote hosts
        if (j.contains("remote_host")) {
            for (const auto& host : j["remote_host"]) {
                HostConfig config;
                config.ipv4 = host["ipv4"].get<std::string>();
                config.mgnt_port = host["mgnt_port"].get<uint16_t>();
                config.data_port = host["data_port"].get<uint16_t>();
                for (const auto& component : host["connect_to"]) {
                    config.connect_to.push_back(component);
                }
                this->_remote_hosts.push_back(config);
            }
        }

        // Parse local hosts
        if (j.contains("local_host")) {
            for (const auto& host : j["local_host"]) {
                HostConfig config;
                config.ipv4 = host["ipv4"].get<std::string>();
                config.mgnt_port = host["mgnt_port"].get<uint16_t>();
                config.data_port = host["data_port"].get<uint16_t>();
                for (const auto& component : host["connect_to"]) {
                    config.connect_to.push_back(component);
                }
                this->_local_hosts.push_back(config);
            }
        }

        // update enabled components
        for (const auto &v : j["enabled_components"]) {
            if (v == "flow_engine") {
                this->_enabled_components |= kComponent_FlowEngine;
            } else if (v == "dpa") {
                this->_enabled_components |= kComponent_DPA;
            } else if (v == "soc") {
                this->_enabled_components |= kComponent_SoC;
            }
            else {
                NICC_ERROR("[Parse Application DAG]: Unknown component: %s", v.get<std::string>().c_str());
            }
        }
        // clear current data in the map
        this->_components_map.clear();
        // iterate over the json object
        for (const auto &item : j["dp_graph"]) {
            if (item.contains("component")) {
                // extract component name
                std::string component_name = item["component"];

                DAGComponent component;
                component.name = component_name;
                component.component_id = component_name_to_id(component_name);

                // parse data_path
                if (item.contains("data_path")) {
                    for (auto &[key, value] : item["data_path"].items()) {
                        component.data_path[key] = value.get<std::string>();
                    }
                }

                // parse ctrl_path
                if (item.contains("ctrl_path")) {
                    for (auto &[key, value] : item["ctrl_path"].items()) {
                        if (value.is_array()) {
                            if (key == "entries") {
                                for (const auto &entry : value) {
                                    component.ctrl_entries.push_back({
                                        {"match", entry["match"].get<std::string>()},
                                        {"action", entry["action"].get<std::string>()}
                                    });
                                }
                            } else {
                                for (const auto &v : value) {
                                    component.ctrl_path[key].push_back(v.get<std::string>());
                                }
                            }
                        } 
                        else {
                            // unexpected format
                            NICC_WARN("[Parse Application DAG]: Unexpected format in ctrl_path");
                        }
                    }
                }

                // store into map
                this->_components_map[component_name] = component;
            }
        }
    }
    
/**
 * ----------------------Util Methods----------------------
 */

    /**
     * \brief print the content of the DAG
     */
    void print() const {
        std::cout << "----------------------" << YELLOW << "Parse App DAG: " << _file_path << RESET << "----------------------" << "\n";
        std::cout << "=====" << YELLOW << "App Name: " << this->_app_name << RESET << "=====" << "\n\n";
        
        // Print remote hosts
        if (!_remote_hosts.empty()) {
            std::cout << "=====" << YELLOW << "Remote Hosts" << RESET << "=====\n";
            for (const auto& host : _remote_hosts) {
                std::cout << "  IPv4: " << host.ipv4 << "\n";
                std::cout << "  Management Port: " << host.mgnt_port << "\n";
                std::cout << "  Data Port: " << host.data_port << "\n";
                std::cout << "  Connect to: ";
                for (const auto& component : host.connect_to) {
                    std::cout << component << " ";
                }
                std::cout << "\n";
            }
            std::cout << "\n";
        }

        // Print local hosts
        if (!_local_hosts.empty()) {
            std::cout << "=====" << YELLOW << "Local Hosts" << RESET << "=====\n";
            for (const auto& host : _local_hosts) {
                std::cout << "  IPv4: " << host.ipv4 << "\n";
                std::cout << "  Management Port: " << host.mgnt_port << "\n";
                std::cout << "  Data Port: " << host.data_port << "\n";
                std::cout << "  Connect to: ";
                for (const auto& component : host.connect_to) {
                    std::cout << component << " ";
                }
                std::cout << "\n";
            }
            std::cout << "\n";
        }

        // Print components
        for (const auto &[name, component] : this->_components_map) {
            std::cout << "=====" << YELLOW << "Component: " << name << RESET << "=====\n";

            // print data_path
            if (!component.data_path.empty()) {
                std::cout << "  Data Path:\n";
                for (const auto &[key, value] : component.data_path) {
                    std::cout << "    " << key << ": " << value << "\n";
                }
            }

            // print ctrl_path
            if (!component.ctrl_path.empty()) {
                std::cout << "  Ctrl Path:\n";
                for (const auto &[key, values] : component.ctrl_path) {
                    std::cout << "    " << key << ": ";
                    for (const auto &value : values) {
                        std::cout << value << " ";
                    }
                    std::cout << "\n";
                }
            }

            // print entries
            if (!component.ctrl_entries.empty()) {
                std::cout << "  Entries:\n";
                for (const auto &entry : component.ctrl_entries) {
                    std::cout << "    Match: " << entry.at("match")
                                << ", Action: " << entry.at("action") << "\n";
                }
            }

            std::cout << "\n";
        }
        std::cout << "--------------------------------------------\n\n";
    }

    /**
     * \brief get enabled components
     */
    component_typeid_t get_enabled_components() {
        return this->_enabled_components;
    }

    /**
     * \brief get host config by name
     */
    const HostConfig* get_host_config(const std::string &name) const {
        if (name == "remote") {
            return &this->_remote_hosts[0];
        }
        else if (name == "local") {
            return &this->_local_hosts[0];
        }
        else {
            NICC_ERROR("[Application DAG]: Unknown host type: %s, the input should be either remote or local", name.c_str());
            return nullptr;
        }
    }

    /**
     * \brief get a component config by name
     */
    const DAGComponent* get_component_config(const std::string &name) const {
        auto it = this->_components_map.find(name);
        if (it != this->_components_map.end()) {
            return &it->second;
        }
        return nullptr;
    }

    /**
     * \brief get a component block config by component id
     */
    const DAGComponent* get_component_config(const component_typeid_t &component_id) const {
        for (const auto &[name, component] : this->_components_map) {
            if (component.component_id == component_id) {
                return &component;
            }
        }
        return nullptr;
    }
    // /**
    //  * \brief find neighbor component block by component id
    //  */
    // component_typeid_t find_neighbor_component(const component_typeid_t &component_id) const {
    //     const DAGComponent* component = this->get_component_config(component_id);
    //     if (component == nullptr) {
    //         NICC_ERROR("[Application DAG]: Failed to find component: %d", component_id);
    //         return kComponent_Unknown;
    //     }
    //     // search neighbor component in ctrl_path
    //     return component->next_component;  
    // }

    /**
     * \brief extract string between parentheses
     * \param str input string like "fwd(dpa)"
     * \return string between parentheses, empty string if not found
     */
    static std::string extract_string_in_parentheses(const std::string& str) {
        size_t start = str.find('(');
        if (start == std::string::npos) {
            return "";
        }
        size_t end = str.find(')', start);
        if (end == std::string::npos) {
            return "";
        }
        return str.substr(start + 1, end - start - 1);
    }

    /**
     * \brief component name to component id
     */
    static component_typeid_t component_name_to_id(const std::string& name) {
        if (name == "root") {
            return kComponent_Unknown;
        }
        else if (name == "flow_engine") {
            return kComponent_FlowEngine;
        }
        else if (name == "dpa") {
            return kComponent_DPA;
        }
        else if (name == "soc") {
            return kComponent_SoC;
        }
        else if (name == "remote_host") {
            return kRemote_Host;
        }
        else if (name == "local_host") {
            return kLocal_Host;
        }
        else {
            NICC_ERROR("[Application DAG]: Unknown component: %s, the component name should be either root, flow_engine, dpa, soc, remote_host, or local_host", name.c_str());
            return kComponent_Unknown;
        }
    }

/**
 * ----------------------Internel Parameters----------------------
 */ 
private:
    std::string _file_path;
    std::string _app_name;
    component_typeid_t _enabled_components = 0;
    std::map<std::string, DAGComponent> _components_map;
    std::vector<HostConfig> _remote_hosts;    ///< Remote host configurations
    std::vector<HostConfig> _local_hosts;     ///< Local host configurations
};
}