/*
 * parse_dev.c -- parse device name into hostname and export path
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 0211-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "xcommon.h"
#include "nls.h"
#include "parse_dev.h"

#ifndef NFS_MAXHOSTNAME
#define NFS_MAXHOSTNAME		(255)
#endif

#ifndef NFS_MAXPATHNAME
#define NFS_MAXPATHNAME		(1024)
#endif

extern char *progname;
extern int verbose;

struct dev_name *dev_name_append (struct dev_name *list,
                                  char *hostname, char *pathname)
{
  struct dev_name *item;

  item = (struct dev_name *)malloc(sizeof(struct dev_name));
  if (hostname == NULL)
    item->hostname = NULL;
  else
    item->hostname = strdup(hostname);
  if (pathname ==NULL)
    item->pathname = NULL;
  else
    item->pathname = strdup(pathname);
  item->next = NULL;

  if (list==NULL) {
    return item;
  } else {
    struct dev_name *last = list;
    while (last->next != NULL)  last = last->next;
    last->next = item;
    return list;
  }
}

struct dev_name *nfs_parse_multiple_hostname(char *dev)
{
  char *s;
  char *hostname;
  char *pathname;
  struct dev_name *list=NULL;

  while ((s = strchr(dev, ':'))) {
    hostname = dev;
    pathname = s + 1;
    *s = '\0';
    if ((s=strchr(pathname,','))) {
      *s = '\0';
      dev = s + 1;
    }

    while ((s=strchr(hostname,','))) {
      *s = '\0';
      list = dev_name_append(list, hostname, pathname);
      hostname = s + 1;
    }
    list = dev_name_append(list, hostname, pathname);
  }
  return list;
}

static int nfs_pdn_no_devname_err(void)
{
	nfs_error(_("%s: no device name was provided"), progname);
	return 0;
}

static int nfs_pdn_hostname_too_long_err(void)
{
	nfs_error(_("%s: server hostname is too long"), progname);
	return 0;
}

static int nfs_pdn_pathname_too_long_err(void)
{
	nfs_error(_("%s: export pathname is too long"), progname);
	return 0;
}

static int nfs_pdn_bad_format_err(void)
{
	nfs_error(_("%s: remote share not in 'host:dir' format"), progname);
	return 0;
}

static int nfs_pdn_nomem_err(void)
{
	nfs_error(_("%s: no memory available to parse devname"), progname);
	return 0;
}

static int nfs_pdn_missing_brace_err(void)
{
	nfs_error(_("%s: closing bracket missing from server address"),
				progname);
	return 0;
}

/*
 * Standard hostname:path format
 */
static int nfs_parse_simple_hostname(const char *dev,
				     char **hostname, char **pathname)
{
	size_t host_len, path_len;
	char *colon, *comma;

	/* Must have a colon */
	colon = strchr(dev, ':');
	if (colon == NULL)
		return nfs_pdn_bad_format_err();
	*colon = '\0';
	host_len = colon - dev;

	if (host_len > NFS_MAXHOSTNAME)
		return nfs_pdn_hostname_too_long_err();

	/* If there's a comma before the colon, take only the
	 * first name in list */
	comma = strchr(dev, ',');
	if (comma != NULL) {
		*comma = '\0';
		host_len = comma - dev;
		nfs_error(_("%s: warning: multiple hostnames not supported"),
				progname);
	} else

	colon++;
	path_len = strlen(colon);
	if (path_len > NFS_MAXPATHNAME)
		return nfs_pdn_pathname_too_long_err();

	if (hostname) {
		*hostname = strndup(dev, host_len);
		if (*hostname == NULL)
			return nfs_pdn_nomem_err();
	}
	if (pathname) {
		*pathname = strndup(colon, path_len);
		if (*pathname == NULL) {
			if (hostname)
				free(*hostname);
			return nfs_pdn_nomem_err();
		}
	}
	return 1;
}

/*
 * To handle raw IPv6 addresses (which contain colons), the
 * server's address is enclosed in square brackets.  Return
 * what's between the brackets.
 *
 * There could be anything in between the brackets, but we'll
 * let DNS resolution sort it out later.
 */
static int nfs_parse_square_bracket(const char *dev,
				    char **hostname, char **pathname)
{
	size_t host_len, path_len;
	char *cbrace;

	dev++;

	/* Must have a closing square bracket */
	cbrace = strchr(dev, ']');
	if (cbrace == NULL)
		return nfs_pdn_missing_brace_err();
	*cbrace = '\0';
	host_len = cbrace - dev;

	/* Must have a colon just after the closing bracket */
	cbrace++;
	if (*cbrace != ':')
		return nfs_pdn_bad_format_err();

	if (host_len > NFS_MAXHOSTNAME)
		return nfs_pdn_hostname_too_long_err();

	cbrace++;
	path_len = strlen(cbrace);
	if (path_len > NFS_MAXPATHNAME)
		return nfs_pdn_pathname_too_long_err();

	if (hostname) {
		*hostname = strndup(dev, host_len);
		if (*hostname == NULL)
			return nfs_pdn_nomem_err();
	}
	if (pathname) {
		*pathname = strndup(cbrace, path_len);
		if (*pathname == NULL) {
			free(*hostname);
			return nfs_pdn_nomem_err();
		}
	}
	return 1;
}

/*
 * RFC 2224 says an NFS client must grok "public file handles" to
 * support NFS URLs.  Linux doesn't do that yet.  Print a somewhat
 * helpful error message in this case instead of pressing forward
 * with the mount request and failing with a cryptic error message
 * later.
 */
static int nfs_parse_nfs_url(__attribute__((unused)) const char *dev,
			     __attribute__((unused)) char **hostname,
			     __attribute__((unused)) char **pathname)
{
	nfs_error(_("%s: NFS URLs are not supported"), progname);
	return 0;
}

/**
 * nfs_parse_devname - Determine the server's hostname by looking at "devname".
 * @devname: pointer to mounted device name (first argument of mount command)
 *
 * Returns a struct dev_name with the information of the proper devname. If
 * fails NULL is returned.
 *
 * Note that this will not work if @devname is a wide-character string.
 */
struct dev_name *nfs_parse_devname(const char *devname)
{
	char *dev;
  char *hostname = NULL;
  char *pathname = NULL;
  struct dev_name *list = NULL;

  if (devname == NULL) {
    nfs_pdn_no_devname_err();
    return NULL;
  }



	/* Parser is destructive, so operate on a copy of the device name. */
	dev = strdup(devname);
  if (dev == NULL) {
    nfs_pdn_nomem_err();
    return NULL;
  }
  if (*dev == '[') {
    if (!nfs_parse_square_bracket(dev, &hostname, &pathname))
      list = dev_name_append(NULL, hostname, pathname);
  } else if (strncmp(dev, "nfs://", 6) == 0) {
    if (!nfs_parse_nfs_url(dev, &hostname, &pathname))
      list = dev_name_append(NULL, hostname, pathname);
  } else if (strchr(dev, ':')) {
    list = nfs_parse_multiple_hostname (dev);
  } else {
    if (!nfs_parse_simple_hostname(dev, &hostname, &pathname))
      list = dev_name_append(NULL, hostname, pathname);
  }

	free(dev);
	return list;
}
