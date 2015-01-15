/**
 * @file db.c Database routines for ndo2db daemon
 */
/*
 * Copyright 2009-2014 Nagios Core Development Team and Community Contributors
 * Copyright 2005-2009 Ethan Galstad
 *
 * This file is part of NDOUtils.
 *
 * NDOUtils is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NDOUtils is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with NDOUtils. If not, see <http://www.gnu.org/licenses/>.
 */

/* Headers from our project. */
#include "../include/config.h"
#include "../include/common.h"
#include "../include/io.h"
#include "../include/utils.h"
#include "../include/protoapi.h"
#include "../include/ndo2db.h"
#include "../include/dbhandlers.h"
#include "../include/dbstatements.h"
#include "../include/db.h"


/* Our program-wide DB settings (ndo2db.c). */
extern ndo2db_dbconfig ndo2db_db_settings;
/* Last time we updated our connection status in the DB (ndo2db.c). */
extern time_t ndo2db_db_last_checkin_time;

const char *ndo2db_db_rawtablenames[NDO2DB_NUM_DBTABLES] = {
	"instances",
	"conninfo",
	"objects",
	"objecttypes",
	"logentries",
	"systemcommands",
	"eventhandlers",
	"servicechecks",
	"hostchecks",
	"programstatus",
	"externalcommands",
	"servicestatus",
	"hoststatus",
	"processevents",
	"timedevents",
	"timedeventqueue",
	"flappinghistory",
	"commenthistory",
	"comments",
	"notifications",
	"contactnotifications",
	"contactnotificationmethods",
	"acknowledgements",
	"statehistory",
	"downtimehistory",
	"scheduleddowntime",
	"configfiles",
	"configfilevariables",
	"runtimevariables",
	"contactstatus",
	"customvariablestatus",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"commands",
	"timeperiods",
	"timeperiod_timeranges",
	"contactgroups",
	"contactgroup_members",
	"hostgroups",
	"hostgroup_members",
	"servicegroups",
	"servicegroup_members",
	"hostescalations",
	"hostescalation_contacts",
	"serviceescalations",
	"serviceescalation_contacts",
	"hostdependencies",
	"servicedependencies",
	"contacts",
	"contact_addresses",
	"contact_notificationcommands",
	"hosts",
	"host_parenthosts",
	"host_contacts",
	"services",
	"service_contacts",
	"customvariables",
	"host_contactgroups",
	"service_contactgroups",
	"hostescalation_contactgroups",
	"serviceescalation_contactgroups",
	"service_parentservices",
};

/* Our prefixed table names. */
char *ndo2db_db_tablenames[NDO2DB_NUM_DBTABLES];


/* #define DEBUG_NDO2DB_QUERIES 1 */

/****************************************************************************/
/* CONNECTION FUNCTIONS                                                     */
/****************************************************************************/

/* Initialize database structures. */
int ndo2db_db_init(ndo2db_idi *idi) {
	int x;

	if (!idi) return NDO_ERROR;

	/* Set our DB server type. */
	idi->dbinfo.server_type = ndo2db_db_settings.server_type;

	/* Prepare prefixed table names. */
	for (x = 0; x < NDO2DB_NUM_DBTABLES; x++) {
		int r = asprintf(ndo2db_db_tablenames + x, "%s%s",
				ndo2db_db_settings.dbprefix ? ndo2db_db_settings.dbprefix : "",
				ndo2db_db_rawtablenames[x]);
		if (r == -1) return NDO_ERROR;
	}

	/* Initialize scalar variables. */
	idi->dbinfo.connected = NDO_FALSE;
	idi->dbinfo.error = NDO_FALSE;
	idi->dbinfo.instance_id = 0;
	idi->dbinfo.conninfo_id = 0;
	idi->dbinfo.latest_program_status_time = 0;
	idi->dbinfo.latest_host_status_time = 0;
	idi->dbinfo.latest_service_status_time = 0;
	idi->dbinfo.latest_queued_event_time = 0;
	idi->dbinfo.latest_realtime_data_time = 0;
	idi->dbinfo.latest_comment_time = 0;
	idi->dbinfo.clean_event_queue = NDO_FALSE;
	idi->dbinfo.last_notification_id = 0;
	idi->dbinfo.last_contact_notification_id = 0;
	idi->dbinfo.max_timedevents_age = ndo2db_db_settings.max_timedevents_age;
	idi->dbinfo.max_systemcommands_age = ndo2db_db_settings.max_systemcommands_age;
	idi->dbinfo.max_servicechecks_age = ndo2db_db_settings.max_servicechecks_age;
	idi->dbinfo.max_hostchecks_age = ndo2db_db_settings.max_hostchecks_age;
	idi->dbinfo.max_eventhandlers_age = ndo2db_db_settings.max_eventhandlers_age;
	idi->dbinfo.max_externalcommands_age = ndo2db_db_settings.max_externalcommands_age;
	idi->dbinfo.max_notifications_age = ndo2db_db_settings.max_notifications_age;
	idi->dbinfo.max_contactnotifications_age = ndo2db_db_settings.max_contactnotifications_age;
	idi->dbinfo.max_contactnotificationmethods_age = ndo2db_db_settings.max_contactnotificationmethods_age;
	idi->dbinfo.max_logentries_age = ndo2db_db_settings.max_logentries_age;
	idi->dbinfo.max_acknowledgements_age = ndo2db_db_settings.max_acknowledgements_age;
	idi->dbinfo.table_trim_interval = ndo2db_db_settings.table_trim_interval;
	idi->dbinfo.last_table_trim_time = 0;
	idi->dbinfo.last_logentry_time = 0;
	idi->dbinfo.last_logentry_data = NULL;

	/* Initialize our low level DB interface. */
	if (!mysql_init(&idi->dbinfo.mysql_conn)) {
		syslog(LOG_USER|LOG_INFO, "Error: mysql_init() failed.");
		return NDO_ERROR;
	}

	return NDO_OK;
}


/* Clean up database structures. */
int ndo2db_db_deinit(ndo2db_idi *idi) {
	int x;

	if (!idi) return NDO_ERROR;

	/* Free our prefixed table names. */
	for (x = 0; x < NDO2DB_NUM_DBTABLES; x++) my_free(ndo2db_db_tablenames[x]);

	/* Free our object id cache. */
	ndo2db_free_obj_cache();

	return NDO_OK;
}


/* Connects to the database server. */
int ndo2db_db_connect(ndo2db_idi *idi) {
	int result = NDO_OK;

	if (!idi) return NDO_ERROR;

	/* Okay if we're already connected... */
	if (idi->dbinfo.connected) return NDO_OK;

	if (!mysql_real_connect(
			&idi->dbinfo.mysql_conn,
			ndo2db_db_settings.host,
			ndo2db_db_settings.username,
			ndo2db_db_settings.password,
			ndo2db_db_settings.dbname,
			ndo2db_db_settings.port,
			ndo2db_db_settings.socket,
			CLIENT_REMEMBER_OPTIONS
	)) {
		mysql_close(&idi->dbinfo.mysql_conn);
		syslog(LOG_USER|LOG_INFO, "Error: Could not connect to MySQL database: %s",
				mysql_error(&idi->dbinfo.mysql_conn));
		result = NDO_ERROR;
		idi->disconnect_client = NDO_TRUE;
	}
	else {
		idi->dbinfo.connected = NDO_TRUE;
		syslog(LOG_USER|LOG_DEBUG, "Successfully connected to MySQL database");
	}

	return result;
}


/* Disconnects from the database server. */
int ndo2db_db_disconnect(ndo2db_idi *idi) {

	if (!idi) return NDO_ERROR;

	/* Okay if we're not connected... */
	if (!idi->dbinfo.connected) return NDO_OK;

	mysql_close(&idi->dbinfo.mysql_conn);
	idi->dbinfo.connected = NDO_FALSE;
	syslog(LOG_USER|LOG_DEBUG, "Successfully disconnected from MySQL database");

	return NDO_OK;
}


/* Post-connect routines. */
int ndo2db_db_hello(ndo2db_idi *idi) {
	char *buf = NULL;
	int result = NDO_OK;
	int have_instance = NDO_FALSE;
	time_t current_time;

	/* Make sure we have an instance name. */
	if (!idi->instance_name) idi->instance_name = strdup("default");

	/* Try to get existing instance id for our instance name. */
	if (asprintf(&buf, "SELECT instance_id FROM %s WHERE instance_name='%s'",
			ndo2db_db_tablenames[NDO2DB_DBTABLE_INSTANCES], idi->instance_name) == -1) {
		buf = NULL;
	}
	if ((result = ndo2db_db_query(idi, buf)) == NDO_OK) {
		idi->dbinfo.mysql_result = mysql_store_result(&idi->dbinfo.mysql_conn);
		idi->dbinfo.mysql_row = mysql_fetch_row(idi->dbinfo.mysql_result);
		if (idi->dbinfo.mysql_row) {
			ndo2db_strtoul(idi->dbinfo.mysql_row[0], &idi->dbinfo.instance_id);
			have_instance = NDO_TRUE;
		}
		mysql_free_result(idi->dbinfo.mysql_result);
		idi->dbinfo.mysql_result = NULL;
	}
	free(buf);

	/* Insert a new instance if needed. */
	if (!have_instance) {
		if (asprintf(&buf, "INSERT INTO %s SET instance_name='%s'",
				ndo2db_db_tablenames[NDO2DB_DBTABLE_INSTANCES], idi->instance_name) == -1) {
			buf = NULL;
		}
		if ((result = ndo2db_db_query(idi, buf)) == NDO_OK) {
			idi->dbinfo.instance_id = mysql_insert_id(&idi->dbinfo.mysql_conn);
		}
		free(buf);
	}

	/* Record initial connection information. */
	if (asprintf(&buf, "INSERT INTO %s SET instance_id=%lu, connect_time=NOW(), "
					"last_checkin_time=NOW(), bytes_processed=0, lines_processed=0, "
					"entries_processed=0, agent_name='%s', agent_version='%s', "
					"disposition='%s', connect_source='%s', connect_type='%s', "
					"data_start_time="NDO2DB_PRI_TIME_AS_DATE,
			ndo2db_db_tablenames[NDO2DB_DBTABLE_CONNINFO],
			idi->dbinfo.instance_id,
			idi->agent_name,
			idi->agent_version,
			idi->disposition,
			idi->connect_source,
			idi->connect_type,
			idi->data_start_time) == -1) {
		buf = NULL;
	}
	if ((result = ndo2db_db_query(idi, buf)) == NDO_OK) {
		idi->dbinfo.conninfo_id = mysql_insert_id(&idi->dbinfo.mysql_conn);
	}
	free(buf);

	/* Initialize our prepared statements now that we're connected and have an
	 * instance_id. */
	ndo2db_stmt_init_stmts(idi);
	/* get cached object ids... */
	ndo2db_load_obj_cache(idi);

	/* get latest times from various tables... */
	ndo2db_db_get_latest_data_time(idi, ndo2db_db_tablenames[NDO2DB_DBTABLE_PROGRAMSTATUS], "status_update_time", (unsigned long *)&idi->dbinfo.latest_program_status_time);
	ndo2db_db_get_latest_data_time(idi, ndo2db_db_tablenames[NDO2DB_DBTABLE_HOSTSTATUS], "status_update_time", (unsigned long *)&idi->dbinfo.latest_host_status_time);
	ndo2db_db_get_latest_data_time(idi, ndo2db_db_tablenames[NDO2DB_DBTABLE_SERVICESTATUS], "status_update_time", (unsigned long *)&idi->dbinfo.latest_service_status_time);
	ndo2db_db_get_latest_data_time(idi, ndo2db_db_tablenames[NDO2DB_DBTABLE_CONTACTSTATUS], "status_update_time", (unsigned long *)&idi->dbinfo.latest_contact_status_time);
	ndo2db_db_get_latest_data_time(idi, ndo2db_db_tablenames[NDO2DB_DBTABLE_TIMEDEVENTQUEUE], "queued_time", (unsigned long *)&idi->dbinfo.latest_queued_event_time);
	ndo2db_db_get_latest_data_time(idi, ndo2db_db_tablenames[NDO2DB_DBTABLE_COMMENTS], "entry_time", (unsigned long *)&idi->dbinfo.latest_comment_time);

	/* calculate time of latest realtime data */
	idi->dbinfo.latest_realtime_data_time = 0;
	if (idi->dbinfo.latest_realtime_data_time < idi->dbinfo.latest_program_status_time) {
		idi->dbinfo.latest_realtime_data_time = idi->dbinfo.latest_program_status_time;
	}
	if (idi->dbinfo.latest_realtime_data_time < idi->dbinfo.latest_host_status_time) {
		idi->dbinfo.latest_realtime_data_time = idi->dbinfo.latest_host_status_time;
	}
	if (idi->dbinfo.latest_realtime_data_time < idi->dbinfo.latest_service_status_time) {
		idi->dbinfo.latest_realtime_data_time = idi->dbinfo.latest_service_status_time;
	}
	if (idi->dbinfo.latest_realtime_data_time < idi->dbinfo.latest_contact_status_time) {
		idi->dbinfo.latest_realtime_data_time = idi->dbinfo.latest_contact_status_time;
	}
	if (idi->dbinfo.latest_realtime_data_time < idi->dbinfo.latest_queued_event_time) {
		idi->dbinfo.latest_realtime_data_time = idi->dbinfo.latest_queued_event_time;
	}

	/* get current time */
	/* make sure latest time stamp isn't in the future - this will cause problems if a backwards system time change occurs */
	time(&current_time);
	if (idi->dbinfo.latest_realtime_data_time > current_time) {
		idi->dbinfo.latest_realtime_data_time = current_time;
	}

	/* set flags to clean event queue, etc. */
	idi->dbinfo.clean_event_queue = NDO_TRUE;

	/* set misc data */
	idi->dbinfo.last_notification_id = 0;
	idi->dbinfo.last_contact_notification_id = 0;

	return result;
}


/* pre-disconnect routines */
int ndo2db_db_goodbye(ndo2db_idi *idi) {
	int result = NDO_OK;
	char *buf = NULL;

	/* record last connection information */
	if (asprintf(&buf, "UPDATE %s SET disconnect_time=NOW(), "
					"last_checkin_time=NOW(), data_end_time="NDO2DB_PRI_TIME_AS_DATE", "
					"bytes_processed=%lu, lines_processed=%lu, entries_processed=%lu "
					"WHERE conninfo_id=%lu",
			ndo2db_db_tablenames[NDO2DB_DBTABLE_CONNINFO],
			idi->data_end_time,
			idi->bytes_processed,
			idi->lines_processed,
			idi->entries_processed,
			idi->dbinfo.conninfo_id) == -1) {
		buf = NULL;
	}
	result = ndo2db_db_query(idi, buf);
	free(buf);

	return result;
}


/* checking routines */
int ndo2db_db_checkin(ndo2db_idi *idi) {
	int result = NDO_OK;
	char *buf = NULL;

	/* record last connection information */
	if (asprintf(&buf, "UPDATE %s SET last_checkin_time=NOW(), bytes_processed='%lu', lines_processed='%lu', entries_processed='%lu' WHERE conninfo_id='%lu'",
			ndo2db_db_tablenames[NDO2DB_DBTABLE_CONNINFO],
			idi->bytes_processed,
			idi->lines_processed,
			idi->entries_processed,
			idi->dbinfo.conninfo_id) == -1) {
		buf = NULL;
	}
	result = ndo2db_db_query(idi, buf);
	free(buf);

	time(&ndo2db_db_last_checkin_time);

	return result;
}



/****************************************************************************/
/* MISC FUNCTIONS                                                           */
/****************************************************************************/

/* escape a string for a SQL statement */
char *ndo2db_db_escape_string(ndo2db_idi *idi, char *buf) {
	int x;
	int y;
	int z;
	char *newbuf = NULL;

	if (!idi || !buf) return NULL;

	z = strlen(buf);

	/* allocate space for the new string */
	newbuf = malloc((z * 2) + 1);
	if (!newbuf) return NULL;

	/* escape characters */
	for (x = 0, y = 0; x < z; x++) {

		if (buf[x] == '\'' || buf[x] == '\"' || buf[x] == '*' || buf[x] == '\\'
				|| buf[x] == '$' || buf[x] == '?' || buf[x] == '.' || buf[x] == '^'
				|| buf[x] == '+' || buf[x] == '[' || buf[x] == ']' || buf[x] == '('
				|| buf[x] == ')') {
			newbuf[y++] = '\\';
		}
		newbuf[y++] = buf[x];
	}

	/* terminate escape string */
	newbuf [y] = '\0';

	return newbuf;
}


/* SQL query conversion of time_t format to date/time format */
char *ndo2db_db_timet_to_sql(ndo2db_idi *idi, time_t t) {
	char *buf = NULL;
	(void)idi; /* Unused, don't warn. */

	asprintf(&buf, "FROM_UNIXTIME(%lu)", (unsigned long)t);

	return buf;
}


/* Executes a SQL statement. */
int ndo2db_db_query(ndo2db_idi *idi, char *buf) {
	int result = NDO_OK;

	if (!idi || !buf) return NDO_ERROR;

	/* If we're not connected, try and reconnect... */
	if (!idi->dbinfo.connected) {
		if (ndo2db_db_connect(idi) == NDO_ERROR || !idi->dbinfo.connected) return NDO_ERROR;
		ndo2db_db_hello(idi);
	}

#ifdef DEBUG_NDO2DB_QUERIES
	printf("%s\n\n", buf);
#endif

	ndo2db_log_debug_info(NDO2DB_DEBUGL_SQL, 0, "%s\n", buf);

	if (mysql_query(&idi->dbinfo.mysql_conn, buf)) {
		syslog(LOG_USER|LOG_ERR, "Error: mysql_query() failed for '%s'", buf);
		syslog(LOG_USER|LOG_ERR, "mysql_error: %s", mysql_error(&idi->dbinfo.mysql_conn));
		result = NDO_ERROR;
	}

	/* handle errors */
	if (result != NDO_OK) ndo2db_handle_db_error(idi);

	return result;
}


/* handles SQL query errors */
int ndo2db_handle_db_error(ndo2db_idi *idi) {
	int result = 0;

	if (!idi) return NDO_ERROR;

	/* we're not currently connected... */
	if (!idi->dbinfo.connected) return NDO_OK;

	result = mysql_errno(&idi->dbinfo.mysql_conn);
	if (result == CR_SERVER_LOST || result == CR_SERVER_GONE_ERROR) {
		syslog(LOG_USER|LOG_INFO, "Error: Connection to MySQL database has been lost!\n");
		ndo2db_db_disconnect(idi);
		idi->disconnect_client = NDO_TRUE;
	}

	return NDO_OK;
}


/* clears data from a given table (current instance only) */
int ndo2db_db_clear_table(ndo2db_idi *idi, char *table_name) {
	char *buf = NULL;
	int result = NDO_OK;

	if (!idi || !table_name) return NDO_ERROR;

	if (asprintf(&buf, "DELETE FROM %s WHERE instance_id='%lu'",
			table_name,
			idi->dbinfo.instance_id) == -1) {
		buf = NULL;
	}
	result = ndo2db_db_query(idi, buf);
	free(buf);

	return result;
}


/* gets latest data time value from a given table */
int ndo2db_db_get_latest_data_time(
		ndo2db_idi *idi,
		const char *table_name,
		const char *field_name,
		unsigned long *t
) {
	char *buf = NULL;
	int result = NDO_OK;

	if (!idi || !table_name || !field_name || !t) return NDO_ERROR;

	*t = 0;

	if (asprintf(&buf, "SELECT "NDO2DB_PRI_DATE_AS_TIME" AS latest_time FROM %s WHERE instance_id=%lu "
					"ORDER BY %s DESC LIMIT 0,1",
			field_name,
			table_name,
			idi->dbinfo.instance_id,
			field_name) == -1) {
		buf = NULL;
	}
	if ((result = ndo2db_db_query(idi, buf)) == NDO_OK) {
		idi->dbinfo.mysql_result = mysql_store_result(&idi->dbinfo.mysql_conn);
		idi->dbinfo.mysql_row = mysql_fetch_row(idi->dbinfo.mysql_result);
		if (idi->dbinfo.mysql_row) {
			ndo2db_strtoul(idi->dbinfo.mysql_row[0], t);
		}
		mysql_free_result(idi->dbinfo.mysql_result);
		idi->dbinfo.mysql_result = NULL;
	}
	free(buf);

	return result;
}


/**
 * Delete old rows from a DB table.
 * @param idi Input data interface handle.
 * @param table_name Table name to delete from.
 * @param field_name Time field name to delete by.
 * @param t Delete rows older than this timestamp.
 * @return NDO_OK if the operation succeeded, otherwise NDO_ERROR.
 */
static int ndo2db_db_trim_data_table(
	ndo2db_idi *idi,
	const char *table_name,
	const char *field_name,
	time_t t
) {
	char *buf;
	int result = NDO_ERROR;

	if (!idi || !table_name || !field_name) return NDO_ERROR;

	if (asprintf(&buf, "DELETE FROM %s WHERE instance_id=%lu "
					"AND %s<"NDO2DB_PRI_TIME_AS_DATE,
			table_name, idi->dbinfo.instance_id, field_name, (unsigned long)t) != -1
	) {
		result = ndo2db_db_query(idi, buf);
		free(buf);
	}

	return result;
}


/**
 * Periodically removes old rows from tables that would otherwise grow without
 * bound.
 * @param idi Input data interface handle.
 * @return NDO_OK.
 */
int ndo2db_db_perform_maintenance(ndo2db_idi *idi) {
	time_t current_time = time(NULL);
	time_t delta = current_time - idi->dbinfo.last_table_trim_time;

	if (idi->dbinfo.table_trim_interval && delta > (time_t)idi->dbinfo.table_trim_interval) {
		if (idi->dbinfo.max_timedevents_age) {
			syslog(LOG_USER|LOG_INFO, "Trimming timedevents.");
			ndo2db_db_trim_data_table(idi, ndo2db_db_tablenames[NDO2DB_DBTABLE_TIMEDEVENTS], "scheduled_time", current_time - (time_t)idi->dbinfo.max_timedevents_age);
		}
		if (idi->dbinfo.max_systemcommands_age) {
			syslog(LOG_USER|LOG_INFO, "Trimming systemcommands.");
			ndo2db_db_trim_data_table(idi, ndo2db_db_tablenames[NDO2DB_DBTABLE_SYSTEMCOMMANDS], "start_time", current_time - (time_t)idi->dbinfo.max_systemcommands_age);
		}
		if (idi->dbinfo.max_servicechecks_age) {
			syslog(LOG_USER|LOG_INFO, "Trimming servicechecks.");
			ndo2db_db_trim_data_table(idi, ndo2db_db_tablenames[NDO2DB_DBTABLE_SERVICECHECKS], "start_time", current_time - (time_t)idi->dbinfo.max_servicechecks_age);
		}
		if (idi->dbinfo.max_hostchecks_age) {
			syslog(LOG_USER|LOG_INFO, "Trimming hostchecks.");
			ndo2db_db_trim_data_table(idi, ndo2db_db_tablenames[NDO2DB_DBTABLE_HOSTCHECKS], "start_time", current_time - (time_t)idi->dbinfo.max_hostchecks_age);
		}
		if (idi->dbinfo.max_eventhandlers_age) {
			syslog(LOG_USER|LOG_INFO, "Trimming eventhandlers.");
			ndo2db_db_trim_data_table(idi, ndo2db_db_tablenames[NDO2DB_DBTABLE_EVENTHANDLERS], "start_time", current_time - (time_t)idi->dbinfo.max_eventhandlers_age);
		}
		if (idi->dbinfo.max_externalcommands_age) {
			syslog(LOG_USER|LOG_INFO, "Trimming externalcommands.");
			ndo2db_db_trim_data_table(idi, ndo2db_db_tablenames[NDO2DB_DBTABLE_EXTERNALCOMMANDS], "entry_time", current_time - (time_t)idi->dbinfo.max_externalcommands_age);
		}
		if (idi->dbinfo.max_notifications_age) {
			syslog(LOG_USER|LOG_INFO, "Trimming notifications.");
			ndo2db_db_trim_data_table(idi, ndo2db_db_tablenames[NDO2DB_DBTABLE_NOTIFICATIONS], "start_time", current_time - (time_t)idi->dbinfo.max_notifications_age);
		}
		if (idi->dbinfo.max_contactnotifications_age) {
			syslog(LOG_USER|LOG_INFO, "Trimming contactnotifications.");
			ndo2db_db_trim_data_table(idi, ndo2db_db_tablenames[NDO2DB_DBTABLE_CONTACTNOTIFICATIONS], "start_time", current_time - (time_t)idi->dbinfo.max_contactnotifications_age);
		}
		if (idi->dbinfo.max_contactnotificationmethods_age) {
			syslog(LOG_USER|LOG_INFO, "Trimming contactnotificationmethods.");
			ndo2db_db_trim_data_table(idi, ndo2db_db_tablenames[NDO2DB_DBTABLE_CONTACTNOTIFICATIONMETHODS], "start_time", current_time - (time_t)idi->dbinfo.max_contactnotificationmethods_age);
		}
		if (idi->dbinfo.max_logentries_age) {
			syslog(LOG_USER|LOG_INFO, "Trimming logentries.");
			ndo2db_db_trim_data_table(idi, ndo2db_db_tablenames[NDO2DB_DBTABLE_LOGENTRIES], "entry_time", current_time - (time_t)idi->dbinfo.max_logentries_age);
		}
		if (idi->dbinfo.max_acknowledgements_age) {
			syslog(LOG_USER|LOG_INFO, "Trimming acknowledgements.");
			ndo2db_db_trim_data_table(idi, ndo2db_db_tablenames[NDO2DB_DBTABLE_ACKNOWLEDGEMENTS], "entry_time", current_time - (time_t)idi->dbinfo.max_acknowledgements_age);
		}

		idi->dbinfo.last_table_trim_time = current_time;
	}

	return NDO_OK;
}
