#include <switch.h>
#include "parlez_globals.h"
#include "translation_session.h"
#include <memory>
#include <string>

// Module definition
SWITCH_MODULE_LOAD_FUNCTION(mod_parlez_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_parlez_shutdown);
SWITCH_MODULE_DEFINITION(mod_parlez, mod_parlez_load, mod_parlez_shutdown, NULL);

// Event handler for conference events
static void conference_event_handler(switch_event_t* event) {
    const char* conference_name = switch_event_get_header(event, "Conference-Name");
    const char* action = switch_event_get_header(event, "Action");
    const char* member_id_str = switch_event_get_header(event, "Member-ID");
    const char* caller_id = switch_event_get_header(event, "Caller-Caller-ID-Number");
    const char* uuid = switch_event_get_header(event, "Unique-ID");
    
    if (action) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
            "PARLEZ: Received Conference Event - Action '%s'\n", action);
    }
    
    if (!conference_name || !action || !member_id_str) {
        return;
    }
    
    uint32_t member_id = static_cast<uint32_t>(atoi(member_id_str));
    auto& globals = ParlezGlobals::getInstance();
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                     "mod_parlez: Conference event - Conference: %s, Action: %s, Member: %u\n",
                     conference_name, action, member_id);
    
    if (!strcasecmp(action, "add-member")) {
        if (!uuid) {
              switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                               "mod_parlez: Missing Unique-ID for add-member action\n");
            return;
        }

        switch_core_session_t* core_session = switch_core_session_locate(uuid);
        if (!core_session) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                            "mod_parlez: Failed to locate session for UUID: %s\n", uuid);
            return;
        }
        
        // Get or create translation session for this conference
        auto session = globals.getSession(conference_name);
        if (!session) {
            session = std::make_shared<TranslationSession>(conference_name);
            if (session->initialize() == SWITCH_STATUS_SUCCESS) {
                globals.addSession(conference_name, session);
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                                 "mod_parlez: Created new translation session for conference: %s\n",
                                 conference_name);
            } else {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                 "mod_parlez: Failed to initialize translation session for conference: %s\n",
                                 conference_name);
                return;
            }
        }
        
        // Add participant with default language settings
        // Language is set in dial plan by extension
        std::string language_code = "en-GB";  // Default to English
        std::string voice_name = "en-GB-LibbyNeural";  // Default voice
        switch_channel_t *channel = switch_core_session_get_channel(core_session);

        const char *language = switch_channel_get_variable(channel, "language");
        if (!language) {
            language = switch_channel_get_variable(channel, "channel_language");
        }
        if (!language) {
            language = "en";  // default fallback
        }

        if (!strcasecmp(language, "fr")) {
            language_code = "fr-FR";  // French
            voice_name = "fr-FR-DeniseNeural";  // French voice
        }
        if (!strcasecmp(language, "it")) {
            language_code = "it-IT";  // Italian
            voice_name = "it-IT-ElsaNeural";  // Italian voice
        }

        session->addParticipant(member_id, caller_id ? caller_id : "Unknown", 
                               language_code, voice_name);
        
        // Attach media bug to capture audio
        session->attachMediaBug(core_session, member_id);
        switch_core_session_rwunlock(core_session);

    } else if (!strcasecmp(action, "del-member")) {
        auto session = globals.getSession(conference_name);
        if (session) {
            session->removeParticipant(member_id);
            
            // Check if conference is empty and clean up
            // This is simplified - in reality you'd track participant count
            // For now, we'll keep the session alive until module shutdown
        }
        
    } else if (!strcasecmp(action, "start-talking")) {
        auto session = globals.getSession(conference_name);
        if (session) {
            session->onStartTalking(member_id);
        }
        
    } else if (!strcasecmp(action, "stop-talking")) {
        auto session = globals.getSession(conference_name);
        if (session) {
            session->onStopTalking(member_id);
        }
    }
}

// CLI command handler
#define PARLEZ_SYNTAX "status | config [set <param> <value>]"

SWITCH_STANDARD_API(parlez_api_function) {
    if (zstr(cmd)) {
        stream->write_function(stream, "Usage: parlez " PARLEZ_SYNTAX "\n");
        return SWITCH_STATUS_SUCCESS;
    }
    
    auto& globals = ParlezGlobals::getInstance();
    
    if (!strcasecmp(cmd, "status")) {
        stream->write_function(stream, "mod_parlez Status:\n");
        stream->write_function(stream, "  Azure Region: %s\n", 
                              globals.getAzureRegion().c_str());
        stream->write_function(stream, "  Azure Key: %s\n", 
                              globals.getAzureSubscriptionKey().empty() ? "Not configured" : "Configured");
        
    } else if (!strncasecmp(cmd, "config", 6)) {
        char* argv[4] = { 0 };
        int argc = switch_split(const_cast<char*>(cmd), ' ', argv);
        
        if (argc >= 4 && !strcasecmp(argv[1], "set")) {
            if (!strcasecmp(argv[2], "azure-key")) {
                globals.setAzureSubscriptionKey(argv[3]);
                stream->write_function(stream, "Azure subscription key updated\n");
            } else if (!strcasecmp(argv[2], "azure-region")) {
                globals.setAzureRegion(argv[3]);
                stream->write_function(stream, "Azure region updated to: %s\n", argv[3]);
            } else {
                stream->write_function(stream, "Unknown parameter: %s\n", argv[2]);
            }
        } else {
            stream->write_function(stream, "Usage: parlez config set <param> <value>\n");
            stream->write_function(stream, "Parameters: azure-key, azure-region\n");
        }
        
    } else {
        stream->write_function(stream, "Usage: parlez " PARLEZ_SYNTAX "\n");
    }
    
    return SWITCH_STATUS_SUCCESS;
}

// Module load function
SWITCH_MODULE_LOAD_FUNCTION(mod_parlez_load) {
    switch_api_interface_t* api_interface;
    switch_status_t status;
    
    *module_interface = switch_loadable_module_create_module_interface(pool, modname);
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                     "mod_parlez: Loading FreeSWITCH Translation Module\n");
    
    // Initialize global state
    auto& globals = ParlezGlobals::getInstance();
    status = globals.initialize();
    if (status != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                         "mod_parlez: Failed to initialize globals\n");
        return status;
    }
    
    // Register event handler for conference events
    if (switch_event_bind(modname, SWITCH_EVENT_CUSTOM, "conference::maintenance",
                         conference_event_handler, NULL) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                         "mod_parlez: Failed to bind to conference events\n");
        globals.cleanup();
        return SWITCH_STATUS_TERM;
    }
    
    // Register CLI command
    SWITCH_ADD_API(api_interface, "parlez", "Parlez translation module commands",
                   parlez_api_function, PARLEZ_SYNTAX);
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                     "mod_parlez: Module loaded successfully\n");
    
    return SWITCH_STATUS_SUCCESS;
}

// Module shutdown function
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_parlez_shutdown) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                     "mod_parlez: Shutting down module\n");
    
    // Unbind from events
    switch_event_unbind_callback(conference_event_handler);
    
    // Cleanup global state
    auto& globals = ParlezGlobals::getInstance();
    globals.cleanup();
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                     "mod_parlez: Module shutdown complete\n");
    
    return SWITCH_STATUS_SUCCESS;
}