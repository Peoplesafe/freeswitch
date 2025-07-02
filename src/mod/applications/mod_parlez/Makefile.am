include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_parlez

# Microsoft Speech SDK paths (adjust based on your installation)
SPEECH_SDK_ROOT = /usr/local/lib/speechsdk
SPEECH_SDK_LIB = $(SPEECH_SDK_ROOT)/lib/x64

# Module library
mod_LTLIBRARIES = mod_parlez.la

# Source files - now includes the separated classes
mod_parlez_la_SOURCES  = mod_parlez.cpp translation_session.cpp parlez_globals.cpp audio_resampler.cpp audio_injection_manager.cpp participant_recognizer.cpp

# FLAGS
mod_parlez_la_CFLAGS   = $(AM_CFLAGS)
mod_parlez_la_CXXFLAGS = $(AM_CXXFLAGS) -std=c++14 -I$(SPEECH_SDK_ROOT)/include/cxx_api -I$(SPEECH_SDK_ROOT)/include/c_api
mod_parlez_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(SPEECH_SDK_ROOT)/include/cxx_api -I$(SPEECH_SDK_ROOT)/include/c_api

# Libraries and linking
mod_parlez_la_LIBADD   = $(switch_builddir)/libfreeswitch.la -L$(SPEECH_SDK_LIB) -lMicrosoft.CognitiveServices.Speech.core
mod_parlez_la_LDFLAGS  = -avoid-version -module -no-undefined -shared -Wl,-rpath,$(SPEECH_SDK_LIB)