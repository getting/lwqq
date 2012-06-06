/**
 * @file   type.c
 * @author mathslinux <riegamaths@gmail.com>
 * @date   Sun May 20 23:01:57 2012
 * 
 * @brief  Linux WebQQ Data Struct API
 * 
 * 
 */

#include <string.h>
#include "type.h"
#include "smemory.h"
#include "logger.h"

/** 
 * Create a new lwqq client
 * 
 * @param username QQ username
 * @param password QQ password
 * 
 * @return A new LwqqClient instance, or NULL failed
 */
LwqqClient *lwqq_client_new(const char *username, const char *password)
{
    if (!username || !password) {
        lwqq_log(LOG_ERROR, "Username or password is null\n");
        return NULL;
    }

    LwqqClient *lc = s_malloc0(sizeof(*lc));
    lc->username = s_strdup(username);
    lc->password = s_strdup(password);
    lc->myself = lwqq_buddy_new();
    if (!lc->myself) {
        goto failed;
    }

    lc->cookies = s_malloc0(sizeof(*(lc->cookies)));
    return lc;
    
failed:
    lwqq_client_free(lc);
    return NULL;
}

/** 
 * Get cookies needby by webqq server
 * 
 * @param lc 
 * 
 * @return Cookies string on success, or null on failure
 */
char *lwqq_get_cookies(LwqqClient *lc)
{
    if (lc->cookies && lc->cookies->lwcookies) {
        return s_strdup(lc->cookies->lwcookies);
    }

    return NULL;
}

static void vc_free(LwqqVerifyCode *vc)
{
    if (vc) {
        s_free(vc->str);
        s_free(vc->type);
        s_free(vc->img);
        s_free(vc->uin);
        s_free(vc);
    }
}

static void cookies_free(LwqqCookies *c)
{
    if (c) {
        s_free(c->ptvfsession);
        s_free(c->ptcz);
        s_free(c->skey);
        s_free(c->ptwebqq);
        s_free(c->ptuserinfo);
        s_free(c->uin);
        s_free(c->ptisp);
        s_free(c->pt2gguin);
        s_free(c->verifysession);
        s_free(c->lwcookies);
        s_free(c);
    }
}

static void lwqq_categories_free(LwqqFriendCategory *cate)
{
    if (!cate)
        return ;

    s_free(cate->name);
    s_free(cate);
}

/** 
 * Free LwqqClient instance
 * 
 * @param client LwqqClient instance
 */
void lwqq_client_free(LwqqClient *client)
{
    if (!client)
        return ;

    /* Free LwqqVerifyCode instance */
    s_free(client->username);
    s_free(client->password);
    s_free(client->version);
    vc_free(client->vc);
    cookies_free(client->cookies);
    s_free(client->clientid);
    s_free(client->seskey);
    s_free(client->cip);
    s_free(client->index);
    s_free(client->port);
    s_free(client->status);
    s_free(client->vfwebqq);
    s_free(client->psessionid);
    lwqq_buddy_free(client->myself);
        
    /* Free friends list */
    LwqqBuddy *b_entry, *b_next;
    LIST_FOREACH_SAFE(b_entry, &client->friends, entries, b_next) {
        LIST_REMOVE(b_entry, entries);
        lwqq_buddy_free(b_entry);
    }

    /* Free categories list */
    LwqqFriendCategory *c_entry, *c_next;
    LIST_FOREACH_SAFE(c_entry, &client->categories, entries, c_next) {
        LIST_REMOVE(c_entry, entries);
        lwqq_categories_free(c_entry);
    }
        
    s_free(client);
}

/************************************************************************/
/* LwqqBuddy API */

/** 
 * 
 * Create a new buddy
 * 
 * @return A LwqqBuddy instance
 */
LwqqBuddy *lwqq_buddy_new()
{
    LwqqBuddy *b = s_malloc0(sizeof(*b));
    return b;
}

/** 
 * Free a LwqqBuddy instance
 * 
 * @param buddy 
 */
void lwqq_buddy_free(LwqqBuddy *buddy)
{
    if (!buddy)
        return ;

    s_free(buddy->uin);
    s_free(buddy->qqnumber);
    s_free(buddy->nick);
    s_free(buddy->markname);
    s_free(buddy->face);
    s_free(buddy->flag);
    
    s_free(buddy);
}

/** 
 * Find buddy object by buddy's uin member
 * 
 * @param lc Our Lwqq client object
 * @param uin The uin of buddy which we want to find
 * 
 * @return 
 */
LwqqBuddy *lwqq_buddy_find_buddy_by_uin(LwqqClient *lc, const char *uin)
{
    LwqqBuddy *buddy;
    
    if (!lc || !uin)
        return NULL;

    LIST_FOREACH(buddy, &lc->friends, entries) {
        if (buddy->uin && (strcmp(buddy->uin, uin) == 0))
            return buddy;
    }

    return NULL;
}

/* LwqqBuddy API END*/
/************************************************************************/
