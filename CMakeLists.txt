cmake_minimum_required(VERSION 3.10)

project(dime)

# Source Files
set(SOURCE_FILES
    "src/ActiveLanguageProfileNotifySink.cpp"
    "src/BaseDictionaryEngine.cpp"
    "src/BinaryDictionaryEngine.cpp"
    "src/BaseWindow.cpp"
    "src/ButtonWindow.cpp"
    "src/CandidateListUIPresenter.cpp"
    "src/CandidateWindow.cpp"
    "src/Compartment.cpp"
    "src/Composition.cpp"
    "src/CompositionProcessorEngine.cpp"
    "src/EnumTfCandidates.cpp"
    "src/KeyStateCategory.cpp"
    "src/DIMEBaseStructure.cpp"
    "src/SearchCandidateProvider.cpp"
    "src/DictionaryParser.cpp"
    "src/DictionaryIndex.cpp"
    "src/DictionarySearch.cpp"
    "src/DebugLog.cpp"
    "src/DisplayAttribute.cpp"
    "src/DisplayAttributeInfo.cpp"
    "src/DisplayAttributeProvider.cpp"
    "src/DllMain.cpp"
    "src/EditSession.cpp"
    "src/EndComposition.cpp"
    "src/EnumDisplayAttributeInfo.cpp"
    "src/File.cpp"
    "src/FileMapping.cpp"
    "src/FunctionProviderSink.cpp"
    "src/GetTextExtentEditSession.cpp"
    "src/Globals.cpp"
    "src/KeyEventSink.cpp"
    "src/KeyHandler.cpp"
    "src/KeyHandlerEditSession.cpp"
    "src/LanguageBar.cpp"
    "src/Register.cpp"
    "src/RegKey.cpp"
    "src/Server.cpp"
    "src/ShadowWindow.cpp"
    "src/StartComposition.cpp"
    "src/StatusWindow.cpp"
    "src/ContextMenu.cpp"
    "src/ConfigDialog.cpp"
    "src/TableDictionaryEngine.cpp"
    "src/DIME.cpp"
    "src/TextEditSink.cpp"
    "src/TfInputProcessorProfile.cpp"
    "src/TfTextLayoutSink.cpp"
    "src/ThreadFocusSink.cpp"
    "src/ThreadMgrEventSink.cpp"
    "src/TipCandidateList.cpp"
    "src/TipCandidateString.cpp"
    "src/DIME.def"
)

# Header Files
set(HEADER_FILES
    "src/BaseDictionaryEngine.h"
    "src/BaseWindow.h"
    "src/BinaryDictionaryEngine.h"
    "src/ButtonWindow.h"
    "src/CandidateListUIPresenter.h"
    "src/CandidateWindow.h"
    "src/Compartment.h"
    "src/CompositionProcessorEngine.h"
    "src/define.h"
    "src/EnumTfCandidates.h"
    "src/KeyStateCategory.h"
    "src/DIMEBaseStructure.h"
    "src/SearchCandidateProvider.h"
    "src/DictionaryParser.h"
    "src/DictionaryIndex.h"
    "src/DictionarySearch.h"
    "src/DebugLog.h"
    "src/DisplayAttributeInfo.h"
    "src/EditSession.h"
    "src/EnumDisplayAttributeInfo.h"
    "src/File.h"
    "src/FileMapping.h"
    "src/GetTextExtentEditSession.h"
    "src/Globals.h"
    "src/KeyHandlerEditSession.h"
    "src/LanguageBar.h"
    "src/Private.h"
    "src/RegKey.h"
    "src/resource.h"
    "src/DIME.h"
    "src/ShadowWindow.h"
    "src/StatusWindow.h"
    "src/ContextMenu.h"
    "src/ConfigDialog.h"
    "src/stdafx.h"
    "src/TableDictionaryEngine.h"
    "src/TfInputProcessorProfile.h"
    "src/TfTextLayoutSink.h"
    "src/TipCandidateList.h"
    "src/TipCandidateString.h"
    "src/Version.h"
)

# Resource Files
set(RESOURCE_FILES
    "src/DIME.rc"
    "image/Dime.ico"
    "image/ImeModeOn.ico"
    "image/ImeModeOff.ico"
    "image/DoubleSingleByteOn.ico"
    "image/DoubleSingleByteOff.ico"
    "image/PunctuationOn.ico"
    "image/PunctuationOff.ico"
)

# Dictionary Files
set(DICTIONARY_FILES
    "Dictionary/wubi98.txt"
    "Dictionary/pinyin.txt"
)

# Create groupings for Visual Studio
source_group("Source Files" FILES ${SOURCE_FILES})
source_group("Header Files" FILES ${HEADER_FILES})
source_group("Resource Files" FILES ${RESOURCE_FILES})
source_group("Dictionary" FILES ${DICTIONARY_FILES})

add_library(dime SHARED
    ${SOURCE_FILES}
    ${HEADER_FILES}
    ${RESOURCE_FILES}
    ${DICTIONARY_FILES}
)

target_compile_definitions(dime PRIVATE
    WIN32
    _WINDOWS
    _USRDLL
    DIME_EXPORTS
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(dime PRIVATE /utf-8 /MP)
endif()

target_link_libraries(dime PRIVATE
    shlwapi
    msimg32
    shell32
)

# ---------------------------------------------------------------------------
# Offline tool: compile a text dictionary ("code"="word", UTF-16LE) into the
# precompiled binary format (.bin). Shares BinaryDictFormat.h with the runtime.
# Built as part of the solution; run it from deploy scripts before registering.
# ---------------------------------------------------------------------------
add_executable(build_bindict
    "src/build_bindict.cpp"
    "src/BinaryDictFormat.h"
)

target_compile_definitions(build_bindict PRIVATE
    WIN32
    _WINDOWS
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(build_bindict PRIVATE /utf-8)
endif()
