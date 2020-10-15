#include "common/common.h"
#include "utils/logger.h"
#include "WiiUScreen.h"
#include "StringTools.h"
#include "fs/FSUtils.h"
#include "common/fst_structs.h"
#include "InstallerService.h"
#include "utils/utils.h"
#include "utils/pugixml.hpp"
#include <coreinit/mcp.h>
#include <string>
#include <memory>
#include <cstdlib>
#include <malloc.h>
#include <sstream>

systemXMLInformation systemXMLHashInformation[] = {
        {WII_U_MENU_JAP,    0x0005001010040000, "2645065A42D18D390C78543E3C4FE7E1D1957A63"},
        {WII_U_MENU_USA,    0x0005001010040100, "124562D41A02C7112DDD5F9A8F0EE5DF97E23471"},
        {WII_U_MENU_EUR,    0x0005001010040200, "F06041A4E5B3F899E748F1BAEB524DE058809F1D"},
        {HEALTH_SAFETY_JAP, 0x000500101004E000, "066D672824128713F0A7D156142A68B998080148"},
        {HEALTH_SAFETY_USA, 0x000500101004E100, "0EBCA1DFC0AB7A6A7FE8FB5EAF23179621B726A1"},
        {HEALTH_SAFETY_EUR, 0x000500101004E200, "DE46EC3E9B823ABA6CB0638D0C4CDEEF9C793BDD"}
};

appInformation supportedApps[] = {
        {0x000500101004E000, "Health and Safety Information [JPN]", false, {'\0'}, "9D34DDD91604D781FDB0727AC75021833304964C", "0"},
        {0x000500101004E100, "Health and Safety Information [USA]", false, {'\0'}, "045734666A36C7EF0258A740855886EBDB20D59B", "0"},
        {0x000500101004E200, "Health and Safety Information [EUR]", false, {'\0'}, "130A76F8B36B36D43B88BBC74393D9AFD9CFD2A4", "F6EBF7BC8AE3AF3BB8A42E0CF3FDA051278AEB03"},
};


InstallerService::eResults InstallerService::checkCOS(const std::string &path, char *hash) {
    std::string cosFilePath = path + "/code/cos1.xml";
    DEBUG_FUNCTION_LINE("Loading %s", cosFilePath.c_str());

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(cosFilePath.c_str());
    if (!result) {
        DEBUG_FUNCTION_LINE("failed to open %s : %s", cosFilePath.c_str(), result.description());
        return COS_XML_PARSING_FAILED;
    }

    patchCOS(&doc);

    std::stringstream ss;
    doc.save(ss, "  ", pugi::format_default, pugi::encoding_utf8);

    std::string newHash = Utils::calculateSHA1(ss.str().c_str(), ss.str().size());

    if (std::string(hash) == newHash) {
        DEBUG_FUNCTION_LINE("Success! cos.xml is compatible");
        return SUCCESS;
    } else {
        DEBUG_FUNCTION_LINE("Hash mismatch! cos.xml is NOT compatible");
    }

    return COS_XML_HASH_MISMATCH;
}

InstallerService::eResults InstallerService::checkSystemXML(const std::string &path, uint64_t titleId) {
    std::string inputFile = std::string(path + "/system.xml");

    systemXMLInformation *data = NULL;
    int arrayLength = (sizeof(systemXMLHashInformation) / sizeof(*systemXMLHashInformation));
    for (int i = 0; i < arrayLength; i++) {
        if (systemXMLHashInformation[i].titleId == titleId) {
            data = &systemXMLHashInformation[i];
            break;
        }
    }

    if (data == NULL) {
        DEBUG_FUNCTION_LINE("system xml information were not found.");
        return SYSTEM_XML_INFORMATION_NOT_FOUND;
    }

    DEBUG_FUNCTION_LINE("Setting coldboot to %016llX", data->titleId);


    pugi::xml_document doc;
    pugi::xml_parse_result resultSystem = doc.load_file(inputFile.c_str());

    if (!resultSystem) {
        DEBUG_FUNCTION_LINE("Error while parsing %s: %s", inputFile.c_str(), resultSystem.description());
        return SYSTEM_XML_PARSING_FAILED;
    }

    char tmp[18];
    snprintf(tmp, 17, "%016llX", data->titleId);

    doc.child("system").child("default_title_id").first_child().set_value(tmp);

    std::stringstream ss;
    doc.save(ss, "  ", pugi::format_default, pugi::encoding_utf8);

    std::string newHash = Utils::calculateSHA1(ss.str().c_str(), ss.str().size());

    if (std::string(data->hash) == newHash) {
        DEBUG_FUNCTION_LINE("Success! system.xml is compatible");
        return SUCCESS;
    } else {
        DEBUG_FUNCTION_LINE("Hash mismatch! system.xml is NOT compatible");
    }
    return SYSTEM_XML_HASH_MISMATCH;
}

InstallerService::eResults InstallerService::checkFST(const std::string &path, const char *fstHash) {
    std::string fstFilePath = path + "/code/title.fst";

    uint8_t *fstData = nullptr;
    uint32_t fstDataSize = 0;

    DEBUG_FUNCTION_LINE("Trying to load FST from %s", fstFilePath.c_str());
    if (FSUtils::LoadFileToMem(fstFilePath.c_str(), &fstData, &fstDataSize) < 0) {
        DEBUG_FUNCTION_LINE("Failed to load title.fst");
        return FAILED_TO_LOAD_FILE;
    }
    InstallerService::eResults res = patchFST(fstData, fstDataSize);
    if (res != SUCCESS) {
        return res;
    }

    std::string newHash = Utils::calculateSHA1((const char *) fstData, fstDataSize);

    if (std::string(fstHash) == newHash) {
        DEBUG_FUNCTION_LINE("title.fst is compatible");
        return SUCCESS;
    } else {
        DEBUG_FUNCTION_LINE("title.fst is NOT compatible");
        return FST_HASH_MISMATCH;
    }
}

bool InstallerService::patchCOS(pugi::xml_document *doc) {
    pugi::xml_node appEntry = doc->child("app");
    appEntry.child("argstr").first_child().set_value("safe.rpx");
    appEntry.child("avail_size").first_child().set_value("00000000");
    appEntry.child("codegen_size").first_child().set_value("02000000");
    appEntry.child("codegen_core").first_child().set_value("80000001");
    appEntry.child("max_size").first_child().set_value("40000000");
    appEntry.child("max_codesize").first_child().set_value("00800000");
    for (pugi::xml_node permission: appEntry.child("permissions").children()) {
        auto mask = permission.child("mask");
        mask.first_child().set_value("FFFFFFFFFFFFFFFF");
    }
    return true;
}

std::optional<appInformation> InstallerService::getInstalledAppInformation() {
    auto mcpHandle = (int32_t) MCP_Open();
    auto titleCount = (uint32_t) MCP_TitleCount(mcpHandle);
    auto *titleList = (MCPTitleListType *) memalign(32, sizeof(MCPTitleListType) * titleCount);

    MCP_TitleListByAppType(mcpHandle, MCP_APP_TYPE_SYSTEM_APPS, &titleCount, titleList, sizeof(MCPTitleListType) * titleCount);

    MCP_Close(mcpHandle);

    DEBUG_FUNCTION_LINE("%d titles found on the WiiU", titleCount);
    bool success = false;

    int arrayLength = (sizeof(supportedApps) / sizeof(*supportedApps));
    for (uint32_t i = 0; i < titleCount; ++i) {
        for (int j = 0; j < arrayLength; ++j) {
            if (titleList[i].titleId == supportedApps[j].titleId) {
                DEBUG_FUNCTION_LINE("%s is on the Wii U (%s) %d", supportedApps[j].appName, titleList[i].path, sizeof(supportedApps[j].path));
                supportedApps[j].onTheWiiU = true;
                strncpy(supportedApps[j].path, titleList[i].path, 255);
                success = true;
                break;
            }
        }
    }
    free(titleList);

    if (success) {
        success = false;
        for (int j = 0; j < arrayLength; ++j) {
            if (supportedApps[j].onTheWiiU) {
                std::string path(supportedApps[j].path);
                if (!StringTools::replace(path, "/vol/storage_mlc01", "storage_mlc_installer:")) {
                    DEBUG_FUNCTION_LINE("Title is not on MLC. This is not expected. %s", path.c_str());
                    return {};
                }
                strncpy(supportedApps[j].path, path.c_str(), 255);
                return supportedApps[j];
            }
        }
    }
    return {};
}

InstallerService::eResults InstallerService::patchFST(uint8_t *fstData, uint32_t size) {
    auto *fstHeader = (FSTHeader *) fstData;
    if (strncmp(FSTHEADER_MAGIC, fstHeader->magic, 3) != 0) {
        DEBUG_FUNCTION_LINE("FST magic is wrong %s", fstHeader->magic);
        return InstallerService::FST_HEADER_MISMATCH;
    }

    auto numberOfSections = fstHeader->numberOfSections;
    DEBUG_FUNCTION_LINE("Found %d sections", numberOfSections);
    auto *sections = (FSTSectionEntry *) (fstData + sizeof(FSTHeader));

    auto usableSectionIndex = -1;

    for (uint32_t i = 0; i < numberOfSections; i++) {
        if (sections[i].hashMode == 2) {
            usableSectionIndex = i;
        }
    }

    if (usableSectionIndex < 0) {
        DEBUG_FUNCTION_LINE("Failed to find suitable section");
        return InstallerService::FST_NO_USABLE_SECTION_FOUND;
    } else {
        DEBUG_FUNCTION_LINE("Section %d can be used as a base", usableSectionIndex);
    }

    auto *rootEntry = (FSTNodeEntry *) (fstData + sizeof(FSTHeader) + numberOfSections * sizeof(FSTSectionEntry));
    auto numberOfNodeEntries = rootEntry->directory.lastEntryNumber;

    char *stringTableOffset = (char *) ((uint32_t) rootEntry + (sizeof(FSTNodeEntry) * numberOfNodeEntries));

    FSTNodeEntry *nodeEntries = rootEntry;
    for (uint32_t i = 0; i < numberOfNodeEntries; i++) {
        if (sections[nodeEntries[i].sectionNumber].hashMode != 2) {
            auto nameOffset = (*((uint32_t *) nodeEntries[i].nameOffset) & 0xFFFFFF00) >> 8;
            DEBUG_FUNCTION_LINE("Updating sectionNumber for %s (entry %d)", stringTableOffset + nameOffset, i);
            nodeEntries[i].sectionNumber = usableSectionIndex;
        }
    }
    return InstallerService::SUCCESS;
}

std::string InstallerService::ErrorMessage(InstallerService::eResults results) {
    if (results == SUCCESS) {
        return "Success";
    } else if (results == NO_COMPATIBLE_APP_INSTALLED) {
        return "NO_COMPATIBLE_APP_INSTALLED";
    } else if (results == FAILED_TO_COPY_FILES) {
        return "FAILED_TO_COPY_FILES";
    } else if (results == FAILED_TO_CHECK_HASH_COPIED_FILES) {
        return "FAILED_TO_CHECK_HASH_COPIED_FILES";
    } else if (results == SYSTEM_XML_INFORMATION_NOT_FOUND) {
        return "SYSTEM_XML_INFORMATION_NOT_FOUND";
    } else if (results == SYSTEM_XML_PARSING_FAILED) {
        return "SYSTEM_XML_PARSING_FAILED";
    } else if (results == SYSTEM_XML_HASH_MISMATCH_RESTORE_FAILED) {
        return "SYSTEM_XML_HASH_MISMATCH_RESTORE_FAILED";
    } else if (results == SYSTEM_XML_HASH_MISMATCH) {
        return "SYSTEM_XML_HASH_MISMATCH";
    } else if (results == RPX_HASH_MISMATCH) {
        return "RPX_HASH_MISMATCH";
    } else if (results == RPX_HASH_MISMATCH_RESTORE_FAILED) {
        return "RPX_HASH_MISMATCH_RESTORE_FAILED";
    } else if (results == COS_XML_PARSING_FAILED) {
        return "COS_XML_PARSING_FAILED";
    } else if (results == COS_XML_HASH_MISMATCH) {
        return "COS_XML_HASH_MISMATCH";
    } else if (results == COS_XML_HASH_MISMATCH_RESTORE_FAILED) {
        return "COS_XML_HASH_MISMATCH_RESTORE_FAILED";
    } else if (results == MALLOC_FAILED) {
        return "MALLOC_FAILED";
    } else if (results == FST_HASH_MISMATCH) {
        return "FST_HASH_MISMATCH";
    } else if (results == FST_HASH_MISMATCH_RESTORE_FAILED) {
        return "FST_HASH_MISMATCH_RESTORE_FAILED";
    } else if (results == FST_HEADER_MISMATCH) {
        return "FST_HEADER_MISMATCH";
    } else if (results == FST_NO_USABLE_SECTION_FOUND) {
        return "FST_NO_USABLE_SECTION_FOUND";
    }  else if (results == FAILED_TO_LOAD_FILE) {
        return "FAILED_TO_LOAD_FILE";
    } else {
        return "UNKNOWN ERROR";
    }

}
