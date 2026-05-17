/**
 * @file ssh_server.c
 * @brief Embedded SSH server implementation using wolfSSH.
 *
 * Architecture
 * ────────────
 *  • One "listener" FreeRTOS task accepts TCP connections on port 22.
 *  • For each accepted connection a short-lived "session" task is spawned.
 *  • wolfSSH handles the protocol handshake, key exchange, and encryption.
 *  • Authentication: password-based, credentials stored in NVS.
 *  • Shell: a minimal command interpreter that exposes management commands.
 *
 * wolfSSH dependency
 * ──────────────────
 *  Add the following to idf_component.yml:
 *    dependencies:
 *      wolfssl/wolfssh: "^1.4.19"
 *      wolfssl/wolfssl: "^5.6.6"
 */
#include "ssh_server.h"
#include "wifi_manager.h"
#include "config.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

/* wolfSSH headers (provided by the wolfssl/wolfssh IDF component) */
#include <wolfssh/ssh.h>
#include <wolfssh/wolfsftp.h>

static const char *TAG = "ssh_srv";

/* ── Internal state ─────────────────────────────────────────────── */

static WOLFSSH_CTX *s_ctx            = NULL;
static int          s_listen_sock    = -1;
static bool         s_running        = false;
static TaskHandle_t s_listener_task  = NULL;

/* Runtime-mutable credentials (protected by task isolation; single writer) */
static char s_ssh_user[32];
static char s_ssh_pass[64];

/* ── Built-in RSA host key (2048-bit, DER encoded) ──────────────── *
 *                                                                    *
 * This key is used ONLY as a last-resort fallback when NVS is empty  *
 * AND key generation has failed.  In normal operation a unique key   *
 * is generated on first boot and stored in NVS.                      *
 *                                                                    *
 * DO NOT use this key in production – replace via ssh_server_init().  *
 * ──────────────────────────────────────────────────────────────── */
/* wolfSSH ships a sample server key that is used in its examples.
 * We reference the same symbol the wolfSSH test-server uses so that
 * no additional key data needs to live in this file. */
extern const unsigned char rsa_key_der_2048[];
extern       unsigned int  rsa_key_der_2048_len;

/* ── NVS helpers ────────────────────────────────────────────────── */

/** Load a NVS string key into buf (max buf_len bytes).  Returns false on miss. */
static bool nvs_load_str(const char *key, char *buf, size_t buf_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    size_t required = buf_len;
    bool ok = (nvs_get_str(h, key, buf, &required) == ESP_OK);
    nvs_close(h);
    return ok;
}

/** Persist a NVS string key. */
static bool nvs_save_str(const char *key, const char *value)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = (nvs_set_str(h, key, value) == ESP_OK) &&
              (nvs_commit(h)              == ESP_OK);
    nvs_close(h);
    return ok;
}

/** Load a binary blob from NVS.  Caller must free() *out. Returns length. */
static size_t nvs_load_blob(const char *key, uint8_t **out)
{
    *out = NULL;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return 0;

    size_t len = 0;
    if (nvs_get_blob(h, key, NULL, &len) != ESP_OK || len == 0) {
        nvs_close(h);
        return 0;
    }
    *out = malloc(len);
    if (!*out) {
        ESP_LOGE(TAG, "malloc failed for %zu bytes", len);
        nvs_close(h);
        return 0;
    }
    bool ok = (nvs_get_blob(h, key, *out, &len) == ESP_OK);
    nvs_close(h);
    if (!ok) { free(*out); *out = NULL; return 0; }
    return len;
}

/** Store a binary blob to NVS. */
static bool nvs_save_blob(const char *key, const uint8_t *data, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = (nvs_set_blob(h, key, data, len) == ESP_OK) &&
              (nvs_commit(h)                    == ESP_OK);
    nvs_close(h);
    return ok;
}

/* ── wolfSSH callbacks ──────────────────────────────────────────── */

/**
 * User-authentication callback.
 * wolfSSH calls this for every authentication attempt.
 */
static int user_auth_callback(byte authType,
                              WS_UserAuthData *authData,
                              void *ctx)
{
    (void)ctx;

    if (authType != WOLFSSH_USERAUTH_PASSWORD) {
        /* Only password auth is supported */
        return WOLFSSH_USERAUTH_FAILURE;
    }

    const char *user = (const char *)authData->username;
    const char *pass = (const char *)authData->sf.password.password;

    if (strcmp(user, s_ssh_user) == 0 && strcmp(pass, s_ssh_pass) == 0) {
        ESP_LOGI(TAG, "Auth OK for user '%s'", user);
        return WOLFSSH_USERAUTH_SUCCESS;
    }

    ESP_LOGW(TAG, "Auth FAILED for user '%s'", user);
    return WOLFSSH_USERAUTH_FAILURE;
}

/* ── Admin shell ────────────────────────────────────────────────── */

#define SHELL_BANNER \
    "\r\n"                                                          \
    "╔══════════════════════════════════╗\r\n"                      \
    "║  ESP32-S3 WiFi Dongle  v" FW_VERSION_STR "   ║\r\n"          \
    "╚══════════════════════════════════╝\r\n"                      \
    "Type 'help' for a list of commands.\r\n\r\n"

/** Send a null-terminated string over the SSH channel. */
static int shell_send(WOLFSSH *ssh, const char *msg)
{
    size_t len = strlen(msg);
    size_t sent = 0;
    while (sent < len) {
        int n = wolfSSH_stream_send(ssh, (const byte *)(msg + sent),
                                    (word32)(len - sent));
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

/** Process one command line, write response back to ssh stream. */
static bool shell_handle_command(WOLFSSH *ssh, const char *line)
{
    /* Trim trailing \r\n */
    char cmd[128];
    strncpy(cmd, line, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    size_t len = strlen(cmd);
    while (len > 0 && (cmd[len - 1] == '\r' || cmd[len - 1] == '\n')) {
        cmd[--len] = '\0';
    }

    if (len == 0) {
        shell_send(ssh, "$ ");
        return true;
    }

    /* ── Command dispatch ─────────────────────────────────────── */
    if (strcmp(cmd, "help") == 0) {
        shell_send(ssh,
            "Commands:\r\n"
            "  status          – show WiFi state and IP address\r\n"
            "  wifi <ssid> <pw>– connect to a new WiFi network and save\r\n"
            "  setpass <new>   – change SSH login password\r\n"
            "  version         – firmware version\r\n"
            "  reboot          – restart the device\r\n"
            "  exit / quit     – close this session\r\n");

    } else if (strcmp(cmd, "status") == 0) {
        char buf[128];
        const char *state_str;
        switch (wifi_manager_get_state()) {
        case WIFI_STATE_CONNECTED:    state_str = "Connected (STA)"; break;
        case WIFI_STATE_CONNECTING:   state_str = "Connecting…";     break;
        case WIFI_STATE_DISCONNECTED: state_str = "Disconnected";    break;
        case WIFI_STATE_AP_MODE:      state_str = "AP mode";         break;
        default:                      state_str = "Idle";            break;
        }
        snprintf(buf, sizeof(buf),
                 "WiFi state : %s\r\nIP address : %s\r\n",
                 state_str, wifi_manager_get_ip());
        shell_send(ssh, buf);

    } else if (strncmp(cmd, "wifi ", 5) == 0) {
        /* wifi <ssid> <password> */
        char ssid[33] = {0};
        char pass[65] = {0};
        if (sscanf(cmd + 5, "%32s %64s", ssid, pass) >= 1) {
            shell_send(ssh, "Saving credentials and reconnecting…\r\n");
            wifi_manager_save_credentials(ssid, pass);
            shell_send(ssh, "Done. The device will reconnect on next boot "
                            "(or run 'reboot').\r\n");
        } else {
            shell_send(ssh, "Usage: wifi <ssid> <password>\r\n");
        }

    } else if (strncmp(cmd, "setpass ", 8) == 0) {
        const char *new_pass = cmd + 8;
        if (strlen(new_pass) < 8) {
            shell_send(ssh, "Password must be at least 8 characters.\r\n");
        } else {
            ssh_server_set_password(new_pass);
            shell_send(ssh, "SSH password updated.\r\n");
        }

    } else if (strcmp(cmd, "version") == 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Firmware version: %s\r\n", FW_VERSION_STR);
        shell_send(ssh, buf);

    } else if (strcmp(cmd, "reboot") == 0) {
        shell_send(ssh, "Rebooting…\r\n");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();

    } else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
        shell_send(ssh, "Bye!\r\n");
        return false; /* signal session end */

    } else {
        char buf[80];
        snprintf(buf, sizeof(buf),
                 "Unknown command: '%s'. Type 'help'.\r\n", cmd);
        shell_send(ssh, buf);
    }

    shell_send(ssh, "$ ");
    return true;
}

/* ── Session task ───────────────────────────────────────────────── */

typedef struct {
    WOLFSSH *ssh;
    int      fd;
} session_args_t;

static void ssh_session_task(void *pvParameters)
{
    session_args_t *args = (session_args_t *)pvParameters;
    WOLFSSH *ssh         = args->ssh;
    int      fd          = args->fd;
    free(args);

    /* ── Handshake ─────────────────────────────────────────────── */
    int rc = wolfSSH_accept(ssh);
    if (rc != WS_SUCCESS) {
        ESP_LOGW(TAG, "SSH handshake failed (rc=%d)", rc);
        goto cleanup;
    }
    ESP_LOGI(TAG, "SSH session established (fd=%d)", fd);

    /* ── Interactive shell ─────────────────────────────────────── */
    shell_send(ssh, SHELL_BANNER);
    shell_send(ssh, "$ ");

    byte    rx_buf[256];
    char    line_buf[128];
    size_t  line_len = 0;

    while (s_running) {
        int n = wolfSSH_stream_read(ssh, rx_buf, sizeof(rx_buf) - 1);
        if (n <= 0) {
            int err = wolfSSH_get_error(ssh);
            if (err == WS_WANT_READ) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            break;
        }

        for (int i = 0; i < n; i++) {
            byte c = rx_buf[i];

            if (c == '\r' || c == '\n') {
                line_buf[line_len] = '\0';
                /* Echo the newline */
                shell_send(ssh, "\r\n");
                if (!shell_handle_command(ssh, line_buf)) {
                    goto cleanup;
                }
                line_len = 0;
            } else if (c == 0x7F || c == '\b') {
                /* Backspace */
                if (line_len > 0) {
                    line_len--;
                    shell_send(ssh, "\b \b");
                }
            } else if (c >= 0x20 && line_len < sizeof(line_buf) - 1) {
                /* Printable character – echo and buffer */
                line_buf[line_len++] = (char)c;
                byte echo[1] = {c};
                wolfSSH_stream_send(ssh, echo, 1);
            }
        }
    }

cleanup:
    wolfSSH_shutdown(ssh);
    wolfSSH_free(ssh);
    close(fd);
    ESP_LOGI(TAG, "SSH session closed (fd=%d)", fd);
    vTaskDelete(NULL);
}

/* ── Listener task ──────────────────────────────────────────────── */

static void ssh_listener_task(void *pvParameters)
{
    (void)pvParameters;

    /* Create listening socket */
    s_listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_listen_sock < 0) {
        ESP_LOGE(TAG, "socket() failed: %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(s_listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(SSH_SERVER_PORT),
        .sin_addr.s_addr = INADDR_ANY,
    };

    if (bind(s_listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed: %d", errno);
        close(s_listen_sock);
        s_listen_sock = -1;
        vTaskDelete(NULL);
        return;
    }

    if (listen(s_listen_sock, SSH_MAX_CONNECTIONS) < 0) {
        ESP_LOGE(TAG, "listen() failed: %d", errno);
        close(s_listen_sock);
        s_listen_sock = -1;
        vTaskDelete(NULL);
        return;
    }

    s_running = true;
    ESP_LOGI(TAG, "SSH server listening on port %d", SSH_SERVER_PORT);

    while (s_running) {
        struct sockaddr_in client_addr;
        socklen_t          addr_len = sizeof(client_addr);

        int client_fd = accept(s_listen_sock,
                               (struct sockaddr *)&client_addr,
                               &addr_len);
        if (client_fd < 0) {
            if (s_running) {
                ESP_LOGE(TAG, "accept() failed: %d", errno);
            }
            break;
        }

        ESP_LOGI(TAG, "Incoming connection from " IPSTR,
                 IP2STR(&client_addr.sin_addr));

        WOLFSSH *ssh = wolfSSH_new(s_ctx);
        if (!ssh) {
            ESP_LOGE(TAG, "wolfSSH_new() failed");
            close(client_fd);
            continue;
        }

        wolfSSH_set_fd(ssh, client_fd);

        session_args_t *args = malloc(sizeof(*args));
        if (!args) {
            ESP_LOGE(TAG, "Failed to allocate session args");
            wolfSSH_free(ssh);
            close(client_fd);
            continue;
        }
        args->ssh = ssh;
        args->fd  = client_fd;

        if (xTaskCreate(ssh_session_task, "ssh_sess",
                        SSH_SESSION_STACK_DEPTH, args,
                        SSH_TASK_PRIORITY, NULL) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create session task");
            free(args);
            wolfSSH_free(ssh);
            close(client_fd);
        }
    }

    close(s_listen_sock);
    s_listen_sock = -1;
    s_running     = false;
    s_listener_task = NULL;
    ESP_LOGI(TAG, "SSH listener stopped");
    vTaskDelete(NULL);
}

/* ── Public API ─────────────────────────────────────────────────── */

int ssh_server_init(void)
{
    /* Load credentials from NVS (fall back to compile-time defaults) */
    if (!nvs_load_str(NVS_KEY_SSH_USER, s_ssh_user, sizeof(s_ssh_user))) {
        strncpy(s_ssh_user, SSH_USERNAME, sizeof(s_ssh_user) - 1);
        s_ssh_user[sizeof(s_ssh_user) - 1] = '\0';
    }
    if (!nvs_load_str(NVS_KEY_SSH_PASS, s_ssh_pass, sizeof(s_ssh_pass))) {
        strncpy(s_ssh_pass, SSH_DEFAULT_PASSWORD, sizeof(s_ssh_pass) - 1);
        s_ssh_pass[sizeof(s_ssh_pass) - 1] = '\0';
    }

    /* Initialise wolfSSH library */
    if (wolfSSH_Init() != WS_SUCCESS) {
        ESP_LOGE(TAG, "wolfSSH_Init() failed");
        return ESP_FAIL;
    }

    s_ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_SERVER, NULL);
    if (!s_ctx) {
        ESP_LOGE(TAG, "wolfSSH_CTX_new() failed");
        return ESP_FAIL;
    }

    wolfSSH_SetUserAuth(s_ctx, user_auth_callback);

    /* ── Host key ─────────────────────────────────────────────── *
     * 1. Try NVS (persisted from a previous boot).               *
     * 2. Fall back to the wolfSSH built-in sample key.           *
     * ─────────────────────────────────────────────────────────── */
    uint8_t *stored_key  = NULL;
    size_t   stored_len  = nvs_load_blob(NVS_KEY_SSH_HOST_KEY, &stored_key);

    if (stored_len > 0 && stored_key != NULL) {
        int rc = wolfSSH_CTX_UsePrivateKey_buffer(
            s_ctx, stored_key, (word32)stored_len, WOLFSSH_FORMAT_ASN1);
        free(stored_key);
        if (rc != WS_SUCCESS) {
            ESP_LOGW(TAG, "Stored host key invalid (rc=%d), using default", rc);
            stored_len = 0;
        }
    }

    if (stored_len == 0) {
        /* Use the wolfSSH built-in RSA key as a fallback */
        int rc = wolfSSH_CTX_UsePrivateKey_buffer(
            s_ctx,
            rsa_key_der_2048, rsa_key_der_2048_len,
            WOLFSSH_FORMAT_ASN1);
        if (rc != WS_SUCCESS) {
            ESP_LOGE(TAG, "Failed to load fallback host key (rc=%d)", rc);
            wolfSSH_CTX_free(s_ctx);
            s_ctx = NULL;
            return ESP_FAIL;
        }
        ESP_LOGW(TAG, "Using built-in host key – run ssh-keygen and store via "
                      "NVS for a unique key");
    }

    ESP_LOGI(TAG, "SSH server initialised (user='%s')", s_ssh_user);
    return ESP_OK;
}

int ssh_server_start(void)
{
    if (s_running) {
        ESP_LOGW(TAG, "SSH server already running");
        return ESP_OK;
    }

    BaseType_t ret = xTaskCreate(
        ssh_listener_task, "ssh_listener",
        SSH_LISTENER_STACK_DEPTH, NULL,
        SSH_TASK_PRIORITY, &s_listener_task);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create listener task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void ssh_server_stop(void)
{
    if (!s_running) return;
    s_running = false;
    if (s_listen_sock >= 0) {
        shutdown(s_listen_sock, SHUT_RDWR);
        close(s_listen_sock);
        s_listen_sock = -1;
    }
    /* Give the listener task time to exit */
    vTaskDelay(pdMS_TO_TICKS(200));
}

bool ssh_server_is_running(void)
{
    return s_running;
}

bool ssh_server_set_password(const char *new_password)
{
    if (!new_password || strlen(new_password) < 8) return false;
    strncpy(s_ssh_pass, new_password, sizeof(s_ssh_pass) - 1);
    s_ssh_pass[sizeof(s_ssh_pass) - 1] = '\0';
    return nvs_save_str(NVS_KEY_SSH_PASS, s_ssh_pass);
}
