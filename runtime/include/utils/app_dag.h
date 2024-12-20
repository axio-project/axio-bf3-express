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
/**
 * ----------------------General structure----------------------
 */ 
using json = nlohmann::json;
// Josn file structure
struct DAGComponent {
    std::string name;
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
 * ----------------------Public Methods----------------------
 */ 
    void load_from_file() {
        std::ifstream file(this->_file_path);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open file " + _file_path);
        }

        json j;
        file >> j;

        // update app name
        this->_app_name = j["app_name"].get<std::string>();
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
        _components_map.clear();
        // iterate over the json object
        for (const auto &item : j["dp_graph"]) {
            if (item.contains("component")) {
                // extract component name
                std::string component_name = item["component"];

                DAGComponent component;
                component.name = component_name;

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
                _components_map[component_name] = component;
            }
        }
    }

    /**
     * \brief get a component config by name
     */
    const DAGComponent* get_component_config(const std::string &name) const {
        auto it = _components_map.find(name);
        if (it != _components_map.end()) {
            return &it->second;
        }
        return nullptr;
    }
    
/**
 * ----------------------Util Methods----------------------
 */

    /**
     * \brief print the content of the DAG
     */
    void print() const {
        std::cout << "----------------------" << YELLOW << "Parse App DAG: " << _file_path << RESET << "----------------------" << "\n";
        std::cout << "=====" << YELLOW << "App Name: " << _app_name << RESET << "=====" << "\n\n";
        for (const auto &[name, component] : _components_map) {
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
 * ----------------------Internel Parameters----------------------
 */ 
private:
    std::string _file_path;
    std::string _app_name;
    component_typeid_t _enabled_components = 0;
    std::map<std::string, DAGComponent> _components_map;
};
}