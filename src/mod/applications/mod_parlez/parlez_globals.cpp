#include "parlez_globals.h"
#include "translation_session.h"
#include <algorithm>

ParlezGlobals& ParlezGlobals::getInstance() {
    static ParlezGlobals instance;
    return instance;
}

switch_status_t ParlezGlobals::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return SWITCH_STATUS_SUCCESS;
    }
       
    // Load configuration from FreeSWITCH config
    switch_xml_t cfg, xml, settings, param;
    
    if ((xml = switch_xml_open_cfg("parlez.conf", &cfg, NULL))) {
        if ((settings = switch_xml_child(cfg, "settings"))) {
            for (param = switch_xml_child(settings, "param"); param; param = param->next) {
                char *var = (char *) switch_xml_attr_soft(param, "name");
                char *val = (char *) switch_xml_attr_soft(param, "value");
                
                if (!strcasecmp(var, "azure-subscription-key")) {
                    azure_subscription_key_ = val;
                } else if (!strcasecmp(var, "azure-region")) {
                    azure_region_ = val;
                }
            }
        }
        switch_xml_free(xml);
    }else{
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, 
                         "mod_parlez: parlez.conf.xml not found\n");
    }
    
    // Set defaults if not configured
    if (azure_subscription_key_.empty()) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, 
                         "mod_parlez: No Azure subscription key configured\n");
    }
    
    if (azure_region_.empty()) {
        azure_region_ = "uksouth";
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
                         "mod_parlez: Using default Azure region: %s\n", azure_region_.c_str());
    }
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
                     "mod_parlez: Globals initialized successfully\n");
    
    initialized_ = true;
    return SWITCH_STATUS_SUCCESS;
}

void ParlezGlobals::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Clean up all sessions
    for (auto& pair : sessions_) {
        if (pair.second) {
            pair.second->cleanup();
        }
    }
    sessions_.clear();
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
                     "mod_parlez: Globals cleaned up\n");
    
    initialized_ = false;
}

void ParlezGlobals::addSession(const std::string& conference_name, 
                              std::shared_ptr<TranslationSession> session) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[conference_name] = session;
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, 
                     "mod_parlez: Added session for conference: %s\n", 
                     conference_name.c_str());
}

void ParlezGlobals::removeSession(const std::string& conference_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(conference_name);
    if (it != sessions_.end()) {
        if (it->second) {
            it->second->cleanup();
        }
        sessions_.erase(it);
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, 
                         "mod_parlez: Removed session for conference: %s\n", 
                         conference_name.c_str());
    }
}

std::shared_ptr<TranslationSession> ParlezGlobals::getSession(const std::string& conference_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(conference_name);
    return (it != sessions_.end()) ? it->second : nullptr;
}

void ParlezGlobals::setAzureSubscriptionKey(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    azure_subscription_key_ = key;
}

void ParlezGlobals::setAzureRegion(const std::string& region) {
    std::lock_guard<std::mutex> lock(mutex_);
    azure_region_ = region;
}

std::string ParlezGlobals::getAzureSubscriptionKey() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return azure_subscription_key_;
}

std::string ParlezGlobals::getAzureRegion() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return azure_region_;
}