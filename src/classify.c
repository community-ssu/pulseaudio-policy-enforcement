#include <stdio.h>

#include <pulsecore/pulsecore-config.h>

#include <pulsecore/log.h>
#include <pulsecore/client.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>

#include "classify.h"
#include "client-ext.h"
#include "sink-ext.h"
#include "source-ext.h"
#include "card-ext.h"
#include "sink-input-ext.h"
#include "source-output-ext.h"



static char *find_group_for_client(struct userdata *,
                                   struct pa_client *, pa_proplist *);
#if 0
static char *arg_dump(int, char **, char *, size_t);
#endif

static void  pid_hash_free(struct pa_classify_pid_hash **);
static void  pid_hash_insert(struct pa_classify_pid_hash **, pid_t,
                             const char *, const char *);
static void  pid_hash_remove(struct pa_classify_pid_hash **, pid_t,
                             const char *);
static char *pid_hash_get_group(struct pa_classify_pid_hash **, pid_t,
                                const char *);
static struct pa_classify_pid_hash
            *pid_hash_find(struct pa_classify_pid_hash **, pid_t, const char *,
                           struct pa_classify_pid_hash **);

static void streams_free(struct pa_classify_stream_def *);
static void streams_add(struct pa_classify_stream_def **, char *, 
                        enum pa_classify_method, char *, char *,
                        uid_t, char *, char *);
static char *streams_get_group(struct pa_classify_stream_def **,
                               pa_proplist *, char *, uid_t, char *);
static struct pa_classify_stream_def
            *streams_find(struct pa_classify_stream_def **, pa_proplist *,
                          char *, uid_t, char *,
                          struct pa_classify_stream_def **);

static void devices_free(struct pa_classify_device *);
static void devices_add(struct pa_classify_device **, char *,
                        char *,  enum pa_classify_method, char *, uint32_t);
static int devices_classify(struct pa_classify_device_def *, pa_proplist *,
                            char *, uint32_t, uint32_t, char *, int);
static int devices_is_typeof(struct pa_classify_device_def *, pa_proplist *,
                             char *, char *,struct pa_classify_device_data **);

static void cards_free(struct pa_classify_card *);
static void cards_add(struct pa_classify_card **, char *,
                      enum pa_classify_method, char *, char *, uint32_t);
static int  cards_classify(struct pa_classify_card_def *, char *, char **,
                           uint32_t,uint32_t, char *,int);
static int card_is_typeof(struct pa_classify_card_def *, char *,
                          char *, struct pa_classify_card_data **);


char *get_property(char *, pa_proplist *, char *);



struct pa_classify *pa_classify_new(struct userdata *u)
{
    struct pa_classify *cl;

    cl = pa_xnew0(struct pa_classify, 1);

    cl->sinks   = pa_xnew0(struct pa_classify_device, 1);
    cl->sources = pa_xnew0(struct pa_classify_device, 1);
    cl->cards   = pa_xnew0(struct pa_classify_card, 1);

    return cl;
}

void pa_classify_free(struct pa_classify *cl)
{
    if (cl) {
        pid_hash_free(cl->streams.pid_hash);
        streams_free(cl->streams.defs);
        devices_free(cl->sinks);
        devices_free(cl->sources);
        cards_free(cl->cards);

        pa_xfree(cl);
    }
}

void pa_classify_add_sink(struct userdata *u, char *type, char *prop,
                          enum pa_classify_method method, char *arg,
                          uint32_t flags)
{
    struct pa_classify *classify;

    pa_assert(u);
    pa_assert_se((classify = u->classify));
    pa_assert(classify->sinks);
    pa_assert(type);
    pa_assert(prop);
    pa_assert(arg);

    devices_add(&classify->sinks, type, prop, method, arg, flags);
}

void pa_classify_add_source(struct userdata *u, char *type, char *prop,
                            enum pa_classify_method method, char *arg,
                            uint32_t flags)
{
    struct pa_classify *classify;

    pa_assert(u);
    pa_assert_se((classify = u->classify));
    pa_assert(classify->sources);
    pa_assert(type);
    pa_assert(prop);
    pa_assert(arg);

    devices_add(&classify->sources, type, prop, method, arg, flags);
}

void pa_classify_add_card(struct userdata *u, char *type,
                          enum pa_classify_method method, char *arg,
                          char *profile, uint32_t flags)
{
    struct pa_classify *classify;

    pa_assert(u);
    pa_assert_se((classify = u->classify));
    pa_assert(classify->cards);
    pa_assert(type);
    pa_assert(arg);

    cards_add(&classify->cards, type, method, arg, profile, flags);
}


void pa_classify_add_stream(struct userdata *u, char *prop,
                            enum pa_classify_method method, char *arg,
                            char *clnam, uid_t uid, char *exe, char *group)
{
    struct pa_classify *classify;

    pa_assert(u);
    pa_assert_se((classify = u->classify));

    if (((prop && method && arg) || uid != (uid_t)-1 || exe) && group) {
        streams_add(&classify->streams.defs, prop,method,arg,
                    clnam, uid, exe, group);
    }
}

void pa_classify_register_pid(struct userdata *u, pid_t pid, char *stnam,
                              char *group)
{
    struct pa_classify *classify;

    pa_assert(u);
    pa_assert_se((classify = u->classify));

    if (pid && group) {
        pid_hash_insert(classify->streams.pid_hash, pid, stnam, group);
    }
}

void pa_classify_unregister_pid(struct userdata *u, pid_t pid, char *stnam)
{
    struct pa_classify *classify;
    
    pa_assert(u);
    pa_assert_se((classify = u->classify));

    if (pid) {
        pid_hash_remove(classify->streams.pid_hash, pid, stnam);
    }
}

char *pa_classify_sink_input(struct userdata *u, struct pa_sink_input *sinp)
{
    struct pa_client     *client;
    char                 *group;

    pa_assert(u);
    pa_assert(sinp);

    client = sinp->client;
    group  = find_group_for_client(u, client, sinp->proplist);

    return group;
}

char *pa_classify_sink_input_by_data(struct userdata *u,
                                     struct pa_sink_input_new_data *data)
{
    struct pa_client     *client;
    char                 *group;

    pa_assert(u);
    pa_assert(data);

    client = data->client;
    group  = find_group_for_client(u, client, data->proplist);

    return group;
}

char *pa_classify_source_output(struct userdata *u,
                                struct pa_source_output *sout)
{
    struct pa_client     *client;
    char                 *group;

    pa_assert(u);
    pa_assert(sout);

    client = sout->client;
    group  = find_group_for_client(u, client, sout->proplist);

    return group;
}

char *
pa_classify_source_output_by_data(struct userdata *u,
                                  struct pa_source_output_new_data *data)
{
    struct pa_client     *client;
    char                 *group;

    pa_assert(u);
    pa_assert(data);

    client = data->client;
    group  = find_group_for_client(u, client, data->proplist);

    return group;
}

int pa_classify_sink(struct userdata *u, struct pa_sink *sink,
                     uint32_t flag_mask, uint32_t flag_value,
                     char *buf, int len)
{
    struct pa_classify *classify;
    struct pa_classify_device_def *defs;
    char *name;

    pa_assert(u);
    pa_assert_se((classify = u->classify));
    pa_assert(classify->sinks);
    pa_assert_se((defs = classify->sinks->defs));

    name = pa_sink_ext_get_name(sink);

    return devices_classify(defs, sink->proplist, name,
                            flag_mask, flag_value, buf, len);
}

int pa_classify_source(struct userdata *u, struct pa_source *source,
                       uint32_t flag_mask, uint32_t flag_value,
                       char *buf, int len)
{
    struct pa_classify *classify;
    struct pa_classify_device_def *defs;
    char *name;

    pa_assert(u);
    pa_assert_se((classify = u->classify));
    pa_assert(classify->sources);
    pa_assert_se((defs = classify->sources->defs));

    name = pa_source_ext_get_name(source);

    return devices_classify(defs, source->proplist, name,
                            flag_mask, flag_value, buf, len);
}

int pa_classify_card(struct userdata *u, struct pa_card *card,
                     uint32_t flag_mask, uint32_t flag_value,
                     char *buf, int size)
{
    struct pa_classify *classify;
    struct pa_classify_card_def *defs;
    char  *name;
    char **profs;
    int    len;

    pa_assert(u);
    pa_assert_se((classify = u->classify));
    pa_assert(classify->cards);
    pa_assert_se((defs = classify->cards->defs));

    name  = pa_card_ext_get_name(card);
    profs = pa_card_ext_get_profiles(card);

    len = cards_classify(defs, name,profs, flag_mask,flag_value, buf,size);

    pa_xfree(profs);

    return len;
}

int pa_classify_is_sink_typeof(struct userdata *u, struct pa_sink *sink,
                               char *type, struct pa_classify_device_data **d)
{
    struct pa_classify *classify;
    struct pa_classify_device_def *defs;
    char *name;

    pa_assert(u);
    pa_assert_se((classify = u->classify));
    pa_assert(classify->sinks);
    pa_assert_se((defs = classify->sinks->defs));

    if (!sink || !type)
        return FALSE;

    name = pa_sink_ext_get_name(sink);

    return devices_is_typeof(defs, sink->proplist, name, type, d);
}


int pa_classify_is_source_typeof(struct userdata *u, struct pa_source *source,
                                 char *type,struct pa_classify_device_data **d)
{
    struct pa_classify *classify;
    struct pa_classify_device_def *defs;
    char *name;

    pa_assert(u);
    pa_assert_se((classify = u->classify));
    pa_assert(classify->sources);
    pa_assert_se((defs = classify->sources->defs));

    if (!source || !type)
        return FALSE;

    name = pa_source_ext_get_name(source);

    return devices_is_typeof(defs, source->proplist, name, type, d);
}


int pa_classify_is_card_typeof(struct userdata *u, struct pa_card *card,
                               char *type, struct pa_classify_card_data **d)
{
    struct pa_classify *classify;
    struct pa_classify_card_def *defs;
    char *name;

    pa_assert(u);
    pa_assert_se((classify = u->classify));
    pa_assert(classify->cards);
    pa_assert_se((defs = classify->cards->defs));

    if (!card || !type)
        return FALSE;

    name = pa_card_ext_get_name(card);

    return card_is_typeof(defs, name, type, d);
}


static char *find_group_for_client(struct userdata  *u,
                                   struct pa_client *client,
                                   pa_proplist      *proplist)
{
    struct pa_classify *classify;
    struct pa_classify_pid_hash **hash;
    struct pa_classify_stream_def **defs;
    pid_t    pid   = 0;           /* client processs PID */
    char    *clnam = (char *)"";  /* client's name in PA */
    uid_t    uid   = (uid_t)-1;   /* client process user ID */
    char    *exe   = (char *)"";  /* client's binary path */
    char    *arg0;
    char    *group = NULL;
    char    *stnam = NULL;

    assert(u);
    pa_assert_se((classify = u->classify));

    hash = classify->streams.pid_hash;
    defs = &classify->streams.defs;

    if (client == NULL)
        group = streams_get_group(defs, proplist, clnam, uid, exe);
    else {
        pid = pa_client_ext_pid(client);

        if (proplist)
            stnam = (char *)pa_proplist_gets(proplist, PA_PROP_MEDIA_NAME);
        else
            stnam = NULL;
 
        if ((group = pid_hash_get_group(hash, pid, stnam)) == NULL) {
            clnam = pa_client_ext_name(client);
            uid   = pa_client_ext_uid(client);
            exe   = pa_client_ext_exe(client);
            arg0  = pa_client_ext_arg0(client);

            group = streams_get_group(defs, proplist, clnam, uid, exe);
        }
    }

    if (group == NULL)
        group = (char *)PA_POLICY_DEFAULT_GROUP_NAME;

    pa_log_debug("%s (%s|%s|%d|%d|%s) => %s", __FUNCTION__,
                 clnam?clnam:"<null>", stnam?stnam:"<null>",
                 pid, uid, exe?exe:"<null>", group?group:"<null>");

    return group;
}

#if 0
static char *arg_dump(int argc, char **argv, char *buf, size_t len)
{
    char *p = buf;
    int   i, l;
    
    if (argc <= 0 || argv == NULL)
        snprintf(buf, len, "0 <null>");
    else {
        l = snprintf(p, len, "%d", argc);
        
        p   += l;
        len -= l;
        
        for (i = 0;  i < argc && len > 0;  i++) {
            l = snprintf(p, len, " [%d]=%s", i, argv[i]);
            
            p   += l;
            len -= l;
        }
    }
    
    return buf;
}
#endif

static void pid_hash_free(struct pa_classify_pid_hash **hash)
{
    struct pa_classify_pid_hash *st;
    int i;

    assert(hash);

    for (i = 0;   i < PA_POLICY_PID_HASH_MAX;   i++) {
        while ((st = hash[i]) != NULL) {
            hash[i] = st->next;

            pa_xfree(st->stnam);
            pa_xfree(st->group);

            pa_xfree(st);
        }
    }
}

static void pid_hash_insert(struct pa_classify_pid_hash **hash, pid_t pid,
                            const char *stnam, const char *group)
{
    struct pa_classify_pid_hash *st;
    struct pa_classify_pid_hash *prev;

    pa_assert(hash);
    pa_assert(group);


    if ((st = pid_hash_find(hash, pid, stnam, &prev)) != NULL) {
        pa_xfree(st->group);
        st->group = pa_xstrdup(group);
    }
    else {
        st  = pa_xnew0(struct pa_classify_pid_hash, 1);

        st->next  = NULL;
        st->pid   = pid;
        st->stnam = stnam ? pa_xstrdup(stnam) : NULL;
        st->group = pa_xstrdup(group);

        prev->next = st;
    }
}

static void pid_hash_remove(struct pa_classify_pid_hash **hash,
                            pid_t pid, const char *stnam)
{
    struct pa_classify_pid_hash *st;
    struct pa_classify_pid_hash *prev;

    pa_assert(hash);

    if ((st = pid_hash_find(hash, pid, stnam, &prev)) != NULL) {
        prev->next = st->next;

        pa_xfree(st->stnam);
        pa_xfree(st->group);
        
        pa_xfree(st);
    }
}

static char *pid_hash_get_group(struct pa_classify_pid_hash **hash,
                                pid_t pid, const char *stnam)
{
    struct pa_classify_pid_hash *st;
    char *group;

    pa_assert(hash);
 
    if (!pid || (st = pid_hash_find(hash, pid, stnam, NULL)) == NULL)
        group = NULL;
    else
        group = st->group;

    return group;
}

static struct
pa_classify_pid_hash *pid_hash_find(struct pa_classify_pid_hash **hash,
                                    pid_t pid, const char *stnam,
                                    struct pa_classify_pid_hash **prev_ret)
{
    struct pa_classify_pid_hash *st;
    struct pa_classify_pid_hash *prev;
    int                          idx;

    idx = pid & PA_POLICY_PID_HASH_MASK;

    for (prev = (struct pa_classify_pid_hash *)&hash[idx];
         (st = prev->next) != NULL;
         prev = prev->next)
    {
        if (pid && pid == st->pid) {
            if ((!stnam && !st->stnam) ||
                ( stnam &&  st->stnam && !strcmp(stnam,st->stnam)))
                break;
        }
    }

    if (prev_ret)
        *prev_ret = prev;

#if 0
    pa_log_debug("%s(%d,'%s') => %p", __FUNCTION__,
                 pid, stnam?stnam:"<null>", st);
#endif

    return st;
}

static void streams_free(struct pa_classify_stream_def *defs)
{
    struct pa_classify_stream_def *stream;
    struct pa_classify_stream_def *next;

    for (stream = defs;  stream;  stream = next) {
        next = stream->next;

        if (stream->method == pa_classify_method_matches)
            regfree(&stream->arg.rexp);
        else
            pa_xfree((void *)stream->arg.string);

        pa_xfree(stream->prop);
        pa_xfree(stream->exe);
        pa_xfree(stream->clnam);
        pa_xfree(stream->group);

        pa_xfree(stream);
    }
}

static void streams_add(struct pa_classify_stream_def **defs, char *prop,
                        enum pa_classify_method method,char *arg, char *clnam,
                        uid_t uid, char *exe, char *group)
{
    struct pa_classify_stream_def *d;
    struct pa_classify_stream_def *prev;
    pa_proplist *proplist = NULL;
    char         method_def[256];

    pa_assert(defs);
    pa_assert(group);

    proplist = pa_proplist_new();

    if (prop && arg && (method == pa_method_equals)) {
        pa_proplist_sets(proplist, prop, arg);
    }

    if ((d = streams_find(defs, proplist, clnam, uid, exe, &prev)) != NULL) {
        pa_log_info("%s: redefinition of stream", __FILE__);
        pa_xfree(d->group);
    }
    else {
        d = pa_xnew0(struct pa_classify_stream_def, 1);

        snprintf(method_def, sizeof(method_def), "<no-property-check>");
        
        if (prop && arg && method > pa_method_min && method < pa_method_max) {
            d->prop = pa_xstrdup(prop);

            switch (method) {

            case pa_method_equals:
                snprintf(method_def, sizeof(method_def),
                         "%s equals:%s", prop, arg);
                d->method = pa_classify_method_equals;
                d->arg.string = pa_xstrdup(arg);
                break;

            case pa_method_startswith:
                snprintf(method_def, sizeof(method_def),
                         "%s startswith:%s",prop, arg);
                d->method = pa_classify_method_startswith;
                d->arg.string = pa_xstrdup(arg);
                break;

            case pa_method_matches:
                snprintf(method_def, sizeof(method_def),
                         "%s matches:%s",prop, arg);
                d->method = pa_classify_method_matches;
                if (regcomp(&d->arg.rexp, arg, 0) != 0) {
                    pa_log("%s: invalid regexp definition '%s'",
                           __FUNCTION__, arg);
                    pa_assert_se(0);
                }
                break;


            case pa_method_true:
                snprintf(method_def, sizeof(method_def), "%s true", prop);
                d->method = pa_classify_method_true;
                memset(&d->arg, 0, sizeof(d->arg));
                break;

            default:
                /* never supposed to get here. just keep the compiler happy */
                pa_assert_se(0);
                break;
            }
        }

        d->uid   = uid;
        d->exe   = exe   ? pa_xstrdup(exe)   : NULL;
        d->clnam = clnam ? pa_xstrdup(clnam) : NULL;
        
        prev->next = d;

        pa_log_debug("stream added (%d|%s|%s|%s)", uid, exe?exe:"<null>",
                     clnam?clnam:"<null>", method_def);
    }

    d->group = pa_xstrdup(group);

    pa_proplist_free(proplist);
}

static char *streams_get_group(struct pa_classify_stream_def **defs,
                               pa_proplist *proplist,
                               char *clnam, uid_t uid, char *exe)
{
    struct pa_classify_stream_def *d;
    char *group;

    pa_assert(defs);

    if ((d = streams_find(defs, proplist, clnam, uid, exe, NULL)) == NULL)
        group = NULL;
    else
        group = d->group;

    return group;
}

static struct pa_classify_stream_def *
streams_find(struct pa_classify_stream_def **defs, pa_proplist *proplist,
             char *clnam, uid_t uid, char *exe,
             struct pa_classify_stream_def **prev_ret)
{
#define PROPERTY_MATCH     (!d->prop || !d->method || \
                           (d->method && d->method(prv, &d->arg)))
#define STRING_MATCH_OF(m) (!d->m || (m && d->m && !strcmp(m, d->m)))
#define ID_MATCH_OF(m)     (d->m == -1 || m == d->m)

    struct pa_classify_stream_def *prev;
    struct pa_classify_stream_def *d;
    char *prv;

    for (prev = (struct pa_classify_stream_def *)defs;
         (d = prev->next) != NULL;
         prev = prev->next)
    {
        if (!proplist || !d->prop ||
            !(prv = (char *)pa_proplist_gets(proplist, d->prop)) || !prv[0])
        {
            prv = (char *)"<unknown>";
        }

#if 0
        if (d->method == pa_classify_method_matches) {
            pa_log_debug("%s: prv='%s' prop='%s' arg=<regexp>",
                         __FUNCTION__, prv, d->prop?d->prop:"<null>");
        }
        else {
            pa_log_debug("%s: prv='%s' prop='%s' arg='%s'",
                         __FUNCTION__, prv, d->prop?d->prop:"<null>",
                         d->arg.string?d->arg.string:"<null>");
        }
#endif

        if (PROPERTY_MATCH         &&
            STRING_MATCH_OF(clnam) &&
            ID_MATCH_OF(uid)       &&
            STRING_MATCH_OF(exe)      )
            break;

    }

    if (prev_ret)
        *prev_ret = prev;

#if 0
    {
        char *s = pa_proplist_to_string_sep(proplist, " ");
        pa_log_debug("%s(<%s>,'%s',%d,'%s') => %p", __FUNCTION__,
                     s, clnam?clnam:"<null>", uid, exe?exe:"<null>", d);
        pa_xfree(s);
    }
#endif

    return d;

#undef STRING_MATCH_OF
#undef ID_MATCH_OF
}

static void devices_free(struct pa_classify_device *sinks)
{
    struct pa_classify_device_def *d;

    if (sinks) {
        for (d = sinks->defs;  d->type;  d++) {
            pa_xfree((void *)d->type);

            if (d->method == pa_classify_method_matches)
                regfree(&d->arg.rexp);
            else
                pa_xfree((void *)d->arg.string);
        }

        pa_xfree(sinks);
    }
}

static void devices_add(struct pa_classify_device **p_devices, char *type,
                        char *prop, enum pa_classify_method method, char *arg,
                        uint32_t flags)
{
    struct pa_classify_device *devs;
    struct pa_classify_device_def *d;
    size_t newsize;
    char *method_name;

    pa_assert(p_devices);
    pa_assert_se((devs = *p_devices));

    newsize = sizeof(*devs) + sizeof(devs->defs[0]) * (devs->ndef + 1);

    devs = *p_devices = pa_xrealloc(devs, newsize);

    d = devs->defs + devs->ndef;

    memset(d+1, 0, sizeof(devs->defs[0]));

    d->type  = pa_xstrdup(type);
    d->prop  = pa_xstrdup(prop);

    d->data.flags = flags;

    switch (method) {

    case pa_method_equals:
        method_name = "equals";
        d->method = pa_classify_method_equals;
        d->arg.string = pa_xstrdup(arg);
        break;

    case pa_method_startswith:
        method_name = "startswidth";
        d->method = pa_classify_method_startswith;
        d->arg.string = pa_xstrdup(arg);
        break;

    case pa_method_matches:
        method_name = "matches";
        if (regcomp(&d->arg.rexp, arg, 0) == 0) {
            d->method = pa_classify_method_matches;
            break;
        }
        /* intentional fall trough */

    default:
        pa_log("%s: invalid device definition %s", __FUNCTION__, type);
        memset(d, 0, sizeof(*d));
        return;
    }

    devs->ndef++;

    pa_log_info("device '%s' added (%s|%s|%s|0x%04x)",
                type, d->prop, method_name, arg, d->data.flags);
}

static int devices_classify(struct pa_classify_device_def *defs,
                            pa_proplist *proplist, char *name,
                            uint32_t flag_mask, uint32_t flag_value,
                            char *buf, int len)
{
    struct pa_classify_device_def *d;
    char       *propval;
    int         i;
    char       *p;
    char       *e;
    const char *s;

    pa_assert(buf);
    pa_assert(len > 0);

    e = (p = buf) + len;
    p[0] = '\0';
    s = "";
        
    for (d = defs, i = 0;  d->type;  d++) {
        propval = get_property(d->prop, proplist, name);

        if (d->method(propval, &d->arg)) {
            if ((d->data.flags & flag_mask) == flag_value) {
                p += snprintf(p, (size_t)(e-p), "%s%s", s, d->type);
                s  = " ";
                
                if (p > e) {
                    pa_log("%s: %s() buffer overflow", __FILE__, __FUNCTION__);
                    *buf = '\0';
                    p = e;
                    break;
                }
            }
        }
    }

    return (e - p);
}

static int devices_is_typeof(struct pa_classify_device_def *defs,
                             pa_proplist *proplist, char *name, char *type,
                             struct pa_classify_device_data **data)
{
    struct pa_classify_device_def *d;
    char *propval;

    for (d = defs;  d->type;  d++) {
        if (!strcmp(type, d->type)) {
            propval = get_property(d->prop, proplist, name);

            if (d->method(propval, &d->arg)) {
                if (data != NULL)
                    *data = &d->data;

                return TRUE;
            }
        }
    }

    return FALSE;
}

static void cards_free(struct pa_classify_card *cards)
{
    struct pa_classify_card_def *d;

    if (cards) {
        for (d = cards->defs;  d->type;  d++) {
            pa_xfree((void *)d->type);
            pa_xfree((void *)d->data.profile);

            if (d->method == pa_classify_method_matches)
                regfree(&d->arg.rexp);
            else
                pa_xfree((void *)d->arg.string);
        }

        pa_xfree(cards);
    }
}

static void cards_add(struct pa_classify_card **p_cards, char *type,
                      enum pa_classify_method method, char *arg,
                      char *profile, uint32_t flags)
{
    struct pa_classify_card *cards;
    struct pa_classify_card_def *d;
    size_t newsize;
    char *method_name;

    pa_assert(p_cards);
    pa_assert_se((cards = *p_cards));

    newsize = sizeof(*cards) + sizeof(cards->defs[0]) * (cards->ndef + 1);

    cards = *p_cards = pa_xrealloc(cards, newsize);

    d = cards->defs + cards->ndef;

    memset(d+1, 0, sizeof(cards->defs[0]));

    d->type    = pa_xstrdup(type);

    d->data.profile = profile ? pa_xstrdup(profile) : NULL;
    d->data.flags   = flags;

    switch (method) {

    case pa_method_equals:
        method_name = "equals";
        d->method = pa_classify_method_equals;
        d->arg.string = pa_xstrdup(arg);
        break;

    case pa_method_startswith:
        method_name = "startswidth";
        d->method = pa_classify_method_startswith;
        d->arg.string = pa_xstrdup(arg);
        break;

    case pa_method_matches:
        method_name = "matches";
        if (regcomp(&d->arg.rexp, arg, 0) == 0) {
            d->method = pa_classify_method_matches;
            break;
        }
        /* intentional fall trough */

    default:
        pa_log("%s: invalid card definition %s", __FUNCTION__, type);
        memset(d, 0, sizeof(*d));
        return;
    }

    cards->ndef++;

    pa_log_info("card '%s' added (%s|%s|%s|0x%04x)", type, method_name, arg,
                d->data.profile?d->data.profile:"", d->data.flags);
}

static int cards_classify(struct pa_classify_card_def *defs,
                          char *name, char **profiles,
                          uint32_t flag_mask, uint32_t flag_value,
                          char *buf, int len)
{
    struct pa_classify_card_def *d;
    int         i,j;
    char       *p;
    char       *e;
    const char *s;
    int         supports_profile;

    pa_assert(buf);
    pa_assert(len > 0);

    e = (p = buf) + len;
    p[0] = '\0';
    s = "";
        
    for (d = defs, i = 0;  d->type;  d++) {
        if (d->method(name, &d->arg)) {
            if (d->data.profile == NULL)
                supports_profile = TRUE;
            else {
                for (j = 0, supports_profile = FALSE;    profiles[j];    j++) {
                    if (!strcmp(d->data.profile, profiles[j])) {
                        supports_profile  = TRUE;
                        break;
                    }
                }
            }

            if (supports_profile && (d->data.flags & flag_mask) == flag_value){
                p += snprintf(p, (size_t)(e-p), "%s%s", s, d->type);
                s  = " ";
                
                if (p > e) {
                    pa_log("%s: %s() buffer overflow", __FILE__, __FUNCTION__);
                    *buf = '\0';
                    p = e;
                    break;
                }
            }
        }
    }

    return (e - p);
}

static int card_is_typeof(struct pa_classify_card_def *defs, char *name,
                          char *type, struct pa_classify_card_data **data)
{
    struct pa_classify_card_def *d;

    for (d = defs;  d->type;  d++) {
        if (!strcmp(type, d->type)) {
            if (d->method(name, &d->arg)) {
                if (data != NULL)
                    *data = &d->data;

                return TRUE;
            }
        }
    }

    return FALSE;
}

char *get_property(char *propname, pa_proplist *proplist, char *name)
{
    char *propval = NULL;

    if (propname != NULL && proplist != NULL && name != NULL) {
        if (!strcmp(propname, "name"))
            propval = name;
        else
            propval = (char *)pa_proplist_gets(proplist, propname);
    }

    if (propval == NULL || propval[0] == '\0')
        propval = (char *)"<unknown>";

    return propval;
}

int pa_classify_method_equals(const char *string,
                              union pa_classify_arg *arg)
{
    int found;

    if (!string || !arg || !arg->string)
        found = FALSE;
    else
        found = !strcmp(string, arg->string);

    return found;
}

int pa_classify_method_startswith(const char *string,
                                  union pa_classify_arg *arg)
{
    int found;

    if (!string || !arg || !arg->string)
        found = FALSE;
    else
        found = !strncmp(string, arg->string, strlen(arg->string));

    return found;
}

int pa_classify_method_matches(const char *string,
                               union pa_classify_arg *arg)
{
#define MAX_MATCH 5

    regmatch_t m[MAX_MATCH];
    regoff_t   end;
    int        found;
    
    found = FALSE;

    if (string && arg) {
        if (regexec(&arg->rexp, string, MAX_MATCH, m, 0) == 0) {
            end = strlen(string);

            if (m[0].rm_so == 0 && m[0].rm_eo == end && m[1].rm_so == -1)
                found = TRUE;
        }  
    }


    return found;

#undef MAX_MATCH
}

int pa_classify_method_true(const char *string,
                            union pa_classify_arg *arg)
{
    (void)string;
    (void)arg;

    return TRUE;
}
                                  
/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
