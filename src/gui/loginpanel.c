#include <glib.h>
#include <glib/gstdio.h>
#include <loginpanel.h>
#include <mainpanel.h>
#include <mainwindow.h>
#include <tray.h>
#include <gdk/gdkkeysyms.h>
#include <stdlib.h>
#include <statusbutton.h>
#include <chatwindow.h>
#include <msgloop.h>
#include <string.h>
#include "type.h"
#include "lwdb.h"
#include "logger.h"
#include "login.h"
#include "info.h"
#include "smemory.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/**
 * The global value
 * in main.c
 */
extern LwqqClient *lwqq_client;
extern char *lwqq_install_dir;
extern char *lwqq_icons_dir;
extern char *lwqq_buddy_status_dir;
extern QQTray *tray;

extern GQQMessageLoop *get_info_loop;
extern GQQMessageLoop *send_loop;

extern GHashTable *lwqq_chat_window;

static void qq_loginpanelclass_init(QQLoginPanelClass *c);
static void qq_loginpanel_init(QQLoginPanel *obj);
static void qq_loginpanel_destroy(GtkWidget *obj);
static void qqnumber_combox_changed(GtkComboBoxText *widget, gpointer data);
//static void update_face_image(LwqqClient *lc, QQMainPanel *panel);
static void update_buddy_qq_number(LwqqClient *lc, QQMainPanel *panel);
static void run_login_state_machine(QQLoginPanel *panel);

typedef struct LoginPanelUserInfo {
    char *qqnumber;
    char *password;
    char *status;
    char *rempwd;
} LoginPanelUserInfo;
static LoginPanelUserInfo login_panel_user_info;

/*
 * The main event loop context of Gtk.
 */
static GQQMessageLoop gtkloop;

//static GPtrArray* login_users = NULL;

GType qq_loginpanel_get_type()
{
    static GType t = 0;
    if(!t){
        static const GTypeInfo info =
            {
                sizeof(QQLoginPanelClass),
                NULL,
                NULL,
                (GClassInitFunc)qq_loginpanelclass_init,
                NULL,
                NULL,
                sizeof(QQLoginPanel),
                0,
                (GInstanceInitFunc)qq_loginpanel_init,
                NULL
            };
        t = g_type_register_static(GTK_TYPE_VBOX, "QQLoginPanel"
                                   , &info, 0);
    }
    return t;
}

GtkWidget* qq_loginpanel_new(GtkWidget *container)
{
    QQLoginPanel *panel = g_object_new(qq_loginpanel_get_type(), NULL);
    panel -> container = container;

    return GTK_WIDGET(panel);
}

static void qq_loginpanelclass_init(QQLoginPanelClass *c)
{
    GtkWidgetClass *object_class = NULL;
    object_class = GTK_WIDGET_CLASS(c);

    object_class -> destroy = qq_loginpanel_destroy;

    /*
     * get the default main evet loop context.
     *
     * I think this will work...
     */
    gtkloop.ctx = g_main_context_default();
    gtkloop.name = "LoginPanel Gtk";
}

/**
 * Update the details.
 */
static void update_details(LwqqClient *lc, QQLoginPanel *panel)
{
#if 0
    // update my information
    qq_get_buddy_info(info, info -> me, NULL);
    gqq_mainloop_attach(&gtkloop
                        , qq_mainpanel_update_my_info
                        , 1
                        , QQ_MAINWINDOW(panel -> container) -> main_panel);
#endif

    QQMainPanel *mp = QQ_MAINPANEL(QQ_MAINWINDOW(panel->container)->main_panel);
    /* update online buddies */
    lwqq_info_get_online_buddies(lc, NULL);
    gqq_mainloop_attach(&gtkloop, qq_mainpanel_update_online_buddies, 1, mp);

    //update qq number
    update_buddy_qq_number(lc, (QQMainPanel *)QQ_MAINWINDOW(panel->container)->main_panel);
#if 0
    // update group number
    gint i;
    QQGroup *grp;
    gchar num[100];
    for(i = 0; i < info->groups->len; ++i){
        grp = g_ptr_array_index(info -> groups, i);
        if(grp == NULL){
            continue;
        }
        qq_get_qq_number(info, grp -> code -> str, num, NULL);
        qq_group_set(grp, "gnumber", num);
    }
    gqq_mainloop_attach(&gtkloop, qq_mainpanel_update_group_info , 1,
                        QQ_MAINWINDOW(panel -> container) -> main_panel);

    //update face image
    update_face_image(info,
                      (QQMainPanel*)QQ_MAINWINDOW(panel->container)-> main_panel);
#endif
}

//login state machine state.
enum{
    LOGIN_SM_CHECKVC,
    LOGIN_SM_LOGIN,
    LOGIN_SM_GET_DATA,
    LOGIN_SM_DONE,
    LOGIN_SM_ERR
};

static void free_login_panel_user_info()
{
    LoginPanelUserInfo *info = &login_panel_user_info;
    g_free(info->password);
    g_free(info->qqnumber);
    g_free(info->rempwd);
    g_free(info->status);
}

static void login_panel_update_user_info(QQLoginPanel* loginpanel)
{
    QQLoginPanel *panel = NULL;
    gboolean active;
    LoginPanelUserInfo *info;
    
    /* Free old data */
    free_login_panel_user_info();
    
    panel = QQ_LOGINPANEL(loginpanel);

    info = &login_panel_user_info;

    info->qqnumber = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(panel->uin_entry));

    info->password = g_strdup(gtk_entry_get_text(
                                  GTK_ENTRY(panel->passwd_entry)));
    
    info->status = g_strdup(qq_statusbutton_get_status_string(
                                loginpanel->status_comb));

    active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
                                              loginpanel->rempwcb));
    if (active == TRUE) {
        info->rempwd = g_strdup("1");
    } else {
        info->rempwd = g_strdup("0");
    }
}

static void update_gdb(QQLoginPanel *lp)
{
#define UPDATE_GDB_MACRO() {                                            \
        gdb->update_user_info(gdb, info->qqnumber, "password", info->password); \
        gdb->update_user_info(gdb, info->qqnumber, "status", info->status); \
        gdb->update_user_info(gdb, info->qqnumber, "rempwd", info->rempwd); \
    }
    LwdbGlobalDB *gdb = lp->gdb;
    LwdbGlobalUserEntry *e;
    LoginPanelUserInfo *info = &login_panel_user_info;
    
    LIST_FOREACH(e, &gdb->head, entries) {
        if (!g_strcmp0(e->qqnumber, info->qqnumber)) {
            UPDATE_GDB_MACRO();
            return ;
        }
    }

    gdb->add_new_user(gdb, info->qqnumber);
    UPDATE_GDB_MACRO();
    
#undef UPDATE_GDB_MACRO
}

static void get_vc(QQLoginPanel *panel)
{
    LwqqClient *lc = lwqq_client;
    char vc_image[128];
    GtkWidget *w = panel->container;

    g_snprintf(vc_image, sizeof(vc_image), "/tmp/lwqq_%s.jpeg", lc->username);
    if (g_access(vc_image, F_OK)) {
        g_warning("No vc image data or type!(%s, %d)" , __FILE__, __LINE__);
        gtk_label_set_text(GTK_LABEL(panel->err_label),
                           "Login failed. Please retry.");
        qq_mainwindow_show_loginpanel(w);
        return;
    }

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Information", GTK_WINDOW(w), GTK_DIALOG_MODAL, GTK_STOCK_OK,
        GTK_RESPONSE_OK, NULL);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(
        GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), vbox);

    GtkWidget *img = gtk_image_new_from_file(vc_image);

    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("VerifyCode："),
                       FALSE, FALSE, 20);
    gtk_box_pack_start(GTK_BOX(vbox), img, FALSE, FALSE, 0);

    GtkWidget *vc_entry = gtk_entry_new();
    gtk_widget_set_size_request(vc_entry, 200, -1);
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vc_entry, TRUE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 10);

    gtk_widget_set_size_request(dialog, 300, 220);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));

    /* got the verify code */
    lc->vc->str = g_strdup(gtk_entry_get_text(GTK_ENTRY(vc_entry)));
    gtk_widget_destroy(dialog);
    run_login_state_machine(panel);
}

static void handle_new_msg(LwqqRecvMsg *recvmsg)
{
    LwqqMsg *msg = recvmsg->msg;

    printf("Receive message type: %d\n", msg->type);

    if (msg->type == LWQQ_MT_BUDDY_MSG) {
        LwqqMsgMessage *mmsg = msg->opaque;
        GtkWidget *cw = g_hash_table_lookup(lwqq_chat_window, mmsg->from);
//        printf("Receive message: %s\n", mmsg->content);
        if (!cw) {
            cw = qq_chatwindow_new(mmsg->from);
            lwqq_log(LOG_DEBUG, "No chat window for uin:%s, create a new:%p\n",
                     mmsg->from, cw);
#if 0
            // not show it
            gtk_widget_hide(cw);
#endif
            g_hash_table_insert(lwqq_chat_window, g_strdup(mmsg->from), cw);
        } else {
            lwqq_log(LOG_DEBUG, "Found chat window:%p for uin:%s\n", cw, mmsg->from);
        }
        qq_chatwindow_add_recv_message(cw, mmsg);
        gtk_widget_show(cw);

    } else if (msg->type == LWQQ_MT_GROUP_MSG) {
        
    } else if (msg->type == LWQQ_MT_STATUS_CHANGE) {
        
    } else {
        printf("unknow message\n");
    }
    
    lwqq_msg_free(recvmsg->msg);
    s_free(recvmsg);
}

static gpointer poll_msg(gpointer data)
{
    LwqqRecvMsgList *l = (LwqqRecvMsgList *)data;
    l->poll_msg(l);
    while (1) {
        LwqqRecvMsg *msg;
        pthread_mutex_lock(&l->mutex);
        if (SIMPLEQ_EMPTY(&l->head)) {
            /* No message now, wait 100ms */
            pthread_mutex_unlock(&l->mutex);
            usleep(100000);
            continue;
        }
        msg = SIMPLEQ_FIRST(&l->head);
        SIMPLEQ_REMOVE_HEAD(&l->head, entries);
        pthread_mutex_unlock(&l->mutex);
        gqq_mainloop_attach(&gtkloop, handle_new_msg, 1, msg);
    }
    return NULL;
}

/**
 * Handle login. In this function, we do login, get friends information,
 * Get group information, and so on.
 * 
 * @param panel 
 */
static void handle_login(QQLoginPanel *panel)
{
    LwqqClient *lc = lwqq_client;
    LwqqErrorCode err = LWQQ_EC_ERROR;

    lwqq_login(lwqq_client, &err);

    switch (err) {
    case LWQQ_EC_OK:
        lwqq_log(LOG_NOTICE, "Login successfully\n");
        lwqq_info_get_friends_info(lc, NULL);

        /* Start poll message */
#if GLIB_CHECK_VERSION(2,31,0)
        g_thread_new("Poll message", poll_msg, lc->msg_list);
#else
        g_thread_create(poll_msg, lc->msg_list, FALSE, NULL);
#endif

        /* update main panel */
        gqq_mainloop_attach(&gtkloop, qq_mainpanel_update, 1,
                            QQ_MAINPANEL(QQ_MAINWINDOW(panel->container)->main_panel));

        /* show main panel */
        gqq_mainloop_attach(&gtkloop, qq_mainwindow_show_mainpanel,
                            1, panel->container);

        update_details(lc, panel);
        break;
    case LWQQ_EC_LOGIN_NEED_VC:
        gqq_mainloop_attach(&gtkloop, get_vc, 1, panel);
        break;
    case LWQQ_EC_ERROR:
    default:
        lwqq_log(LOG_ERROR, "Login failed\n");
        gqq_mainloop_attach(&gtkloop, gtk_label_set_text, 2,
                            GTK_LABEL(panel->err_label), "Login failed");
        gqq_mainloop_attach(&gtkloop, qq_mainwindow_show_loginpanel,
                            1, panel->container);
        break;
    }
}

//
// The login state machine is run in the message loop.
//
static void run_login_state_machine(QQLoginPanel *panel)
{
    gqq_mainloop_attach(get_info_loop, handle_login, 1, panel);
}

/**
 * login_cb(QQLoginPanel *panel)
 * show the splashpanel and start the login procedure.
 */
static void login_cb(QQLoginPanel* panel)
{
    LoginPanelUserInfo *info = &login_panel_user_info;
    GtkWidget *win = panel->container;
    qq_mainwindow_show_splashpanel(win);
    
    /* Get user information from the login panel */
    login_panel_update_user_info(panel);
    lwqq_log(LOG_NOTICE, "Start login... qqnum: %s, status: %s\n",
             info->qqnumber, info->status);

    /* Update database */
    update_gdb(panel);

    if (lwqq_client) {
        /* Free old object */
        lwqq_client_free(lwqq_client);
    }
    lwqq_client = lwqq_client_new(info->qqnumber, info->password);

    if (lwqq_client) {
        run_login_state_machine(panel);
    }
}
/*
 * actived when RETURN pressed at uin_entry or passwd_entry
 */
gboolean quick_login(GtkWidget* widget,GdkEvent* e,gpointer data)
{
	GdkEventKey *event = (GdkEventKey*)e;
	if(event -> keyval == GDK_KEY_Return || event -> keyval == GDK_KEY_KP_Enter||
			event -> keyval == GDK_KEY_ISO_Enter){
		if((event -> state & GDK_CONTROL_MASK) != 0
                         || (event -> state & GDK_SHIFT_MASK) != 0){
			return FALSE;
         	}
         	login_cb(QQ_LOGINPANEL(data));
         	return TRUE;
	}
	return FALSE;

}

/**
 * Callback of login_btn button
 */
static void login_btn_cb(GtkButton *btn, gpointer data)
{
	QQLoginPanel *panel = QQ_LOGINPANEL(data);
	login_cb(panel);
}


static void qq_loginpanel_init(QQLoginPanel *obj)
{
    LwdbGlobalUserEntry *e;
    
    memset(&login_panel_user_info, 0, sizeof(login_panel_user_info));

    obj->gdb = lwdb_globaldb_new();
    if (!obj->gdb) {
        lwqq_log(LOG_ERROR, "Create global db failed, exit\n");
        exit(1);
    }
    obj->uin_label = gtk_label_new("QQ Number:");
    obj->uin_entry = gtk_combo_box_text_new_with_entry();
    LIST_FOREACH(e, &obj->gdb->head, entries) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(obj->uin_entry),
                                       e->qqnumber);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(obj->uin_entry), 0);

    obj->passwd_label = gtk_label_new("Password:");
    obj->passwd_entry = gtk_entry_new();
    e = LIST_FIRST(&obj->gdb->head);
    if (e && e->rempwd) {
        gtk_entry_set_text(GTK_ENTRY(obj->passwd_entry), e->password);
    }
    g_signal_connect(G_OBJECT(obj->uin_entry), "changed",
                     G_CALLBACK(qqnumber_combox_changed), obj);
	g_signal_connect(G_OBJECT(obj->uin_entry),"key-press-event",
                     G_CALLBACK(quick_login), (gpointer)obj);
	g_signal_connect(G_OBJECT(obj->passwd_entry), "key-press-event",
                     G_CALLBACK(quick_login), (gpointer)obj);
    /* not visibily */
    gtk_entry_set_visibility(GTK_ENTRY(obj->passwd_entry), FALSE);

    gtk_widget_set_size_request(obj->uin_entry, 200, -1);
    gtk_widget_set_size_request(obj->passwd_entry, 220, -1);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    /* uin label and entry */
    GtkWidget *uin_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(uin_hbox), obj->uin_label, FALSE, FALSE, 0);
    GtkWidget *uin_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(uin_vbox), uin_hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(uin_vbox), obj->uin_entry, FALSE, FALSE, 0);
    
    /* password label and entry */
    GtkWidget *passwd_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(passwd_hbox), obj->passwd_label,
                       FALSE, FALSE, 0);
    GtkWidget *passwd_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(passwd_vbox), passwd_hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(passwd_vbox), obj->passwd_entry, FALSE, FALSE, 0);

    /* put uin and password in a vbox */
    gtk_box_pack_start(GTK_BOX(vbox), uin_vbox, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), passwd_vbox, FALSE, FALSE, 2);

    /* rember password check box */
    obj->rempwcb = gtk_check_button_new_with_label("Remeber Password");
    if (e && e->rempwd) {
        gboolean r;
        r = atoi(e->rempwd) == 1 ? TRUE : FALSE;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(obj->rempwcb), r);

    }
    /* g_signal_connect(G_OBJECT(obj->rempwcb), "toggled" */
    /*                     , G_CALLBACK(qqnumber_combox_changed), obj); */
    GtkWidget *hbox4 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(hbox4), obj->rempwcb, TRUE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox4, FALSE, TRUE, 2);

    /* login button */
    obj->login_btn = gtk_button_new_with_label("Login");
    gtk_widget_set_size_request(obj->login_btn, 90, -1);
    g_signal_connect(G_OBJECT(obj->login_btn), "clicked", G_CALLBACK(login_btn_cb), (gpointer)obj);
    
    /* status combo box */
    obj->status_comb = qq_statusbutton_new();
    if (e && e->status) {
        qq_statusbutton_set_status_string(obj->status_comb, e->status);
    }

    GtkWidget *hbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(hbox1), vbox, TRUE, FALSE, 0);

    GtkWidget *hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *hbox3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(hbox2), obj->status_comb, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox2), obj->login_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox3), hbox2, TRUE, FALSE, 0);

    /* error informatin label */
    obj->err_label = gtk_label_new("");
    GdkRGBA color;
    gdk_rgba_parse(&color, "#fff000000"); /* red */
    gtk_widget_override_color(GTK_WIDGET(obj-> err_label), GTK_STATE_NORMAL, &color);

    hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(hbox2), obj -> err_label, TRUE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox2, TRUE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), hbox3, FALSE, FALSE, 0);

    gtk_box_set_homogeneous(GTK_BOX(obj), FALSE);
    char img[512];
    g_snprintf(img, sizeof(img), "%s/webqq_icon.png", lwqq_icons_dir);
    GtkWidget *logo = gtk_image_new_from_file(img);
    gtk_widget_set_size_request(logo, -1, 150);
    gtk_box_pack_start(GTK_BOX(obj), logo, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(obj), hbox1, FALSE, FALSE, 15);
}

static void qqnumber_combox_changed(GtkComboBoxText *widget, gpointer data)
{
    LwdbGlobalUserEntry *e;
    char *qqnumber = gtk_combo_box_text_get_active_text(widget);
    if (!qqnumber) {
        return;
    }

    QQLoginPanel *obj = QQ_LOGINPANEL(data);
    LIST_FOREACH(e, &obj->gdb->head, entries) {
        if (!strcmp(e->qqnumber, qqnumber)) {
            gtk_entry_set_text(GTK_ENTRY(obj->passwd_entry), e->password);
            qq_statusbutton_set_status_string(obj->status_comb, e->status);
            break;
        }
    }
}

/*
 * Destroy the instance of QQLoginPanel
 */
static void qq_loginpanel_destroy(GtkWidget *obj)
{
    /*
     * Child widgets will be destroied by their parents.
     * So, we should not try to unref the Child widgets here.
     */

}

const gchar* qq_loginpanel_get_uin(QQLoginPanel *loginpanel)
{
    QQLoginPanel *panel = QQ_LOGINPANEL(loginpanel);
    /* return gtk_combo_box_get_active_text(
        GTK_COMBO_BOX(panel -> uin_entry));     */
    return gtk_combo_box_text_get_active_text(
		    GTK_COMBO_BOX_TEXT(panel->uin_entry));

}
const gchar* qq_loginpanel_get_passwd(QQLoginPanel *loginpanel)
{
    QQLoginPanel *panel = QQ_LOGINPANEL(loginpanel);
    return gtk_entry_get_text(
        GTK_ENTRY(panel -> passwd_entry));
}
const gchar* qq_loginpanel_get_status(QQLoginPanel *loginpanel)
{
    return qq_statusbutton_get_status_string(loginpanel -> status_comb);
}

gint qq_loginpanel_get_rempw(QQLoginPanel *loginpanel)
{
	return  gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(loginpanel->rempwcb));
}

typedef struct {
    LwqqClient *lc;
    LwqqBuddy *bdy;
} ThreadFuncPar;

//
// Get buddy qq number thread func
//
static void get_buddy_qqnumber_thread_func(gpointer data, gpointer user_data)
{

    ThreadFuncPar *par = data;
    LwqqClient *lc = par->lc;
    LwqqBuddy *bdy = par->bdy;
    g_slice_free(ThreadFuncPar ,par);

    /* Free old one */
    s_free(bdy->qqnumber);
    bdy->qqnumber = lwqq_info_get_friend_qqnumber(lc, bdy->uin);
}

//
// Update qq number
// Run in get_info_loop.
//
static void update_buddy_qq_number(LwqqClient *lc, QQMainPanel *panel)
{
    GThreadPool *thread_pool;
    ThreadFuncPar *par = NULL;
    LwqqBuddy *buddy;

    thread_pool = g_thread_pool_new(get_buddy_qqnumber_thread_func, NULL,
                                    100, TRUE, NULL);

    if (!thread_pool){
        g_debug("can not create new thread pool ...(%s,%d)",
                __FILE__, __LINE__ );
        return;
    }
    LIST_FOREACH(buddy, &lc->friends, entries) {
        par = g_slice_new0(ThreadFuncPar);
        par->lc = lc;
        par->bdy = buddy;
        g_thread_pool_push(thread_pool, (gpointer)par, NULL);
    }

    g_thread_pool_free(thread_pool, 0, 1);

    //update the panel
    qq_mainpanel_update_buddy_info(panel);
//    gqq_mainloop_attach(&gtkloop, qq_mainpanel_update_buddy_faceimg, 1, panel);
    return;
}

#if 0
void get_buddy_face_thread_func(gpointer data, gpointer user_data)
{

    ThreadFuncPar *par = data;
    GPtrArray * imgs = par -> array;
    gint id = par -> id;
    QQInfo * info = par -> info;
    g_slice_free(ThreadFuncPar , par);
    QQFaceImg * img;
    gchar path[500];
    img = g_ptr_array_index(imgs, id);
    qq_get_face_img(info,img, NULL);
    g_snprintf(path, 500, "%s/%s", QQ_FACEDIR, img -> num -> str);
    qq_save_face_img(img,path,NULL);

}
/**
 * Get all buddies' and groups' face images
 * Run in get_info_loop.
 */
static void update_face_image(LwqqClient *lc, QQMainPanel *panel)
{
    if(info == NULL || panel == NULL){
        return;
    }

    GPtrArray *fimgs = g_ptr_array_new();
    gint i;
    QQBuddy *bdy = NULL;
    QQFaceImg *img = NULL;

    // me
    img = qq_faceimg_new();
    qq_faceimg_set(img, "uin", info -> me -> uin);
    qq_faceimg_set(img, "num", info -> me  -> qqnumber);
    g_ptr_array_add(fimgs, img);

    // buddies
    for(i = 0; i < info -> buddies -> len; ++i){
        bdy = g_ptr_array_index(info -> buddies, i);
        if(bdy == NULL){
            continue;
        }
        img = qq_faceimg_new();
        qq_faceimg_set(img, "uin", bdy -> uin);
        qq_faceimg_set(img, "num", bdy -> qqnumber);
        g_ptr_array_add(fimgs, img);
    }
//#if 0
    // groups
    QQGroup *grp = NULL;
    for(i = 0; i < info -> groups -> len; ++i){
        grp = g_ptr_array_index(info -> groups, i);
        if(grp == NULL){
            continue;
        }
        img = qq_faceimg_new();
        qq_faceimg_set(img, "uin", grp -> code);
        qq_faceimg_set(img, "num", grp -> gnumber);
        g_ptr_array_add(fimgs, img);
    }
//#endif

    GThreadPool * thread_pool;

#if !GLIB_CHECK_VERSION(2,31,0)
    g_thread_init(NULL);
#endif
    ThreadFuncPar * par = NULL;

    thread_pool = g_thread_pool_new(get_buddy_face_thread_func, NULL ,100, TRUE, NULL );

    if ( ! thread_pool ){
        g_debug("can not create new thread pool ...(%s,%d)",
                __FILE__, __LINE__ );
        return;
    }

    for ( i =0 ; i < info->buddies ->len ; i ++ )
    {
        par = g_slice_new0(ThreadFuncPar);
        par -> array = NULL;
        par -> id = i;
        par -> info = info;
        par -> array = fimgs;

        g_thread_pool_push( thread_pool , (gpointer) par, NULL);
    }

    g_thread_pool_free(thread_pool, 0,1);


    for(i = 0; i < fimgs -> len; ++i){
        img = g_ptr_array_index(fimgs, i);
        qq_faceimg_free(img);
    }

    //update the buddy info
    gqq_mainloop_attach(&gtkloop, qq_mainpanel_update_buddy_faceimg, 1, panel);
    gqq_mainloop_attach(&gtkloop, qq_mainpanel_update_group_info, 1, panel);
}
#endif
