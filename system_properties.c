/*
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <errno.h>

#include <sys/mman.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/types.h>
#include <netinet/in.h>

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>

#include <sys/atomics.h>

static const char property_service_name[] = PROP_SERVICE_NAME;

static unsigned dummy_props = 0;

prop_area *__system_property_area__ = (void*) &dummy_props;

/* Detected PROP_NAME_MAX. Note, that android-x86.org uses a changed constant, see
 * http://android-x86.git.sourceforge.net/git/gitweb.cgi?p=android-x86/x86_bionic.git;a=commitdiff;h=380a68a2b52f41bfdb0d2662b0e21281505037d5
 */
#define PROP_NAME_LIMIT 256
static int prop_name_max = PROP_NAME_MAX;

int __system_properties_init(void)
{
    prop_area *pa;
    int fd;
    unsigned sz;
    char *env;

    if(__system_property_area__ != ((void*) &dummy_props)) {
        return 0;
    }

    env = getenv("ANDROID_PROPERTY_WORKSPACE");
    if (!env) {
        return -1;
    }
    fd = atoi(env);
    env = strchr(env, ',');
    if (!env) {
        return -1;
    }
    sz = atoi(env + 1);
    
    pa = mmap(0, sz, PROT_READ, MAP_SHARED, fd, 0);
    
    if(pa == MAP_FAILED) {
        return -1;
    }

    if((pa->magic != PROP_AREA_MAGIC) || (pa->version != PROP_AREA_VERSION)) {
        munmap(pa, sz);
        return -1;
    }

    __system_property_area__ = pa;
    if (0 < pa->count)
    {
        /*
         * Yes - this is C. I like it, but it may have bugs :)
         * Search the longest value in the /system/build.prop
         * and /system/default.prop files with a name that starts
         * with "ro." in order to search this read-only property to
         * determine PROP_NAME_MAX later on.
         */
        int prop;
        int minlen = 8;
        char name[PROP_NAME_LIMIT];
        char value[PROP_VALUE_MAX];
        for (prop = 0; prop < 2; prop++) {
            FILE* f = fopen(prop ? PROP_PATH_SYSTEM_BUILD : PROP_PATH_SYSTEM_DEFAULT, "r");
            if (f)
            {
                char s[32 + PROP_NAME_LIMIT + PROP_VALUE_MAX];
                while (fgets(s, sizeof(s), f))
                {
                    char* p = s;
                    while (sizeof(s) > p - s && (' ' == *p || '\t' == *p)) p++;
                    if (sizeof(s) > p - s && 0 == strncmp(p, "ro.", 3))
                    {
                        char* n = p;
                        while (sizeof(s) > p - s && 0 != *p && '=' != *p && ' ' != *p && '\t' != *p) p++;
                        if (sizeof(s) > p - s && 0 != *p)
                        {
                            char* nend = p;
                            while (sizeof(s) > p - s && 0 != *p && '=' != *p) p++;
                            if (sizeof(s) > p - s && '=' == *p)
                            {
                                char *v;
                                *nend = 0;
                                p++;
                                while (sizeof(s) > p - s && (' ' == *p || '\t' == *p)) p++;
                                v = p;
                                while (sizeof(s) > p - s && (' ' <= *p)) p++;
                                if (sizeof(s) > p - s)
                                {
                                    char *vend = p;
                                    *vend = 0;
                                    if (sizeof(name) > nend - n && sizeof(value) > vend - v && minlen < vend - v)
                                    {
                                        minlen = vend - v;
                                        strcpy(name, n);
                                        strcpy(value, v);
                                    }
                                }
                            }
                        }
                    }
                }
                fclose(f);
            }
        }
        if (8 < minlen)
        {
            const prop_info *pi = __system_property_find(name);
            if (0 != pi)
            {
            	int m = PROP_NAME_MAX;
            	while(m <= PROP_NAME_LIMIT)
            	{
            		if (0 == strcmp(pi->value + (m - PROP_NAME_MAX), value))
            		{
            			prop_name_max = m;
            			break;
            		}
            		m++;
            	}
            }
        }
    }
    return 0;
}

const prop_info *__system_property_find_nth(unsigned n)
{
    prop_area *pa = __system_property_area__;

    if(n >= pa->count) {
        return 0;
    } else {
        return TOC_TO_INFO(pa, pa->toc[n]);
    }
}

const prop_info *__system_property_find(const char *name)
{
    prop_area *pa = __system_property_area__;
    unsigned count = pa->count;
    unsigned *toc = pa->toc;
    unsigned len = strlen(name);
    prop_info *pi;

    while(count--) {
        unsigned entry = *toc++;
        if(TOC_NAME_LEN(entry) != len) continue;
        
        pi = TOC_TO_INFO(pa, entry);
        if(memcmp(name, pi->name, len)) continue;

        return pi;
    }

    return 0;
}

int __system_property_read(const prop_info *pi, char *name, char *value)
{
    unsigned serial, len;
    
    for(;;) {
        serial = *(unsigned*)((char*)pi + prop_name_max);
        while(SERIAL_DIRTY(serial)) {
            __futex_wait((volatile void *)((char*)pi + prop_name_max), serial, 0);
            serial = *(unsigned*)((char*)pi + prop_name_max);
        }
        len = SERIAL_VALUE_LEN(serial);
        memcpy(value, (char*)pi + prop_name_max + sizeof(pi->serial), len + 1);
        if(serial == *(unsigned*)((char*)pi + prop_name_max)) {
            if(name != 0) {
                strcpy(name, pi->name);
            }
            return len;
        }
    }
}

int __system_property_get(const char *name, char *value)
{
    const prop_info *pi = __system_property_find(name);

    if(pi != 0) {
        return __system_property_read(pi, 0, value);
    } else {
        value[0] = 0;
        return 0;
    }
}

int __system_property_wait(const prop_info *pi)
{
    unsigned n;
    if(pi == 0) {
        prop_area *pa = __system_property_area__;
        n = pa->serial;
        do {
            __futex_wait(&pa->serial, n, 0);
        } while(n == pa->serial);
    } else {
        n = pi->serial;
        do {
            __futex_wait((volatile void *)&pi->serial, n, 0);
        } while(n == pi->serial);
    }
    return 0;
}
