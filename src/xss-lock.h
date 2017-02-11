#define LOGIND_SERVICE "org.freedesktop.login1"
#define LOGIND_PATH    "/org/freedesktop/login1"
#define LOGIND_MANAGER_INTERFACE "org.freedesktop.login1.Manager"
#define LOGIND_SESSION_INTERFACE "org.freedesktop.login1.Session"

typedef struct Child {
    gchar        *name;
    gchar       **cmd;
    GPid          pid;
    gboolean      transfer_sleep_lock_fd;
    struct Child *kill_first;
} Child;

static gboolean register_screensaver(xcb_connection_t *connection, xcb_screen_t *screen, xcb_atom_t *atom, GError **error);
static void unregister_screensaver(xcb_connection_t *connection, xcb_screen_t *screen, xcb_atom_t atom);
static gboolean screensaver_event_cb(xcb_connection_t *connection, xcb_generic_event_t *event, const int *xcb_screensaver_notify);

static void keep_sleep_lock_fd_open(gpointer user_data);
static void start_child(Child *child);
static void kill_child(Child *child);
static void child_watch_cb(GPid pid, gint status, Child *child);

static void logind_manager_proxy_new_cb(GObject *source_object, GAsyncResult *res, gpointer user_data);
static void logind_manager_take_sleep_delay_lock(void);
static void logind_manager_call_inhibit_cb(GObject *source_object, GAsyncResult *res, gpointer user_data);
static void logind_manager_on_signal_prepare_for_sleep(GDBusProxy *proxy, gchar *sender_name, gchar *signal_name, GVariant *parameters, gpointer user_data);
static void logind_manager_call_get_session_cb(GObject *source_object, GAsyncResult *res, gpointer user_data);
static void logind_session_proxy_new_cb(GObject *source_object, GAsyncResult *res, gpointer user_data);
static void logind_session_on_signal_lock(GDBusProxy *proxy, gchar *sender_name, gchar *signal_name, GVariant *parameters, gpointer user_data);
static void logind_session_set_idle_hint(gboolean idle);

static gboolean parse_options(int argc, char *argv[], GError **error);
static gboolean parse_notifier_cmd(const gchar *option_name, const gchar *value, gpointer data, GError **error);
static gboolean reset_screensaver(xcb_connection_t *connection);
static gboolean exit_service(GMainLoop *loop);
static void log_handler(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data);

static Child notifier = {"notifier", NULL, 0, FALSE, NULL};
static Child locker = {"locker", NULL, 0, FALSE, &notifier};
static gboolean opt_quiet = FALSE;
static gboolean opt_verbose = FALSE;
static gboolean opt_ignore_sleep = FALSE;
static gboolean opt_print_version = FALSE;

static GOptionEntry opt_entries[] = {
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &locker.cmd, NULL, "LOCK_CMD [ARG...]"},
    {"notifier", 'n', G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, parse_notifier_cmd, "Send notification using CMD", "CMD"},
    {"transfer-sleep-lock", 'l', 0, G_OPTION_ARG_NONE, &locker.transfer_sleep_lock_fd, "Pass sleep delay lock file descriptor to locker", NULL},
    {"ignore-sleep", 0, 0, G_OPTION_ARG_NONE, &opt_ignore_sleep, "Do not lock on suspend/hibernate", NULL},
    {"quiet", 'q', 0, G_OPTION_ARG_NONE, &opt_quiet, "Output only fatal errors", NULL},
    {"verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose, "Output more messages", NULL},
    {"version", 0, 0, G_OPTION_ARG_NONE, &opt_print_version, "Print version number and exit", NULL},
    {NULL}
};

static GDBusProxy *logind_manager = NULL;
static GDBusProxy *logind_session = NULL;
static gint sleep_lock_fd = -1;
static gboolean preparing_for_sleep = FALSE;
