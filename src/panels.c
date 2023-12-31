#include "panels.h"
#include "chat_client.h"
#include "chat_common.h"
#include "chat_server.h"
#include "nuklear/src/nuklear.h"
#include <bits/getopt_core.h>
#include <pthread.h>
#include <signal.h>

#define WINDOW_FLAGS (NK_WINDOW_BORDER | NK_WINDOW_TITLE)

static void *thread_server_run(void *arg)
{
    ServerThreadArg *data = arg;
    ChatServer *chat_server = data->chat_server;

    while (true) {
        pthread_mutex_lock(&data->lock);
        if (!data->running)
            break;
        chat_server_update(chat_server);
        pthread_mutex_unlock(&data->lock);
    }

    return NULL;
}

static void thread_stop_server(ChatServerWindow *window)
{
    pthread_mutex_lock(&window->arg.lock);

    window->arg.running = false;

    chat_server_delete(window->chat_server);
    window->chat_server = NULL;

    pthread_mutex_unlock(&window->arg.lock);
    pthread_join(window->thread, NULL);
}

void user_window_init(ChatUserWindow *window, int max_messages)
{
    window->chat_user = chat_user_create();
    message_list_init(&window->messages, max_messages);
}

void user_window_deinit(ChatUserWindow *window)
{
    message_list_deinit(&window->messages);
    chat_user_delete(window->chat_user);
    window->chat_user = NULL;
}

void user_window_draw(struct nk_context *ctx, ChatUserWindow *window, int ww, int wh)
{
    static char buffer[200];
    static char username[USERNAME_MAX];
    static int username_len;
    static char ip_address[21];
    static int ip_address_len;
    static int port;
    static int w_width;
    static char msg_buffer[MESSAGE_MAX];
    static int msg_len;
    static ChatMessage message_buffer;
    static enum Flags {
        FLAG_USERNAME_EMPTY = 0x01,
    } flags;

    int width = ww - 600;
    if (width <= 0)
        width = 0;
    int height = wh;

    ChatUser *user = window->chat_user;

    if (window->chat_user->status != CHAT_USER_STATUS_CONNECTED) {
        if (nk_begin(ctx, "Join Chat", nk_rect(600, 0, width, height), WINDOW_FLAGS)) {
            float ratio[] = {0.2f, 0.3f};

            nk_layout_row(ctx, NK_DYNAMIC, 30, 2, ratio);
            nk_label(ctx, "IP Address", NK_TEXT_LEFT);
            nk_edit_string(ctx, NK_EDIT_SIMPLE, ip_address, &ip_address_len, 20, nk_filter_ascii);

            nk_layout_row_dynamic(ctx, 30, 2);
            nk_property_int(ctx, "Port", 0, &port, 65535, 1, 1);

            nk_layout_row(ctx, NK_DYNAMIC, 30, 2, ratio);
            nk_label(ctx, "Username", NK_TEXT_LEFT);
            nk_edit_string(ctx,
                           NK_EDIT_SIMPLE,
                           username,
                           &username_len,
                           USERNAME_MAX,
                           nk_filter_ascii);

            nk_layout_row_dynamic(ctx, 30, 1);
            bool enter_pressed =
                nk_window_has_focus(ctx) && ctx->input.keyboard.keys[NK_KEY_ENTER].clicked;

            if (nk_button_label(ctx, "Join Chat") || enter_pressed) {
                if (username_len == 0) {
                    flags |= FLAG_USERNAME_EMPTY;
                } else {
                    flags &= ~FLAG_USERNAME_EMPTY;
                    if (chat_user_connect(user, port, ip_address) != CHAT_USER_SUCESS) {
                    } else {

                        chat_message_make_and_send(&user->socket,
                                                   &message_buffer,
                                                   CHAT_MESSAGE_CLIENT_CHANGE_INFO,
                                                   username,
                                                   username_len);
                        message_list_init(&window->messages, 200);
                    }
                }
            }

            if (flags & FLAG_USERNAME_EMPTY) {
                nk_layout_row_dynamic(ctx, 30, 1);
                nk_label(ctx, "Username Field is empty", NK_TEXT_LEFT);
            }

            switch (user->status) {
            case CHAT_USER_STATUS_FAILED:
                nk_layout_row_dynamic(ctx, 30, 1);
                nk_label(ctx, "Failed to connect to server", NK_TEXT_LEFT);
                break;

            case CHAT_USER_STATUS_NON_CONFIRMED:
                nk_layout_row_dynamic(ctx, 30, 1);
                nk_label(ctx, "Wanting Server accept", NK_TEXT_LEFT);
                break;

            case CHAT_USER_STATUS_REFUSED:
                nk_layout_row_dynamic(ctx, 30, 1);
                nk_label(ctx, "Server Refused connection", NK_TEXT_LEFT);
                break;

            case CHAT_USER_STATUS_DISCONNECTED:
                nk_layout_row_dynamic(ctx, 30, 1);
                nk_label(ctx, "You were disconnected from the server", NK_TEXT_LEFT);
                break;

            case CHAT_USER_STATUS_BANNED:
                nk_layout_row_dynamic(ctx, 30, 1);
                nk_label(ctx, "You were banned from the server", NK_TEXT_LEFT);
                break;

            case CHAT_USER_STATUS_NONE:
            case CHAT_USER_STATUS_CONNECTED:
                break;
            }
        }
        nk_end(ctx);
    } else {
        sprintf(buffer, "Messages - IP %s - Port %d", user->chat_ip_address, (int)user->chat_port);

        if (nk_begin(ctx, "Chat Prompt", nk_rect(600, 0, width, height), WINDOW_FLAGS)) {
            nk_layout_row_dynamic(ctx, height - 150, 1);
            if (nk_group_begin(ctx, "Messages", WINDOW_FLAGS)) {
                w_width = nk_window_get_width(ctx);

                float char_per_line = w_width * 0.8f / 8.3f;
                MessageList *msgs = &window->messages;

                for (size_t i = 0; i < msgs->count; ++i) {
                    int lines = ceilf(msgs->messages[i].msg_len / char_per_line);
                    int row_height = lines * 20;
                    nk_layout_row(ctx, NK_DYNAMIC, row_height, 3, (float[]) {0.15f, 0.05f, 0.75f});
                    nk_label(ctx,
                             msgs->messages[i].username,
                             NK_TEXT_ALIGN_TOP | NK_TEXT_ALIGN_LEFT);
                    nk_label(ctx, " : ", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_TOP);
                    nk_label_wrap(ctx, msgs->messages[i].msg);
                }
                nk_group_end(ctx);
            }

            nk_layout_row(ctx, NK_DYNAMIC, 30, 2, (float[]) {0.8f, 0.2f});
            nk_edit_string(ctx,
                           NK_EDIT_SIMPLE,
                           msg_buffer,
                           &msg_len,
                           MESSAGE_MAX,
                           nk_filter_default);

            bool enter_pressed =
                nk_window_has_focus(ctx) && ctx->input.keyboard.keys[NK_KEY_ENTER].clicked;

            if ((nk_button_label(ctx, "Send Message") || enter_pressed) && msg_len > 0) {
                PRINT_DEBUG("Message size is %d\n", msg_len);
                chat_message_make_and_send(&user->socket,
                                           &message_buffer,
                                           CHAT_MESSAGE_CLIENT_MESSAGE,
                                           msg_buffer,
                                           msg_len);
                msg_len = 0;
            }

            nk_layout_row_dynamic(ctx, 30, 1);
            if (nk_button_label(ctx, "Disconnect from chat")) {
                chat_user_disconnect(user, CHAT_USER_STATUS_DISCONNECTED);
                message_list_deinit(&window->messages);
            }
        }
        nk_end(ctx);
    }
}

void server_window_init(ChatServerWindow *window)
{
    window->chat_server = NULL;
    pthread_mutex_init(&window->arg.lock, NULL);
}

void server_window_deinit(ChatServerWindow *window)
{
    if (window->chat_server) {
        thread_stop_server(window);
    }
    pthread_mutex_destroy(&window->arg.lock);
}

void server_window_draw(struct nk_context *ctx, ChatServerWindow *window, int ww, int wh)
{
    static int port = 0;
    static char buffer_label[100];

    int height = wh;

    if (!window->chat_server) {
        if (nk_begin(ctx, "Server", nk_rect(0, 0, 600, 120), WINDOW_FLAGS)) {
            float port_row_ratio[] = {0.5f, 0.5f};

            nk_layout_row(ctx, NK_DYNAMIC, 35, 2, port_row_ratio);
            nk_property_int(ctx, "Port", 0, &port, 65535, 1, 1);

            bool enter_pressed =
                nk_window_has_focus(ctx) && ctx->input.keyboard.keys[NK_KEY_ENTER].clicked;

            if (nk_button_label(ctx, "Create chat group") || enter_pressed) {
                if ((window->chat_server = chat_server_create(port))) {
                    window->arg.running = true;
                    window->arg.chat_server = window->chat_server;
                    pthread_mutex_unlock(&window->arg.lock);
                    pthread_create(&window->thread, NULL, thread_server_run, &window->arg);
                }
            }
        }
        nk_end(ctx);
    } else {
        if (nk_begin(ctx, "Server Manager", nk_rect(0, 0, 600, height), WINDOW_FLAGS)) {
            nk_layout_row_dynamic(ctx, 30, 1);
            sprintf(buffer_label, "Server running at port %d", port);
            nk_label(ctx, buffer_label, NK_TEXT_CENTERED);

            nk_layout_row_dynamic(ctx, 30, 1);
            if (nk_button_label(ctx, "Stop server")) {
                thread_stop_server(window);
                nk_end(ctx);
                return;
            }

            static char *tabs[3] = {"Messages", "Server info", "Members"};
            static enum Tab { TAB_MESSAGES, TAB_INFO, TAB_MEMBERS, TAB_COUNT } tab_selected;

            nk_layout_row_dynamic(ctx, 35, 1);
            if (nk_group_begin(ctx, "Tab", NK_WINDOW_NO_SCROLLBAR)) {
                nk_layout_row_static(ctx, 25, 100, TAB_COUNT);
                for (int i = 0; i < TAB_COUNT; ++i) {
                    int non_selected = 0;
                    int selected = 1;
                    int *select_status = (i == tab_selected) ? &selected : &non_selected;
                    if (nk_selectable_label(ctx, tabs[i], NK_TEXT_CENTERED, select_status)) {
                        tab_selected = i;
                    }
                }

                nk_group_end(ctx);
            }

            switch (tab_selected) {
            case TAB_MESSAGES:
                nk_layout_row_dynamic(ctx, height - 165, 1);
                if (nk_group_begin(ctx, "User Messages", WINDOW_FLAGS)) {
                    float w_width = nk_window_get_width(ctx);
                    float char_per_line = w_width * 0.8f / 8.3f;
                    MessageList *msgs = &window->chat_server->received;

                    for (size_t i = 0; i < msgs->count; ++i) {
                        int lines = ceilf(msgs->messages[i].msg_len / char_per_line);
                        int row_height = lines * 20;
                        nk_layout_row(ctx,
                                      NK_DYNAMIC,
                                      row_height,
                                      3,
                                      (float[]) {0.15f, 0.05f, 0.75f});
                        nk_label(ctx,
                                 msgs->messages[i].username,
                                 NK_TEXT_ALIGN_TOP | NK_TEXT_ALIGN_LEFT);
                        nk_label(ctx, " : ", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_TOP);
                        nk_label_wrap(ctx, msgs->messages[i].msg);
                    }

                    nk_group_end(ctx);
                }
                break;

            case TAB_INFO: {
                ChatServerInfoList *info = &window->chat_server->info;

                for (int i = 0; i < info->count; ++i) {
                    ChatServerInfo *inf = &info->data[i];

                    switch (inf->type) {
                    case INFO_CLIENT_CHANGE_NAME:
                        sprintf(buffer_label,
                                "Client %s changed name to %s",
                                inf->data1,
                                inf->data2);
                        nk_layout_row_dynamic(ctx, 30, 1);
                        nk_label_wrap(ctx, buffer_label);
                        break;

                    case INFO_CLIENT_BANNED:
                        sprintf(buffer_label, "Client %s was banned", inf->data1);
                        nk_layout_row_dynamic(ctx, 30, 1);
                        nk_label_wrap(ctx, buffer_label);
                        break;

                    case INFO_CLIENT_ACCEPTED:
                        sprintf(buffer_label, "Client %s entered chat", inf->data1);
                        nk_layout_row_dynamic(ctx, 30, 1);
                        nk_label_wrap(ctx, buffer_label);
                        break;

                    case INFO_CLIENT_DISCONNECTED:
                        sprintf(buffer_label,
                                "Client %s was disconnected from the chat",
                                inf->data1);
                        nk_layout_row_dynamic(ctx, 30, 1);
                        nk_label_wrap(ctx, buffer_label);
                        break;
                    }
                }
            } break;

            case TAB_MEMBERS:
                for (int i = 0; i < window->chat_server->clients_count; ++i) {
                    nk_layout_row_dynamic(ctx, 30, 2);
                    nk_label(ctx, window->chat_server->clients[i].username, NK_TEXT_CENTERED);
                    if (nk_button_label(ctx, "Ban")) {
                        chat_server_ban_user(window->chat_server, i);
                    }
                }
                break;
            }
        }
        nk_end(ctx);
    }
}
