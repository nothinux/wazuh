/**
 * @file wdb_integrity.c
 * @brief DB integrity synchronization library definition.
 * @date 2019-08-14
 *
 * @copyright Copyright (C) 2015-2021 Wazuh, Inc.
 */

/*
 * This program is a free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */

#include "cJSON.h"
#include "os_err.h"
#include "wdb.h"
#include "os_crypto/sha1/sha1_op.h"
#include <openssl/evp.h>
#include <stdarg.h>

static const char * COMPONENT_NAMES[] = {
    [WDB_FIM] = "fim",
    [WDB_FIM_FILE] = "fim_file",
    [WDB_FIM_REGISTRY] = "fim_registry",
    [WDB_SYSCOLLECTOR_PROCESSES] = "syscollector-processes",
    [WDB_SYSCOLLECTOR_PACKAGES] = "syscollector-packages",
    [WDB_SYSCOLLECTOR_HOTFIXES] = "syscollector-hotfixes",
    [WDB_SYSCOLLECTOR_PORTS] = "syscollector-ports",
    [WDB_SYSCOLLECTOR_NETPROTO] = "syscollector-netproto",
    [WDB_SYSCOLLECTOR_NETADDRESS] = "syscollector-netaddress",
    [WDB_SYSCOLLECTOR_NETINFO] = "syscollector-netinfo",
    [WDB_SYSCOLLECTOR_HWINFO] = "syscollector-hwinfo",
    [WDB_SYSCOLLECTOR_OSINFO] = "syscollector-osinfo"
};

#ifdef WAZUH_UNIT_TESTING
/* Remove static qualifier when unit testing */
#define static

/* Replace assert with mock_assert */
extern void mock_assert(const int result, const char* const expression,
                        const char * const file, const int line);
#undef assert
#define assert(expression) \
    mock_assert((int)(expression), #expression, __FILE__, __LINE__);
#endif

/**
 * @brief Run checksum of the whole result of an already prepared statement
 *
 * @param[in] wdb Database node.
 * @param[in] stmt Statement to be executed already prepared.
 * @param[in] component Name of the component.
 * @param[out] hexdigest
 * @retval 1 On success.
 * @retval 0 If no items were found.
 * @retval -1 On error.
 */
int wdb_calculate_stmt_checksum(wdb_t * wdb, sqlite3_stmt * stmt, wdb_component_t component, os_sha1 hexdigest) {

    assert(wdb != NULL);
    assert(stmt != NULL);
    assert(hexdigest != NULL);

    int step = sqlite3_step(stmt);

    if (step != SQLITE_ROW) {
        return 0;
    }

    EVP_MD_CTX * ctx = EVP_MD_CTX_create();
    EVP_DigestInit(ctx, EVP_sha1());

    for (; step == SQLITE_ROW; step = sqlite3_step(stmt)) {
        const unsigned char * checksum = sqlite3_column_text(stmt, 0);

        if (checksum == 0) {
            mdebug1("DB(%s) has a NULL %s checksum.", wdb->id, COMPONENT_NAMES[component]);
            continue;
        }

        EVP_DigestUpdate(ctx, checksum, strlen((const char *)checksum));
    }

    // Get the hex SHA-1 digest

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_size;

    EVP_DigestFinal_ex(ctx, digest, &digest_size);
    EVP_MD_CTX_destroy(ctx);
    OS_SHA1_Hexdigest(digest, hexdigest);

    return 1;
}

/**
 * @brief Run checksum of a database table
 *
 * @param[in] wdb Database node.
 * @param[in] component Name of the component.
 * @param[out] hexdigest
 * @retval 1 On success.
 * @retval 0 If no items were found.
 * @retval -1 On error.
 */
int wdbi_checksum(wdb_t * wdb, wdb_component_t component, os_sha1 hexdigest) {

    assert(wdb != NULL);
    assert(hexdigest != NULL);

    const int INDEXES[] = { [WDB_FIM] = WDB_STMT_FIM_SELECT_CHECKSUM,
                            [WDB_FIM_FILE] = WDB_STMT_FIM_FILE_SELECT_CHECKSUM,
                            [WDB_FIM_REGISTRY] = WDB_STMT_FIM_REGISTRY_SELECT_CHECKSUM,
                            [WDB_SYSCOLLECTOR_PROCESSES] = WDB_STMT_SYSCOLLECTOR_PROCESSES_SELECT_CHECKSUM,
                            [WDB_SYSCOLLECTOR_PACKAGES] = WDB_STMT_SYSCOLLECTOR_PACKAGES_SELECT_CHECKSUM,
                            [WDB_SYSCOLLECTOR_HOTFIXES] = WDB_STMT_SYSCOLLECTOR_HOTFIXES_SELECT_CHECKSUM,
                            [WDB_SYSCOLLECTOR_PORTS] = WDB_STMT_SYSCOLLECTOR_PORTS_SELECT_CHECKSUM,
                            [WDB_SYSCOLLECTOR_NETPROTO] = WDB_STMT_SYSCOLLECTOR_NETPROTO_SELECT_CHECKSUM,
                            [WDB_SYSCOLLECTOR_NETADDRESS] = WDB_STMT_SYSCOLLECTOR_NETADDRESS_SELECT_CHECKSUM,
                            [WDB_SYSCOLLECTOR_NETINFO] = WDB_STMT_SYSCOLLECTOR_NETINFO_SELECT_CHECKSUM,
                            [WDB_SYSCOLLECTOR_HWINFO] = WDB_STMT_SYSCOLLECTOR_HWINFO_SELECT_CHECKSUM,
                            [WDB_SYSCOLLECTOR_OSINFO] = WDB_STMT_SYSCOLLECTOR_OSINFO_SELECT_CHECKSUM };

    assert(component < sizeof(INDEXES) / sizeof(int));

    if (wdb_stmt_cache(wdb, INDEXES[component]) == -1) {
        return -1;
    }

    sqlite3_stmt * stmt = wdb->stmt[INDEXES[component]];

    return wdb_calculate_stmt_checksum(wdb, stmt, component, hexdigest);
}

/**
 * @brief Run checksum of a database table range
 *
 * @param[in] wdb Database node.
 * @param[in] component Name of the component.
 * @param[in] begin First element.
 * @param[in] end Last element.
 * @param[out] hexdigest
 * @retval 1 On success.
 * @retval 0 If no items were found in that range.
 * @retval -1 On error.
 */
int wdbi_checksum_range(wdb_t * wdb, wdb_component_t component, const char * begin, const char * end, os_sha1 hexdigest) {

    assert(wdb != NULL);
    assert(hexdigest != NULL);

    const int INDEXES[] = { [WDB_FIM] = WDB_STMT_FIM_SELECT_CHECKSUM_RANGE,
                            [WDB_FIM_FILE] = WDB_STMT_FIM_FILE_SELECT_CHECKSUM_RANGE,
                            [WDB_FIM_REGISTRY] = WDB_STMT_FIM_REGISTRY_SELECT_CHECKSUM_RANGE,
                            [WDB_SYSCOLLECTOR_PROCESSES] = WDB_STMT_SYSCOLLECTOR_PROCESSES_SELECT_CHECKSUM_RANGE,
                            [WDB_SYSCOLLECTOR_PACKAGES] = WDB_STMT_SYSCOLLECTOR_PACKAGES_SELECT_CHECKSUM_RANGE,
                            [WDB_SYSCOLLECTOR_HOTFIXES] = WDB_STMT_SYSCOLLECTOR_HOTFIXES_SELECT_CHECKSUM_RANGE,
                            [WDB_SYSCOLLECTOR_PORTS] = WDB_STMT_SYSCOLLECTOR_PORTS_SELECT_CHECKSUM_RANGE,
                            [WDB_SYSCOLLECTOR_NETPROTO] = WDB_STMT_SYSCOLLECTOR_NETPROTO_SELECT_CHECKSUM_RANGE,
                            [WDB_SYSCOLLECTOR_NETADDRESS] = WDB_STMT_SYSCOLLECTOR_NETADDRESS_SELECT_CHECKSUM_RANGE,
                            [WDB_SYSCOLLECTOR_NETINFO] = WDB_STMT_SYSCOLLECTOR_NETINFO_SELECT_CHECKSUM_RANGE,
                            [WDB_SYSCOLLECTOR_HWINFO] = WDB_STMT_SYSCOLLECTOR_HWINFO_SELECT_CHECKSUM_RANGE,
                            [WDB_SYSCOLLECTOR_OSINFO] = WDB_STMT_SYSCOLLECTOR_OSINFO_SELECT_CHECKSUM_RANGE };

    assert(component < sizeof(INDEXES) / sizeof(int));

    if (wdb_stmt_cache(wdb, INDEXES[component]) == -1) {
        return -1;
    }

    sqlite3_stmt * stmt = wdb->stmt[INDEXES[component]];
    sqlite3_bind_text(stmt, 1, begin, -1, NULL);
    sqlite3_bind_text(stmt, 2, end, -1, NULL);

    return wdb_calculate_stmt_checksum(wdb, stmt, component, hexdigest);
}

/**
 * @brief Delete old elements in a table
 *
 * This function shall delete every item in the corresponding table,
 * between end and tail (none of them included).
 *
 * Should tail be NULL, this function will delete every item from the first
 * element to 'begin' and from 'end' to the last element.
 *
 * @param wdb Database node.
 * @param component Name of the component.
 * @param begin First valid element in the list.
 * @param end Last valid element. This is the previous element to the first item to delete.
 * @param tail Subsequent element to the last item to delete.
 * @retval 0 On success.
 * @retval -1 On error.
 */
int wdbi_delete(wdb_t * wdb, wdb_component_t component, const char * begin, const char * end, const char * tail) {

    assert(wdb != NULL);

    const int INDEXES_AROUND[] = { [WDB_FIM] = WDB_STMT_FIM_DELETE_AROUND,
                                   [WDB_FIM_FILE] = WDB_STMT_FIM_FILE_DELETE_AROUND,
                                   [WDB_FIM_REGISTRY] = WDB_STMT_FIM_REGISTRY_DELETE_AROUND,
                                   [WDB_SYSCOLLECTOR_PROCESSES] = WDB_STMT_SYSCOLLECTOR_PROCESSES_DELETE_AROUND,
                                   [WDB_SYSCOLLECTOR_PACKAGES] = WDB_STMT_SYSCOLLECTOR_PACKAGES_DELETE_AROUND,
                                   [WDB_SYSCOLLECTOR_HOTFIXES] = WDB_STMT_SYSCOLLECTOR_HOTFIXES_DELETE_AROUND,
                                   [WDB_SYSCOLLECTOR_PORTS] = WDB_STMT_SYSCOLLECTOR_PORTS_DELETE_AROUND,
                                   [WDB_SYSCOLLECTOR_NETPROTO] = WDB_STMT_SYSCOLLECTOR_NETPROTO_DELETE_AROUND,
                                   [WDB_SYSCOLLECTOR_NETADDRESS] = WDB_STMT_SYSCOLLECTOR_NETADDRESS_DELETE_AROUND,
                                   [WDB_SYSCOLLECTOR_NETINFO] = WDB_STMT_SYSCOLLECTOR_NETINFO_DELETE_AROUND,
                                   [WDB_SYSCOLLECTOR_HWINFO] = WDB_STMT_SYSCOLLECTOR_HWINFO_DELETE_AROUND,
                                   [WDB_SYSCOLLECTOR_OSINFO] = WDB_STMT_SYSCOLLECTOR_OSINFO_DELETE_AROUND };
    const int INDEXES_RANGE[] = { [WDB_FIM] = WDB_STMT_FIM_DELETE_RANGE,
                                  [WDB_FIM_FILE] = WDB_STMT_FIM_FILE_DELETE_RANGE,
                                  [WDB_FIM_REGISTRY] = WDB_STMT_FIM_REGISTRY_DELETE_RANGE,
                                  [WDB_SYSCOLLECTOR_PROCESSES] = WDB_STMT_SYSCOLLECTOR_PROCESSES_DELETE_RANGE,
                                  [WDB_SYSCOLLECTOR_PACKAGES] = WDB_STMT_SYSCOLLECTOR_PACKAGES_DELETE_RANGE,
                                  [WDB_SYSCOLLECTOR_HOTFIXES] = WDB_STMT_SYSCOLLECTOR_HOTFIXES_DELETE_RANGE,
                                  [WDB_SYSCOLLECTOR_PORTS] = WDB_STMT_SYSCOLLECTOR_PORTS_DELETE_RANGE,
                                  [WDB_SYSCOLLECTOR_NETPROTO] = WDB_STMT_SYSCOLLECTOR_NETPROTO_DELETE_RANGE,
                                  [WDB_SYSCOLLECTOR_NETADDRESS] = WDB_STMT_SYSCOLLECTOR_NETADDRESS_DELETE_RANGE,
                                  [WDB_SYSCOLLECTOR_NETINFO] = WDB_STMT_SYSCOLLECTOR_NETINFO_DELETE_RANGE,
                                  [WDB_SYSCOLLECTOR_HWINFO] = WDB_STMT_SYSCOLLECTOR_HWINFO_DELETE_RANGE,
                                  [WDB_SYSCOLLECTOR_OSINFO] = WDB_STMT_SYSCOLLECTOR_OSINFO_DELETE_RANGE };

    assert(component < sizeof(INDEXES_AROUND) / sizeof(int));
    assert(component < sizeof(INDEXES_RANGE) / sizeof(int));

    int index = tail ? INDEXES_RANGE[component] : INDEXES_AROUND[component];

    if (wdb_stmt_cache(wdb, index) == -1) {
        return -1;
    }

    sqlite3_stmt * stmt = wdb->stmt[index];

    if (tail) {
        sqlite3_bind_text(stmt, 1, end, -1, NULL);
        sqlite3_bind_text(stmt, 2, tail, -1, NULL);
    } else {
        sqlite3_bind_text(stmt, 1, begin, -1, NULL);
        sqlite3_bind_text(stmt, 2, end, -1, NULL);
    }

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        mdebug1("DB(%s) sqlite3_step(): %s", wdb->id, sqlite3_errmsg(wdb->db));
        return -1;
    }

    return 0;
}

/**
 * @brief Update sync attempt timestamp
 *
 * Set the column "last_attempt" with the timestamp argument,
 * and increase "n_attempts" one unit (non legacy agents).
 *
 * It should be called when the syncronization with the agents is in process, or the checksum sent
 * to the manager is not the same than the one calculated locally.
 *
 * The 'legacy' flag calls internally to a different SQL statement, to avoid an overflow in the n_attempts column.
 * It happens because the old agents call this method once per row, and not once per syncronization cycle.
 *
 * @param wdb Database node.
 * @param component Name of the component.
 * @param legacy This flag is set to TRUE for agents with an old syscollector syncronization process, and FALSE otherwise.
 * @param timestamp Synchronization event timestamp (field "id");
 */

void wdbi_update_attempt(wdb_t * wdb, wdb_component_t component, long timestamp, bool legacy, os_sha1 last_agent_checksum) {

    assert(wdb != NULL);

    if (wdb_stmt_cache(wdb, legacy ? WDB_STMT_SYNC_UPDATE_ATTEMPT_LEGACY : WDB_STMT_SYNC_UPDATE_ATTEMPT) == -1) {
        return;
    }

    sqlite3_stmt * stmt = wdb->stmt[legacy ? WDB_STMT_SYNC_UPDATE_ATTEMPT_LEGACY : WDB_STMT_SYNC_UPDATE_ATTEMPT];

    sqlite3_bind_int64(stmt, 1, timestamp);
    sqlite3_bind_text(stmt, 2, last_agent_checksum, -1, NULL);
    sqlite3_bind_text(stmt, 3, COMPONENT_NAMES[component], -1, NULL);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        mdebug1("DB(%s) sqlite3_step(): %s", wdb->id, sqlite3_errmsg(wdb->db));
    }
}

/**
 * @brief Update sync completion timestamp
 *
 * Set the columns "last_attempt" and "last_completion" with the timestamp argument.
 * Increase "n_attempts" and "n_completions" one unit.
 *
 * It should be called when the syncronization with the agents is complete,
 * or the checksum sent to the manager is the same than the one calculated locally.
 *
 * @param wdb Database node.
 * @param component Name of the component.
 * @param timestamp Synchronization event timestamp (field "id");
 */

void wdbi_update_completion(wdb_t * wdb, wdb_component_t component, long timestamp, os_sha1 last_agent_checksum) {

    assert(wdb != NULL);

    if (wdb_stmt_cache(wdb, WDB_STMT_SYNC_UPDATE_COMPLETION) == -1) {
        return;
    }

    sqlite3_stmt * stmt = wdb->stmt[WDB_STMT_SYNC_UPDATE_COMPLETION];

    sqlite3_bind_int64(stmt, 1, timestamp);
    sqlite3_bind_int64(stmt, 2, timestamp);
    sqlite3_bind_text(stmt, 3, last_agent_checksum, -1, NULL);
    sqlite3_bind_text(stmt, 4, COMPONENT_NAMES[component], -1, NULL);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        mdebug1("DB(%s) sqlite3_step(): %s", wdb->id, sqlite3_errmsg(wdb->db));
    }
}

// Query the checksum of a data range
int wdbi_query_checksum(wdb_t * wdb, wdb_component_t component, const char * command, const char * payload) {
    int retval = -1;
    cJSON * data = cJSON_Parse(payload);

    if (data == NULL) {
        mdebug1("DB(%s): cannot parse checksum range payload: '%s'", wdb->id, payload);
        return -1;
    }

    cJSON * item = cJSON_GetObjectItem(data, "begin");
    char * begin = cJSON_GetStringValue(item);

    if (begin == NULL) {
        mdebug1("No such string 'begin' in JSON payload.");
        goto end;
    }

    item = cJSON_GetObjectItem(data, "end");
    char * end = cJSON_GetStringValue(item);

    if (end == NULL) {
        mdebug1("No such string 'end' in JSON payload.");
        goto end;
    }

    item = cJSON_GetObjectItem(data, "checksum");
    char * checksum = cJSON_GetStringValue(item);

    if (checksum == NULL) {
        mdebug1("No such string 'checksum' in JSON payload.");
        goto end;
    }

    item = cJSON_GetObjectItem(data, "id");

    if (!cJSON_IsNumber(item)) {
        mdebug1("No such string 'id' in JSON payload.");
        goto end;
    }

    long timestamp = item->valuedouble;
    os_sha1 hexdigest;
    struct timespec ts_start, ts_end;
    gettime(&ts_start);

    switch (wdbi_checksum_range(wdb, component, begin, end, hexdigest)) {
    case -1:
        goto end;

    case 0:
        retval = 0;
        break;

    case 1:
        gettime(&ts_end);
        mdebug2("Agent '%s' %s range checksum: Time: %.3f ms.", wdb->id, COMPONENT_NAMES[component], time_diff(&ts_start, &ts_end) * 1e3);
        retval = strcmp(hexdigest, checksum) ? 1 : 2;
    }

    // Remove old elements

    if (strcmp(command, "integrity_check_global") == 0) {
        wdbi_delete(wdb, component, begin, end, NULL);

        // Update synchronization timestamp

        switch (retval) {
        case 0: // No data
        case 1: // Checksum failure
            wdbi_update_attempt(wdb, component, timestamp, FALSE, checksum);
            break;

        case 2: // Data is synchronized
            wdbi_update_completion(wdb, component, timestamp, checksum);
        }

    } else if (strcmp(command, "integrity_check_left") == 0) {
        item = cJSON_GetObjectItem(data, "tail");
        wdbi_delete(wdb, component, begin, end, cJSON_GetStringValue(item));
    }

end:
    cJSON_Delete(data);
    return retval;
}

// Query a complete table clear
int wdbi_query_clear(wdb_t * wdb, wdb_component_t component, const char * payload) {
    const int INDEXES[] = { [WDB_FIM] = WDB_STMT_FIM_CLEAR,
                            [WDB_FIM_FILE] = WDB_STMT_FIM_FILE_CLEAR,
                            [WDB_FIM_REGISTRY] = WDB_STMT_FIM_REGISTRY_CLEAR,
                            [WDB_SYSCOLLECTOR_PROCESSES] = WDB_STMT_SYSCOLLECTOR_PROCESSES_CLEAR,
                            [WDB_SYSCOLLECTOR_PACKAGES] = WDB_STMT_SYSCOLLECTOR_PACKAGES_CLEAR,
                            [WDB_SYSCOLLECTOR_HOTFIXES] = WDB_STMT_SYSCOLLECTOR_HOTFIXES_CLEAR,
                            [WDB_SYSCOLLECTOR_PORTS] = WDB_STMT_SYSCOLLECTOR_PORTS_CLEAR,
                            [WDB_SYSCOLLECTOR_NETPROTO] = WDB_STMT_SYSCOLLECTOR_NETPROTO_CLEAR,
                            [WDB_SYSCOLLECTOR_NETADDRESS] = WDB_STMT_SYSCOLLECTOR_NETADDRESS_CLEAR,
                            [WDB_SYSCOLLECTOR_NETINFO] = WDB_STMT_SYSCOLLECTOR_NETINFO_CLEAR,
                            [WDB_SYSCOLLECTOR_HWINFO] = WDB_STMT_SYSCOLLECTOR_HWINFO_CLEAR,
                            [WDB_SYSCOLLECTOR_OSINFO] = WDB_STMT_SYSCOLLECTOR_OSINFO_CLEAR };

    assert(component < sizeof(INDEXES) / sizeof(int));

    int retval = -1;
    cJSON * data = cJSON_Parse(payload);

    if (data == NULL) {
        mdebug1("DB(%s): cannot parse checksum range payload: '%s'", wdb->id, payload);
        goto end;
    }

    cJSON * item = cJSON_GetObjectItem(data, "id");

    if (!cJSON_IsNumber(item)) {
        mdebug1("No such string 'id' in JSON payload.");
        goto end;
    }

    item = cJSON_GetObjectItem(data, "checksum");
    char * checksum = cJSON_GetStringValue(item);

    if (checksum == NULL) {
        mdebug1("No such string 'checksum' in JSON payload.");
        goto end;
    }

    long timestamp = item->valuedouble;

    if (wdb_stmt_cache(wdb, INDEXES[component]) == -1) {
        goto end;
    }

    sqlite3_stmt * stmt = wdb->stmt[INDEXES[component]];

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        mdebug1("DB(%s) sqlite3_step(): %s", wdb->id, sqlite3_errmsg(wdb->db));
        goto end;
    }

    wdbi_update_completion(wdb, component, timestamp, checksum);
    retval = 0;

end:
    cJSON_Delete(data);
    return retval;
}

 // Calculates SHA1 hash from a NULL terminated string array
 int wdbi_array_hash(const char ** strings_to_hash, os_sha1 hexdigest)
 {
    size_t it = 0;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_size;
    int ret_val = OS_SUCCESS;

    EVP_MD_CTX * ctx = EVP_MD_CTX_create();
    if (!ctx) {
        mdebug2("Failed during hash context creation");
        return OS_INVALID;
    }

    if (1 != EVP_DigestInit(ctx, EVP_sha1()) ) {
        mdebug2("Failed during hash context initialization");
        EVP_MD_CTX_destroy(ctx);
        return OS_INVALID;
    }

    if (strings_to_hash) {
        while(strings_to_hash[it]) {
            if (1 != EVP_DigestUpdate(ctx, strings_to_hash[it], strlen(strings_to_hash[it])) ) {
                mdebug2("Failed during hash context update");
                ret_val = OS_INVALID;
                break;
            }
            it++;
        }
    }

    EVP_DigestFinal_ex(ctx, digest, &digest_size);
    EVP_MD_CTX_destroy(ctx);
    if (ret_val != OS_INVALID) {
        OS_SHA1_Hexdigest(digest, hexdigest);
    }

    return ret_val;
 }

 // Calculates SHA1 hash from a set of strings as parameters, with NULL as end
 int wdbi_strings_hash(os_sha1 hexdigest, ...)
 {
    char* parameter = NULL;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_size;
    int ret_val = OS_SUCCESS;
    va_list parameters;

    EVP_MD_CTX * ctx = EVP_MD_CTX_create();
    if (!ctx) {
        mdebug2("Failed during hash context creation");
        return OS_INVALID;
    }

    if (1 != EVP_DigestInit(ctx, EVP_sha1()) ) {
        mdebug2("Failed during hash context initialization");
        EVP_MD_CTX_destroy(ctx);
        return OS_INVALID;
    }

    va_start(parameters, hexdigest);

    while(parameter = va_arg(parameters, char*), parameter) {
        if (1 != EVP_DigestUpdate(ctx, parameter, strlen(parameter)) ) {
            mdebug2("Failed during hash context update");
            ret_val = OS_INVALID;
            break;
        }
    }
    va_end(parameters);

    EVP_DigestFinal_ex(ctx, digest, &digest_size);
    EVP_MD_CTX_destroy(ctx);
    if (ret_val != OS_INVALID) {
        OS_SHA1_Hexdigest(digest, hexdigest);
    }

    return ret_val;
 }

/**
 * @brief Returns the syncronization status of a component from sync_info table.
 *
 * @param [in] wdb The 'agents' struct database.
 * @param [in] component An enumeration member that was previously added to the table.
 * @return Returns 0 if data is not ready, 1 if it is, or -1 on error.
 */
int wdbi_check_sync_status(wdb_t *wdb, wdb_component_t component) {
    cJSON* j_sync_info = NULL;
    int result = OS_INVALID;

    if (wdb_stmt_cache(wdb, WDB_STMT_SYNC_GET_INFO) == -1) {
        mdebug1("Cannot cache statement");
        return OS_INVALID;
    }

    sqlite3_stmt * stmt = wdb->stmt[WDB_STMT_SYNC_GET_INFO];
    sqlite3_bind_text(stmt, 1, COMPONENT_NAMES[component], -1, NULL);

    j_sync_info = wdb_exec_stmt(stmt);

    if (!j_sync_info) {
        mdebug1("wdb_exec_stmt(): %s", sqlite3_errmsg(wdb->db));
        return OS_INVALID;
    }

    cJSON* j_last_attempt = cJSON_GetObjectItem(j_sync_info->child, "last_attempt");
    cJSON* j_last_completion = cJSON_GetObjectItem(j_sync_info->child, "last_completion");
    cJSON* j_checksum = cJSON_GetObjectItem(j_sync_info->child, "last_global_checksum");

    if ( cJSON_IsNumber(j_last_attempt) && cJSON_IsNumber(j_last_completion) && cJSON_IsString(j_checksum)) {
        int last_attempt = j_last_attempt->valueint;
        int last_completion = j_last_completion->valueint;

        // Return 0 if there was not at least one successful syncronization or
        // if the syncronization is in process or there was an error iun the syncronization
        if (result = (last_completion != 0 && last_attempt <= last_completion ), result) {
            // Verifying the integrity checksum
            os_sha1 hexdigest;
            char *checksum;

            switch (wdbi_checksum(wdb, component, hexdigest)) {
            case -1:
                result = OS_INVALID;
                break;

            case 0:
                result = 0;
                break;

            case 1:
                checksum = cJSON_GetStringValue(j_checksum);
                result = !strcmp(hexdigest, checksum);
            }
        }
    } else {
        mdebug1("Failed to get agent's sync status data");
        result = OS_INVALID;
    }

    cJSON_Delete(j_sync_info);
    return result;
}
