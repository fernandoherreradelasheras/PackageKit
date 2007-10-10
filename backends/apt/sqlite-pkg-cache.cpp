/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Tom Parker <palfrey@tevp.net>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <sqlite3.h>
#include <glib.h>
#include "sqlite-pkg-cache.h"

static sqlite3 *db = NULL;

struct search_task {
	gchar *search;
	gchar *filter;
	SearchDepth depth;
};

void init_sqlite_cache(PkBackend *backend, const char* dbname, void (*build_db)(PkBackend *, sqlite3 *))
{
	gint ret;
	char *errmsg = NULL;
	ret = sqlite3_open (dbname, &db);
	ret = sqlite3_exec(db,"PRAGMA synchronous = OFF",NULL,NULL,NULL);
	g_assert(ret == SQLITE_OK);
	sqlite3_exec(db,"create table packages (name text, version text, deps text, arch text, short_desc text, long_desc text, repo string, primary key(name,version,arch,repo))",NULL,NULL,&errmsg);
	if (errmsg == NULL) // success, ergo didn't exist
	{
		build_db(backend,db);
	}
	else
	{
		sqlite3_free(errmsg);
		/*ret = sqlite3_exec(db,"delete from packages",NULL,NULL,NULL); // clear it!
		g_assert(ret == SQLITE_OK);
		pk_debug("wiped db");*/
	}
}

// backend_search_packages_thread
gboolean backend_search_packages_thread (PkBackend *backend, gpointer data)
{
	search_task *st = (search_task *) data;
	int res;

	pk_backend_change_status(backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates(backend);

	pk_debug("finding %s", st->search);

	sqlite3_stmt *package = NULL;
	g_strdelimit(st->search," ",'%');
	gchar *sel = g_strdup_printf("select name,version,arch,repo,short_desc from packages where name like '%%%s%%'",st->search);
	pk_debug("statement is '%s'",sel);
	res = sqlite3_prepare_v2(db,sel, -1, &package, NULL);
	g_free(sel);
	if (res!=SQLITE_OK)
		pk_error("sqlite error during select prepare: %s", sqlite3_errmsg(db));
	res = sqlite3_step(package);
	while (res == SQLITE_ROW)
	{
		gchar *pid = pk_package_id_build((const gchar*)sqlite3_column_text(package,0),
				(const gchar*)sqlite3_column_text(package,1),
				(const gchar*)sqlite3_column_text(package,2),
				(const gchar*)sqlite3_column_text(package,3));
		pk_backend_package(backend, FALSE, pid, (const gchar*)sqlite3_column_text(package,4));
		g_free(pid);
		if (res==SQLITE_ROW)
			res = sqlite3_step(package);
	}
	if (res!=SQLITE_DONE)
	{
		pk_debug("sqlite error during step (%d): %s", res, sqlite3_errmsg(db));
		g_assert(0);
	}

	g_free(st->search);
	g_free(st);

	return TRUE;
}

/**
 * backend_search_common
 **/
void
backend_search_common(PkBackend * backend, const gchar * filter, const gchar * search, SearchDepth which, PkBackendThreadFunc func)
{
	g_return_if_fail (backend != NULL);
	search_task *data = g_new(struct search_task, 1);
	if (data == NULL)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory for search task");
		pk_backend_finished(backend);
	}
	else
	{
		data->search = g_strdup(search);
		data->filter = g_strdup(filter);
		data->depth = which;
		pk_backend_thread_helper (backend, func, data);
	}
}

/**
 * backend_search_details:
 */
void
sqlite_search_details (PkBackend *backend, const gchar *filter, const gchar *search)
{
	backend_search_common(backend, filter, search, SEARCH_DETAILS, backend_search_packages_thread);
}

/**
 * backend_search_name:
 */
void
sqlite_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	backend_search_common(backend, filter, search, SEARCH_NAME, backend_search_packages_thread);
}


