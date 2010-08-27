/*
 * -----------------------------------------------------------------------------
 *
 * Author: Markus Moeller (markus_moeller at compuserve.com)
 *
 * Copyright (C) 2007 Markus Moeller. All rights reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 *
 * -----------------------------------------------------------------------------
 */

#include "config.h"
#include "util.h"

#ifdef HAVE_LDAP

#include "support.h"
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_RESOLV_H
#include <resolv.h>
#endif
#ifdef HAVE_ARPA_NAMESER_H
#include <arpa/nameser.h>
#endif

void nsError(int nserror, char *server);
static int compare_hosts(struct hstruct *h1, struct hstruct *h2);
static void swap(struct hstruct *a, struct hstruct *b);
static void sort(struct hstruct *array, int nitems, int (*cmp) (struct hstruct *, struct hstruct *), int begin, int end);
static void msort(struct hstruct *array, size_t nitems, int (*cmp) (struct hstruct *, struct hstruct *));

/*
 * See http://www.ietf.org/rfc/rfc1035.txt
 */
/*
 * See http://www.ietf.org/rfc/rfc2782.txt
 *
 */
void
nsError(int nserror, char *service)
{
    switch (nserror) {
    case HOST_NOT_FOUND:
        error((char *) "%s| %s: ERROR: res_search: Unknown service record: %s\n", LogTime(), PROGRAM, service);
        break;
    case NO_DATA:
        error((char *) "%s| %s: ERROR: res_search: No SRV record for %s\n", LogTime(), PROGRAM, service);
        break;
    case TRY_AGAIN:
        error((char *) "%s| %s: ERROR: res_search: No response for SRV query\n", LogTime(), PROGRAM);
        break;
    default:
        error((char *) "%s| %s: ERROR: res_search: Unexpected error: %s\n", LogTime(), PROGRAM, strerror(nserror));
    }
}

static void
swap(struct hstruct *a, struct hstruct *b)
{
    struct hstruct c;

    c.host = a->host;
    c.priority = a->priority;
    c.weight = a->weight;
    a->priority = b->priority;
    a->weight = b->weight;
    b->host = c.host;
    b->priority = c.priority;
    b->weight = c.weight;
}

static void
sort(struct hstruct *array, int nitems, int (*cmp) (struct hstruct *, struct hstruct *), int begin, int end)
{
    if (end > begin) {
        int pivot = begin;
        int l = begin + 1;
        int r = end;
        while (l < r) {
            if (cmp(&array[l], &array[pivot]) <= 0) {
                l += 1;
            } else {
                r -= 1;
                swap(&array[l], &array[r]);
            }
        }
        l -= 1;
        swap(&array[begin], &array[l]);
        sort(array, nitems, cmp, begin, l);
        sort(array, nitems, cmp, r, end);
    }
}

static void
msort(struct hstruct *array, size_t nitems, int (*cmp) (struct hstruct *, struct hstruct *))
{
    sort(array, nitems, cmp, 0, nitems - 1);
}

static int
compare_hosts(struct hstruct *host1, struct hstruct *host2)
{
    /*
     *
     * The comparison function must return an integer less than,  equal  to,
     * or  greater  than  zero  if  the  first  argument is considered to be
     * respectively less than, equal to, or greater than the second.
     */
    if ((host1->priority < host2->priority) && (host1->priority != -1))
        return -1;
    if ((host1->priority < host2->priority) && (host1->priority == -1))
        return 1;
    if ((host1->priority > host2->priority) && (host2->priority != -1))
        return 1;
    if ((host1->priority > host2->priority) && (host2->priority == -1))
        return -1;
    if (host1->priority == host2->priority) {
        if (host1->weight > host2->weight)
            return -1;
        if (host1->weight < host2->weight)
            return 1;
    }
    return 0;
}

int
free_hostname_list(struct hstruct **hlist, int nhosts)
{
    struct hstruct *hp = NULL;
    int i;

    hp = *hlist;
    for (i = 0; i < nhosts; i++) {
        if (hp[i].host)
            xfree(hp[i].host);
        hp[i].host = NULL;
    }


    if (hp)
        xfree(hp);
    hp = NULL;
    *hlist = hp;
    return 0;
}

int
get_hostname_list(struct main_args *margs, struct hstruct **hlist, int nhosts, char *name)
{
    /*
     * char host[sysconf(_SC_HOST_NAME_MAX)];
     */
    char host[1024];
    struct addrinfo *hres = NULL, *hres_list;
    int rc, count;
    struct hstruct *hp = NULL;

    if (!name)
        return (nhosts);

    hp = *hlist;
    rc = getaddrinfo((const char *) name, NULL, NULL, &hres);
    if (rc != 0) {
        error((char *) "%s| %s: ERROR: Error while resolving hostname with getaddrinfo: %s\n", LogTime(), PROGRAM, gai_strerror(rc));
        return (nhosts);
    }
    hres_list = hres;
    count = 0;
    while (hres_list) {
        count++;
        hres_list = hres_list->ai_next;
    }
    hres_list = hres;
    count = 0;
    while (hres_list) {
        rc = getnameinfo(hres_list->ai_addr, hres_list->ai_addrlen, host, sizeof(host), NULL, 0, 0);
        if (rc != 0) {
            error((char *) "%s| %s: ERROR: Error while resolving ip address with getnameinfo: %s\n", LogTime(), PROGRAM, gai_strerror(rc));
            freeaddrinfo(hres);
            *hlist = hp;
            return (nhosts);
        }
        count++;
        debug((char *) "%s| %s: DEBUG: Resolved address %d of %s to %s\n", LogTime(), PROGRAM, count, name, host);

        hp = (struct hstruct *) xrealloc(hp, sizeof(struct hstruct) * (nhosts + 1));
        hp[nhosts].host = xstrdup(host);
        hp[nhosts].port = -1;
        hp[nhosts].priority = -1;
        hp[nhosts].weight = -1;
        nhosts++;

        hres_list = hres_list->ai_next;
    }

    freeaddrinfo(hres);
    *hlist = hp;
    return (nhosts);
}

int
get_ldap_hostname_list(struct main_args *margs, struct hstruct **hlist, int nh, char *domain)
{

    /*
     * char name[sysconf(_SC_HOST_NAME_MAX)];
     */
    char name[1024];
    char host[NS_MAXDNAME];
    char *service;
    struct hstruct *hp = NULL;
    int nhosts = 0;
    int size;
    int type, rdlength;
    int priority, weight, port;
    int len, olen;
    int i, j, k;
    u_char *buffer;
    u_char *p;

    if (margs->ssl) {
        service = (char *) xmalloc(strlen("_ldaps._tcp.") + strlen(domain) + 1);
        strcpy(service, "_ldaps._tcp.");
    } else {
        service = (char *) xmalloc(strlen("_ldap._tcp.") + strlen(domain) + 1);
        strcpy(service, "_ldap._tcp.");
    }
    strcat(service, domain);

#ifndef PACKETSZ_MULT
    /*
     * It seems Solaris doesn't give back the real length back when res_search uses a to small buffer
     * Set a bigger one here
     */
#define PACKETSZ_MULT 10
#endif

    hp = *hlist;
    buffer = (u_char *) xmalloc(PACKETSZ_MULT * NS_PACKETSZ);
    if ((len = res_search(service, ns_c_in, ns_t_srv, (u_char *) buffer, PACKETSZ_MULT * NS_PACKETSZ)) < 0) {
        error((char *) "%s| %s: ERROR: Error while resolving service record %s with res_search\n", LogTime(), PROGRAM, service);
        nsError(h_errno, service);
        if (margs->ssl) {
            xfree(service);
            service = (char *) xmalloc(strlen("_ldap._tcp.") + strlen(domain) + 1);
            strcpy(service, "_ldap._tcp.");
            strcat(service, domain);
            if ((len = res_search(service, ns_c_in, ns_t_srv, (u_char *) buffer, PACKETSZ_MULT * NS_PACKETSZ)) < 0) {
                error((char *) "%s| %s: ERROR: Error while resolving service record %s with res_search\n", LogTime(), PROGRAM, service);
                nsError(h_errno, service);
                goto cleanup;
            }
        } else {
            goto cleanup;
        }
    }
    if (len > PACKETSZ_MULT * NS_PACKETSZ) {
        olen = len;
        buffer = (u_char *) xrealloc(buffer, len);
        if ((len = res_search(service, ns_c_in, ns_t_srv, (u_char *) buffer, len)) < 0) {
            error((char *) "%s| %s: ERROR: Error while resolving service record %s with res_search\n", LogTime(), PROGRAM, service);
            nsError(h_errno, service);
            goto cleanup;
        }
        if (len > olen) {
            error((char *) "%s| %s: ERROR: Reply to big: buffer: %d reply length: %d\n", LogTime(), PROGRAM, olen, len);
            goto cleanup;
        }
    }
    p = buffer;
    p += 6 * NS_INT16SZ;	/* Header(6*16bit) = id + flags + 4*section count */
    if (p > buffer + len) {
        error((char *) "%s| %s: ERROR: Message to small: %d < header size\n", LogTime(), PROGRAM, len);
        goto cleanup;
    }
    if ((size = dn_expand(buffer, buffer + len, p, name, sysconf(_SC_HOST_NAME_MAX))) < 0) {
        error((char *) "%s| %s: ERROR: Error while expanding query name with dn_expand:  %s\n", LogTime(), PROGRAM, strerror(errno));
        goto cleanup;
    }
    p += size;			/* Query name */
    p += 2 * NS_INT16SZ;	/* Query type + class (2*16bit) */
    if (p > buffer + len) {
        error((char *) "%s| %s: ERROR: Message to small: %d < header + query name,type,class \n", LogTime(), PROGRAM, len);
        goto cleanup;
    }
    while (p < buffer + len) {
        if ((size = dn_expand(buffer, buffer + len, p, name, sysconf(_SC_HOST_NAME_MAX))) < 0) {
            error((char *) "%s| %s: ERROR: Error while expanding answer name with dn_expand:  %s\n", LogTime(), PROGRAM, strerror(errno));
            goto cleanup;
        }
        p += size;		/* Resource Record name */
        if (p > buffer + len) {
            error((char *) "%s| %s: ERROR: Message to small: %d < header + query name,type,class + answer name\n", LogTime(), PROGRAM, len);
            goto cleanup;
        }
        NS_GET16(type, p);	/* RR type (16bit) */
        p += NS_INT16SZ + NS_INT32SZ;	/* RR class + ttl (16bit+32bit) */
        if (p > buffer + len) {
            error((char *) "%s| %s: ERROR: Message to small: %d < header + query name,type,class + answer name + RR type,class,ttl\n", LogTime(), PROGRAM, len);
            goto cleanup;
        }
        NS_GET16(rdlength, p);	/* RR data length (16bit) */

        if (type == ns_t_srv) {	/* SRV record */
            if (p > buffer + len) {
                error((char *) "%s| %s: ERROR: Message to small: %d < header + query name,type,class + answer name + RR type,class,ttl + RR data length\n", LogTime(), PROGRAM, len);
                goto cleanup;
            }
            NS_GET16(priority, p);	/* Priority (16bit) */
            if (p > buffer + len) {
                error((char *) "%s| %s: ERROR: Message to small: %d <  SRV RR + priority\n", LogTime(), PROGRAM, len);
                goto cleanup;
            }
            NS_GET16(weight, p);	/* Weight (16bit) */
            if (p > buffer + len) {
                error((char *) "%s| %s: ERROR: Message to small: %d <  SRV RR + priority + weight\n", LogTime(), PROGRAM, len);
                goto cleanup;
            }
            NS_GET16(port, p);	/* Port (16bit) */
            if (p > buffer + len) {
                error((char *) "%s| %s: ERROR: Message to small: %d <  SRV RR + priority + weight + port\n", LogTime(), PROGRAM, len);
                goto cleanup;
            }
            if ((size = dn_expand(buffer, buffer + len, p, host, NS_MAXDNAME)) < 0) {
                error((char *) "%s| %s: ERROR: Error while expanding SRV RR name with dn_expand:  %s\n", LogTime(), PROGRAM, strerror(errno));
                goto cleanup;
            }
            debug((char *) "%s| %s: DEBUG: Resolved SRV %s record to %s\n", LogTime(), PROGRAM, service, host);
            hp = (struct hstruct *) xrealloc(hp, sizeof(struct hstruct) * (nh + 1));
            hp[nh].host = xstrdup(host);
            hp[nh].port = port;
            hp[nh].priority = priority;
            hp[nh].weight = weight;
            nh++;
            p += size;
        } else {
            p += rdlength;
        }
        if (p > buffer + len) {
            error((char *) "%s| %s: ERROR: Message to small: %d <  SRV RR + priority + weight + port + name\n", LogTime(), PROGRAM, len);
            goto cleanup;
        }
    }
    if (p != buffer + len) {
#if (SIZEOF_LONG == 8)
        error("%s| %s: ERROR: Inconsistence message length: %ld!=0\n", LogTime(), PROGRAM, buffer + len - p);
#else
        error((char *) "%s| %s: ERROR: Inconsistence message length: %d!=0\n", LogTime(), PROGRAM, buffer + len - p);
#endif
        goto cleanup;
    }
    nhosts = get_hostname_list(margs, &hp, nh, domain);

    /* Remove duplicates */
    for (i = 0; i < nhosts; i++) {
        for (j = i + 1; j < nhosts; j++) {
            if (!strcasecmp(hp[i].host, hp[j].host)) {
                if (hp[i].port == hp[j].port ||
                        (hp[i].port == -1 && hp[j].port == 389) ||
                        (hp[i].port == 389 && hp[j].port == -1)) {
                    xfree(hp[j].host);
                    for (k = j + 1; k < nhosts; k++) {
                        hp[k - 1].host = hp[k].host;
                        hp[k - 1].port = hp[k].port;
                        hp[k - 1].priority = hp[k].priority;
                        hp[k - 1].weight = hp[k].weight;
                    }
                    j--;
                    nhosts--;
                    hp = (struct hstruct *) xrealloc(hp, sizeof(struct hstruct) * (nhosts + 1));
                }
            }
        }
    }

    /* Sort by Priority / Weight */
    msort(hp, nhosts, compare_hosts);

    if (debug_enabled) {
        debug((char *) "%s| %s: DEBUG: Sorted ldap server names for domain %s:\n", LogTime(), PROGRAM, domain);
        for (i = 0; i < nhosts; i++) {
            debug((char *) "%s| %s: DEBUG: Host: %s Port: %d Priority: %d Weight: %d\n", LogTime(), PROGRAM, hp[i].host, hp[i].port, hp[i].priority, hp[i].weight);
        }
    }
    if (buffer)
        xfree(buffer);
    if (service)
        xfree(service);
    *hlist = hp;
    return (nhosts);

cleanup:
    if (buffer)
        xfree(buffer);
    if (service)
        xfree(service);
    *hlist = hp;
    return (nhosts);
}
#endif
